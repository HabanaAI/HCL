#include "platform/gen2_arch_common/signals/manager.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gen2_arch_common/hcl_collective_routines.h"
#include "infra/scal/gen2_arch_common/scal_utils.h"
#include "infra/scal/gen2_arch_common/scal_names.h"
#include "hcl_global_conf.h"  // for GCFG_*
#include "platform/gen2_arch_common/hccl_device.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "hcl_math_utils.h"
#include <algorithm>

#define MIN_SIZE_OF_FW_CMD 4

SignalsManager::SignalDescription::SignalDescription()
: event(SignalEvent::SIGNAL_EVENT_MAX), consumed(true), signalWaitDesc(nullptr)
{
}
SignalsManager::SignalDescription::SignalDescription(SignalEvent signalEvent, bool startConsumed)
: event(signalEvent), consumed(startConsumed), signalWaitDesc(nullptr)
{
}

bool SignalsManager::SignalDescription::wasRegistered() const
{
    return signalWaitDesc && signalWaitDesc->event != WaitEvent::WAIT_EVENT_MAX;
}

bool SignalsManager::SignalDescription::wasSignalled() const
{
    // a SignalEvent must finish the collective (reach finalize()) with it being consumed. All Signals are HCL-based
    // and will be consumed by dequeueSignalAddress - except FORCE_ORDER which is provided by SCAL.
    return consumed || event == SignalEvent::FORCE_ORDER;
}

SignalsManager::SignalWaitEvent::SignalWaitEvent()
: event(WaitEvent::WAIT_EVENT_MAX),
  signals({}),
  method(WaitMethod::WAIT_METHOD_MAX),
  currentPhase(WAIT_PHASE_MAX),
  numSignals(0),
  numExecutedFences(0),
  numExpectedFences(0),
  nextPhaseEvent(nullptr)
{
}
SignalsManager::SignalWaitEvent::SignalWaitEvent(WaitEvent                                        waitEvent,
                                                 llvm_vecsmall::SmallVector<SignalDescription, 8> signalDescs,
                                                 WaitMethod                                       waitMethod,
                                                 WaitPhase                                        waitPhase,
                                                 unsigned                                         longtermSyncObjIdx,
                                                 bool                                             accSignals,
                                                 bool                                             expectAnotherPhase_)
: event(waitEvent),
  signals(signalDescs),
  method(waitMethod),
  currentPhase(waitPhase),
  longtermIdx(longtermSyncObjIdx),
  accumulateSignals(accSignals),
  expectAnotherPhase(expectAnotherPhase_),
  numSignals(0),
  numExecutedFences(0),
  numExpectedFences(1),
  nextPhaseEvent(nullptr)
{
}

SignalsManager::SignalWaitEvent& SignalsManager::SignalWaitEvent::operator=(const SignalWaitEvent& other)
{
    this->event = other.event;
    this->signals.clear();
    this->method            = other.method;
    this->currentPhase      = other.currentPhase;
    this->numSignals        = other.numSignals;
    this->numExecutedFences = other.numExecutedFences;
    this->numExpectedFences = other.numExpectedFences;
    this->longtermIdx       = other.longtermIdx;

    this->nextPhaseEvent = nullptr;

    return *this;
}

bool SignalsManager::SignalWaitEvent::wasSignalled() const
{
    auto currentEvent = this;
    do
    {
        for (const SignalDescription& desc : currentEvent->signals)
        {
            if (!desc.consumed)
            {
                return false;
            }
        }
        currentEvent = currentEvent->nextPhaseEvent;
    } while (currentEvent);
    return true;
}

SignalsManager::FenceCheckResult SignalsManager::SignalWaitEvent::wasFenced(bool checkPhases) const
{
    auto currentEvent = this;
    do
    {
        if (currentEvent->numExecutedFences < currentEvent->numExpectedFences)
        {
            return {false, currentEvent->event};
        }
        currentEvent = currentEvent->nextPhaseEvent;
    } while (currentEvent && checkPhases);

    return {true, WaitEvent::WAIT_EVENT_MAX};
}

bool SignalsManager::SignalWaitEvent::wasCompleted() const
{
    return wasSignalled() && wasFenced().isFenced && !expectAnotherPhase;
}

SignalsManager::SignalsManager(HclGraphSyncGen2Arch& graphSync, Gen2ArchScalUtils* utils, unsigned cgSize)
: m_graph(nullptr), m_graphSync(graphSync), m_utils(utils), m_cgSize(cgSize), m_commonState(nullptr)
{
    m_completionTracker.resize(hcl::ScalStream::getCcbSize() / MIN_SIZE_OF_FW_CMD);  // sizeofCCB/MinimumSizeOfCommand
    m_allowGraphCaching = GCFG_HCL_ALLOW_GRAPH_CACHING.value();
}

SignalsManager::Graph::Graph()
{
    // Nullify all internal data sets.
    static const SignalWaitEvent _empty;
    for (size_t i = 0; i < m_events.size(); i++)
    {
        m_events[i] = _empty;
    }

    for (size_t i = 0; i < m_signals.size(); i++)
    {
        m_signals[i].clear();
    }
}

bool SignalsManager::isCachingRequired(CommonState& commonState)
{
    return m_allowGraphCaching && commonState.m_collectiveOp != eHCLNoCollective &&
           commonState.m_collectiveOp != eHCLAll2All && commonState.m_collectiveOp != eHCLBroadcast &&
           commonState.m_collectiveOp != eHCLSimpleBroadcast;
}

void SignalsManager::initialize(CommonState* commonState, uint64_t cuid)
{
    Graph* old     = m_graph;
    bool   created = updateGraph(cuid, commonState);

    m_commonState = commonState;

    if (created || !m_usingCache)
    {
        // number of boxes defines the numbers of used events and phases
        uint32_t nBoxes  = div((uint32_t)m_commonState->m_dynamicComm.getCommSize(),
                              (uint32_t)m_commonState->m_dynamicComm.getScaleupGroupSize());
        uint64_t nPhases = div(nBoxes, m_commonState->m_scaleoutBuffersAmount);

        m_graph->m_maxPhases = std::max(MIN_PHASES, nPhases);
        LOG_HCL_DEBUG(HCL, "for ({}) boxes setup, and m_max_Phases is ({})", nBoxes, m_graph->m_maxPhases);
    }
    else
    {
        m_graph->m_firstCollective =
            m_prevIteration < (int)commonState->m_boxIter ? old->m_firstCollective : !m_usingCache;
    }

    m_prevIteration = m_commonState->m_boxIter;

    if (likely(old) && m_usingCache) handleLongtermOnGraphSwitch(created, old);
}

bool SignalsManager::updateGraph(uint64_t cuid, CommonState* commonState)
{
    bool           isCreated = false;
    const HCL_Comm comm      = commonState->m_comm;
    m_usingCache             = isCachingRequired(*commonState) && cuid != 0;
    if (m_usingCache)
    {
        if (comm >= m_cache.size())
        {
            const uint32_t newCommsCountInCache =
                std::min(std::max((uint32_t)(comm + 1), (uint32_t)(m_cache.size() * 2)), (uint32_t)(2 << 16));
            LOG_HCL_DEBUG(HCL, "resizing m_cache for comm {}, new size {}", comm, newCommsCountInCache);
            m_cache.resize(newCommsCountInCache);
        }
        if (unlikely(m_cache[comm].count(cuid) == 0))
        {
            m_cache[comm][cuid];  // allocate a new graph, without invoking a copy constructor.
            const uint64_t cache_size = m_cache[comm].size();
            LOG_HCL_DEBUG(HCL,
                          "Can't find cached Graph for comm {} cuid 0x{:x}. Allocating new one. cache size {} elements",
                          comm,
                          cuid,
                          cache_size);
            isCreated = true;
        }
    }

    m_graph = m_usingCache ? &m_cache[comm].at(cuid) : &m_nonCachedGraph;

    LOG_HCL_DEBUG(HCL,
                  "Using a cached graph for comm {} cuid 0x{:x} is {} (graph addr = 0x{:x}). Current cache size: {}",
                  comm,
                  cuid,
                  m_usingCache ? "allowed" : "not allowed",
                  (uint64_t)m_graph,
                  m_cache.size());

    return isCreated;
}

void SignalsManager::handleLongtermOnGraphSwitch(bool created, Graph* oldGraph)
{
    if (unlikely(created))
    {
        for (int i = (int)WaitMethod::GPSO_LONGTERM_8; i >= 0; i--)
        {
            int phase = 0;
            for (WaitPhaseEntry& entry : oldGraph->m_waitMethods[i])
            {
                SignalWaitEvent* desc = entry.waitEvent;
                if (!desc) break;
                else if (desc->wasFenced().isFenced)
                {
                    if (!isNextReusable((WaitMethod)i, phase, oldGraph)) desc->numExecutedFences = 0;
                    LOG_HCL_DEBUG(HCL,
                                  "event {} on method {} phase {} from oldGraph graph completed",
                                  desc->event,
                                  i,
                                  phase);
                    for (SignalDescription& s_desc : desc->signals)
                        s_desc.consumed = false;
                }
                else
                {
                    LOG_HCL_DEBUG(HCL,
                                  "performing data copy for event {} on method {} phase {}",
                                  desc->event,
                                  i,
                                  phase);
                    m_graph->m_events[(unsigned)desc->event]   = oldGraph->m_events[(unsigned)desc->event];
                    m_graph->m_waitMethods[i][phase].waitEvent = &m_graph->m_events[(unsigned)desc->event];
                    m_graph->m_waitMethods[i][phase].signalsPerPhase =
                        oldGraph->m_waitMethods[i][phase].signalsPerPhase;
                    if (phase > 0)
                    {
                        m_graph->m_waitMethods[i][phase - 1].waitEvent->nextPhaseEvent =
                            m_graph->m_waitMethods[i][phase].waitEvent;
                    }
                }
                phase++;
            }
        }
    }
    else
    {
        if (unlikely(m_graph->m_firstCollective)) updateEventsOnLongterm(oldGraph);
        for (size_t j = 0; j < m_graph->m_events.size(); j++)
        {
            if (isLongTerm(oldGraph->m_events[j].method))
            {
                if (!oldGraph->m_events[j].wasFenced().isFenced)
                {
                    m_graph->m_events[j].numExecutedFences = oldGraph->m_events[j].numExecutedFences;
                    m_graph->m_events[j].currentPhase      = oldGraph->m_events[j].currentPhase;
                }
            }
            else if (m_graph->m_events[j].event != WaitEvent::WAIT_EVENT_MAX &&
                     oldGraph->m_events[j].event == WaitEvent::WAIT_EVENT_MAX)
            {
                if (isReusableEvent((WaitEvent)j)) m_graph->m_events[j].currentPhase = 0;
                m_graph->m_events[j].numExecutedFences = 0;
            }
            else if (!isLongTerm(oldGraph->m_events[j].method))
            {
                m_graph->m_events[j].numExecutedFences = 0;
            }
            for (SignalDescription& desc : m_graph->m_events[j].signals)
            {
                if ((WaitEvent)j != WaitEvent::GENERAL_INTERNAL_COMPLETION_EVENT) desc.consumed = false;
            }
        }
    }
}

void SignalsManager::updateEventsOnLongterm(Graph* oldGraph)
{
    for (int i = (int)WaitMethod::GPSO_LONGTERM_8; i >= 0; i--)
    {
        int phase = 0;
        for (WaitPhaseEntry& entry : oldGraph->m_waitMethods[i])
        {
            SignalWaitEvent* desc = entry.waitEvent;
            if (!desc) break;
            if (m_graph->m_events[(unsigned)desc->event].event == WaitEvent::WAIT_EVENT_MAX)
            {
                m_graph->m_events[(unsigned)desc->event] = oldGraph->m_events[(unsigned)desc->event];
            }
            m_graph->m_events[(unsigned)desc->event].numExpectedFences =
                oldGraph->m_events[(unsigned)desc->event].numExpectedFences;
            if (isReusableEvent(desc->event)) m_graph->m_events[(unsigned)desc->event].signals.clear();
            m_graph->m_waitMethods[i][phase].waitEvent          = &m_graph->m_events[(unsigned)desc->event];
            m_graph->m_waitMethods[i][phase].signalsPerPhase    = oldGraph->m_waitMethods[i][phase].signalsPerPhase;
            m_graph->m_events[(unsigned)desc->event].numSignals = oldGraph->m_events[(unsigned)desc->event].numSignals;
            phase++;
        }
    }
}

void SignalsManager::finalize(bool entireCollective)
{
    if (unlikely(!m_graph))
    {
        return;
    }

    // Ensure all SignalEvents were consumed during the collective (otherwise, we registered a resource but it was not
    // used by the creation functions, thus it's missing/not needed).
    if (unlikely(m_graph->m_firstCollective))
    {
        for (size_t i = 0; i < m_graph->m_events.size(); i++)
        {
            SignalWaitEvent& desc = m_graph->m_events[i];
            if (desc.event == WaitEvent::WAIT_EVENT_MAX) continue;
            for (SignalDescription& signalDesc : desc.signals)
            {
                VERIFY(signalDesc.wasSignalled(),
                       "In event {}, signal {} was not signalled (even though it was enqueued)!",
                       desc.event,
                       signalDesc.event);
            }
            // As long as the event is not a general completion event, make sure that it was fenced. If it's a longterm
            // event, only check for fencing if this is the end of the entire collective.
            if ((!isLongTerm(desc.method) || entireCollective) && desc.event != WaitEvent::GENERAL_COMPLETION_EVENT &&
                desc.event != WaitEvent::GENERAL_INTERNAL_COMPLETION_EVENT)
            {
                auto fenceCheckResult = desc.wasFenced();
                VERIFY(fenceCheckResult.isFenced,
                       "Event {} was not fenced (missing an 'streamAddSingleWaitIfNeeded()' call)",
                       fenceCheckResult.notFencedEvent);
            }
        }
    }

    if (!m_usingCache) resetGraph();

    m_graph->m_firstUse = !m_usingCache;

    std::fill(m_graph->m_waitMethodsToClean.begin(), m_graph->m_waitMethodsToClean.end(), false);
}

void SignalsManager::finalize(WaitEvent waitEvent)
{
    SignalWaitEvent& desc = m_graph->m_events[(unsigned)waitEvent];

    desc.numExecutedFences++;

    if (!isLongTerm(desc.method) ||
        (desc.wasCompleted() && ((unsigned)m_graphSync.getLongtermAmount() == desc.longtermIdx + 1)))
    {
        // Add an easy-to-access (read: O(1)) list of resources that should be cleaned up at the end of a credit.
        markMethodForCleanup(desc.method);
    }
}

void SignalsManager::resetGraph()
{
    m_graph->m_requestedEventsBitmap = 0;

    for (size_t i = 0; i < m_graph->m_signals.size(); i++)
    {
        m_graph->m_signals[i].clear();
    }

    for (size_t i = 0; i < m_graph->m_waitMethods.size(); i++)
    {
        WaitPhase        lastPhaseForMethod = getLastPhase((WaitMethod)i);
        SignalWaitEvent* lastPhase          = m_graph->m_waitMethods[i][(unsigned)lastPhaseForMethod].waitEvent;

        if (!isLongTerm((WaitMethod)i) || (lastPhase != nullptr && lastPhase->wasCompleted()))
        {
            std::array<WaitPhaseEntry, WAIT_PHASE_MAX>& phases = m_graph->m_waitMethods[i];
            WaitPhaseEntry                              empty  = {nullptr, 0};
            std::fill_n(phases.begin(), m_graph->m_maxPhases, empty);
        }
    }

    static const SignalWaitEvent _empty;
    for (size_t i = 0; i < m_graph->m_events.size(); i++)
    {
        if (!isLongTerm(m_graph->m_events[i].method) || m_graph->m_events[i].wasCompleted())
        {
            m_graph->m_events[i] = _empty;
        }
        else
        {
            m_graph->m_events[i].signals.clear();
        }
    }
}

void SignalsManager::newCollective(const HCL_Comm comm)
{
    LOG_HCL_DEBUG(HCL, "comm {}", comm);
    if (comm >= m_cache.size() || m_cache[comm].empty())
    {
        LOG_HCL_DEBUG(HCL, "deleting m_graph");
        m_graph = nullptr;
    }
}

void SignalsManager::enqueueCompletion(llvm_vecsmall::SmallVector<SignalDescription, 8>&& signalEvents)
{
    enqueueWait(WaitEvent::GENERAL_COMPLETION_EVENT, std::move(signalEvents), WaitMethod::EXTERNAL_CG_SO);
}

void SignalsManager::enqueueInternalCompletion(SignalEvent signalEvent)
{
    enqueueWait(WaitEvent::GENERAL_INTERNAL_COMPLETION_EVENT, {{signalEvent, true}}, WaitMethod::INTERNAL_CG_SO);
}

void SignalsManager::enqueueWait(WaitEvent                                          waitEvent,
                                 llvm_vecsmall::SmallVector<SignalDescription, 8>&& signalEvents,
                                 WaitMethod                                         waitMethod,
                                 WaitPhase                                          waitPhase,
                                 unsigned                                           numExpectedFences,
                                 unsigned                                           longtermIdx,
                                 bool                                               accSignals,
                                 bool                                               expectAnotherPhase)
{
    if (likely((!m_graph->m_firstUse && !isReusableEvent(waitEvent)) ||
               (isReusableEvent(waitEvent) && !m_graph->m_firstCollective)))
    {
        // This graph has already been created, so we can just... not re-create it as it already exists in m_cache.
        return;
    }

    const WaitMethod effectiveMethod = (WaitMethod)(((unsigned)waitMethod) + longtermIdx);

    LOG_HCL_DEBUG(HCL,
                  "trying to enqueue event {} on method {} phase {} fence={}",
                  waitEvent,
                  waitMethod,
                  waitPhase,
                  numExpectedFences);

    if (m_graph->m_waitMethods[(unsigned)effectiveMethod][waitPhase].waitEvent != nullptr)
    {
        VERIFY(m_graph->m_waitMethods[(unsigned)effectiveMethod][waitPhase].waitEvent->event == waitEvent,
               "Tried to enqueue {} on resource {} (waitPhase {}) but {} was already registered on same resource/phase",
               waitEvent,
               waitMethod,
               waitPhase,
               m_graph->m_waitMethods[(unsigned)effectiveMethod][waitPhase].waitEvent->event);
    }
    // Retrieve the SignalWaitEvent instance associated with waitEvent. If one doesn't exist - create one.
    if (!hasWaitEvent(waitEvent))
    {
        m_graph->m_events[(unsigned)waitEvent] =
            {waitEvent, {}, effectiveMethod, waitPhase, longtermIdx, accSignals, expectAnotherPhase};
    }
    else if (isLongTerm(effectiveMethod))
    {
        m_graph->m_events[(unsigned)waitEvent].longtermIdx = longtermIdx;
        if (isReusableEvent(waitEvent))
        {
            VERIFY(numExpectedFences <= 1,
                   "Expected fences for each reusable event update is {} but should be either 1 or 0");
            m_graph->m_events[(unsigned)waitEvent].numExpectedFences += numExpectedFences;
        }
    }
    SignalWaitEvent& desc = m_graph->m_events[(unsigned)waitEvent];

    VERIFY(desc.method == effectiveMethod,
           "WaitMethod for event {} is set to {} but tried to enqueue {}",
           waitEvent,
           desc.method,
           effectiveMethod);

    // Always override this, for cases where the scaleout provider logic enqueues the event first
    if (numExpectedFences > desc.numExpectedFences)
    {
        desc.numExpectedFences = numExpectedFences;
    }

    // For each SignalEvent (encapsulated by SignalDescription) we want to add easy (O(1)) references to it when needed.
    // We'll either ask 'what address should this SignalEvent signal to?', or 'what signals will work on my resource?'
    // m_signals work on the first, m_methods on the latter.
    for (SignalDescription& signalDesc : signalEvents)
    {
        llvm_vecsmall::SmallVector<SignalDescription*, 8>& queue = m_graph->m_signals[(unsigned)signalDesc.event];

        signalDesc.signalWaitDesc = &desc;
        desc.signals.push_back(signalDesc);
        queue.push_back(&desc.signals.back());
        m_graph->m_requestedEventsBitmap |= 1 << ((unsigned int)desc.signals.back().event);
    }
    desc.expectAnotherPhase                                                = expectAnotherPhase;
    m_graph->m_waitMethods[(unsigned)effectiveMethod][waitPhase].waitEvent = &desc;
}

bool SignalsManager::isEventRegistered(SignalEvent signalEvent)
{
    return (m_graph->m_requestedEventsBitmap & (1 << (unsigned int)signalEvent)) != 0;
}

void SignalsManager::allocateResources()
{
    if (likely(!m_graph->m_firstCollective))
    {
        return;
    }

    for (size_t i = 0; i < m_graph->m_waitMethods.size(); i++)
    {
        // Assemble a semi-linked list for easier access between phases of a method.
        for (size_t j = 1; j < m_graph->m_maxPhases; j++)
        {
            if (m_graph->m_waitMethods[i][j].waitEvent != nullptr &&
                m_graph->m_waitMethods[i][j].waitEvent->event != WaitEvent::WAIT_EVENT_MAX)
            {
                SignalWaitEvent* prevDesc = m_graph->m_waitMethods[i][j - 1].waitEvent;
                VERIFY(prevDesc != nullptr,
                       "Trying to allocate event ({}) on phase ({}/{}), method({}/{}), but a previous phase is not "
                       "initialized",
                       m_graph->m_waitMethods[i][j].waitEvent->event,
                       j,
                       m_graph->m_maxPhases,
                       i,
                       m_graph->m_waitMethods.size());
                prevDesc->nextPhaseEvent = prevDesc != m_graph->m_waitMethods[i][j].waitEvent
                                               ? m_graph->m_waitMethods[i][j].waitEvent
                                               : nullptr;
            }
        }
    }

    for (size_t i = 0; i < m_graph->m_waitMethods.size(); i++)
    {
        unsigned   numSignals         = 0;
        WaitMethod waitMethod         = (WaitMethod)i;
        WaitPhase  lastPhaseForMethod = getLastPhase(waitMethod);

        std::array<WaitPhaseEntry, WAIT_PHASE_MAX>& phases = m_graph->m_waitMethods[i];
        if (isLongTerm(waitMethod) && lastPhaseForMethod != WAIT_PHASE_MAX)
        {
            numSignals = phases[(unsigned)lastPhaseForMethod].waitEvent->numSignals;
            LOG_HCL_DEBUG(HCL,
                          "Detected pre-existing longterm gpso on event {} method {} phase {} with numSignals={}",
                          phases[(unsigned)lastPhaseForMethod].waitEvent->event,
                          waitMethod,
                          lastPhaseForMethod,
                          numSignals);
        }

        for (size_t j = 0; j < m_graph->m_maxPhases; j++)
        {
            SignalWaitEvent* desc = phases[j].waitEvent;
            if (!desc || !hasWaitEvent(desc->event)) continue;

            // For each WaitEvent descriptor we want to calculate how many signals will be consumed for the completion
            // of the event. We know the resource (desc->method) and we know the numSignals (calculateNumSignals()).
            // In case this is not the first re-use of a GPSO, numSignals will increment appropriately (hence +=).

            if (!isReusableEvent(desc->event) ||
                (isReusableEvent(desc->event) && (phases[j].signalsPerPhase == 0 || lastPhaseForMethod == j)))
            {
                numSignals = desc->accumulateSignals ? numSignals + calculateNumSignals(desc->event) : numSignals;
                phases[j].signalsPerPhase = numSignals;
            }
            desc->numSignals = numSignals;
        }
    }
}

void SignalsManager::updateCompletionTracker(uint64_t targetValue)
{
    static const uint32_t                                           mask  = m_cgSize - 1;
    static const SignalsManager::CompletionTracker::CompletionEntry empty = {{{0, 0, 0}, 0}, {}};

    SignalsManager::CompletionTracker& tracker = m_completionTracker[targetValue & mask];
    std::fill(tracker.entries.begin(), tracker.entries.end(), empty);

    for (std::array<WaitPhaseEntry, WAIT_PHASE_MAX>& phases : m_graph->m_waitMethods)
    {
        int phase = 0;
        for (WaitPhaseEntry& entry : phases)
        {
            SignalWaitEvent* desc = entry.waitEvent;
            if (!desc || desc->event == WaitEvent::WAIT_EVENT_MAX)
            {
                break;
            }

            if (phase == 0)
            {
                tracker.entries[(unsigned)desc->method] = {
                    {m_utils->getSOBInfo(getSoAddress(desc->method, desc->longtermIdx)), desc->numSignals},
                    {}};
            }
            else
            {
                tracker.entries[(unsigned)desc->method].value = desc->numSignals;
            }

            for (SignalDescription& signalDesc : desc->signals)
            {
                tracker.entries[(unsigned)desc->method].signals.push_back(
                    {signalDesc.event, calculateNumSignals(signalDesc)});
            }

            phase++;
        }
    }
}

void SignalsManager::printGraph()
{
    if (likely(!LOG_LEVEL_AT_LEAST_DEBUG(HCL)))
    {
        // If the logs are not required, no reason to even run this code.
        return;
    }

    for (size_t i = 0; i < m_graph->m_events.size(); i++)
    {
        SignalWaitEvent& desc = m_graph->m_events[i];
        if (hasWaitEvent(desc.event) && desc.event != WaitEvent::GENERAL_INTERNAL_COMPLETION_EVENT)
        {
            LOG_HCL_CONTEXT_DEBUG(HCL,
                                  "Calculating requirements for event {} (on resource {} ({})): generates {} signals "
                                  "(numSignals={}, currentPhase={}, numExecutedFences={}, numExpectedFences={})",
                                  desc.event,
                                  desc.method,
                                  m_utils->printSOBInfo(getSoAddress(desc.method, desc.longtermIdx)),
                                  calculateNumSignals(desc.event),
                                  desc.numSignals,
                                  desc.currentPhase,
                                  desc.numExecutedFences,
                                  desc.numExpectedFences);
            for (SignalDescription& signalDesc : desc.signals)
            {
                LOG_HCL_TRACE(HCL, "signal {} should add {}", signalDesc.event, calculateNumSignals(signalDesc));
            }
        }
    }
}

void SignalsManager::invalidateCommCache(const HCL_Comm comm)
{
    if (comm < m_cache.size())
    {
        LOG_HCL_INFO(HCL, "deleting comm {} cache, size {}", comm, m_cache[comm].size());
        m_cache[comm].clear();
    }
    else
    {
        LOG_HCL_INFO(HCL, "no cache yet for comm {}, no need to delete", comm);
    }
}

/**
 * @brief Checks if a wait event is registered in the graph.
 *
 * @param waitEvent The wait event to check.
 * @return true if the wait event is registered, false otherwise.
 */
bool SignalsManager::hasWaitEvent(WaitEvent waitEvent) const
{
    return waitEvent != WaitEvent::WAIT_EVENT_MAX && (m_graph->m_events[(unsigned)waitEvent].signals.size() != 0 ||
                                                      isLongTerm(m_graph->m_events[(unsigned)waitEvent].method));
}

bool SignalsManager::isNextReusable(WaitMethod method, int phase, Graph* graph) const
{
    SignalWaitEvent* desc = graph->m_waitMethods[(int)method][phase].waitEvent;
    return isReusableEvent(desc->event) && phase < (int)WAIT_PHASE_MAX - 1 &&
           desc == graph->m_waitMethods[(int)method][phase + 1].waitEvent;
}

/**
 * @brief Calculates the number of signals for a given signal description.
 *
 * This function calculates the number of signals required for a given signal description
 * by using the signal event to cost mapping from the common state.
 *
 * @param desc The signal description for which the number of signals is to be calculated.
 * @return The number of signals required for the given signal description.
 */
unsigned SignalsManager::calculateNumSignals(const SignalDescription& desc) const
{
    return m_commonState->signalToCost(desc.event);
}

/**
 * @brief Calculates the total number of signals associated with a given wait event.
 *
 * @param waitEvent The wait event for which the number of signals is to be calculated.
 * @return The total number of signals associated with the specified wait event.
 */
unsigned SignalsManager::calculateNumSignals(WaitEvent waitEvent) const
{
    unsigned numSignals = 0;

    if (m_graph->m_events[(unsigned)waitEvent].event == WaitEvent::WAIT_EVENT_MAX) return 0;

    for (const SignalDescription& desc : m_graph->m_events[(unsigned)waitEvent].signals)
    {
        numSignals += calculateNumSignals(desc);
    }

    return numSignals;
}

/**
 * @brief Retrieves the total number of signals required for the completion of external CG
 * synchronization.
 *
 * @return The total number of signals required for the completion of external collective group synchronization.
 */
unsigned SignalsManager::getNumSignalsForCompletion() const
{
    return getNumSignals(WaitMethod::EXTERNAL_CG_SO);
}

/**
 * @brief Retrieves the total number of signals required for the completion of internal collective group
 * synchronization.
 *
 * @return The total number of signals required for the completion of internal collective group synchronization.
 */
unsigned SignalsManager::getNumSignalsForInternal() const
{
    return calculateNumSignals(WaitEvent::GENERAL_INTERNAL_COMPLETION_EVENT);
}

/**
 * @brief
 *
 * This function iterates through all the events in the graph and sums up the number of signals for the events that
 * match the specified wait method.
 *
 * @param waitMethod The wait method for which the number of signals is to be calculated.
 * @return The total number of signals associated with the specified wait method.
 */
unsigned SignalsManager::getNumSignals(WaitMethod waitMethod) const
{
    unsigned numSignals = 0;

    for (size_t i = 0; i < m_graph->m_events.size(); i++)
    {
        const SignalWaitEvent& desc = m_graph->m_events[i];
        if (hasWaitEvent(desc.event) && desc.method == waitMethod)
        {
            numSignals += desc.numSignals;
        }
    }

    return numSignals;
}

/**
 * @brief Retrieves the SyncObjectDescriptor for a given wait event.
 *
 * @param waitEvent The wait event for which the SyncObjectDescriptor is to be retrieved.
 * @return The SyncObjectDescriptor for the specified wait event.
 */
SyncObjectDescriptor SignalsManager::getSobDesc(WaitEvent waitEvent)
{
    SignalWaitEvent& desc = m_graph->m_events[(unsigned)waitEvent];
    if (desc.event == WaitEvent::WAIT_EVENT_MAX)
    {
        return {{0, 0, 0}, 0};
    }
    else
    {
        unsigned longtermToWait = desc.longtermIdx;
        unsigned waitSignals =
            m_graph->m_waitMethods[(unsigned)desc.method][(unsigned)desc.currentPhase].signalsPerPhase;
        desc.currentPhase += isReusableEvent(waitEvent);
        return {m_utils->getSOBInfo(getSoAddress(desc.method, longtermToWait)), waitSignals};
    }
}

uint32_t SignalsManager::dequeueSoAddress(SignalEvent signalEvent)
{
    // Lookup the SignalDescription (which has a pointer to a SignalWaitEvent instance) that matches this signalEvent.
    // An issue arises where there are more than one of the same event, for example, eHCLAllReduce has 2 SCALEUP_SEND
    // and 2 SCALEUP_RECV, but some may signal to different resources (RS's SCALEUP_RECV should signal to a GPSO, which
    // the AG's SCALEUP_SEND is blocked on).
    // We chose to handle this as a queue - the first instance that is not yet consumed will be returned (so an instance
    // cannot be returned more than once). This implies that enqueueWait() needs to be invoked in the order of
    // consumption.
    SignalDescription* signalDesc = nullptr;
    for (SignalDescription* desc : m_graph->m_signals[(unsigned)signalEvent])
    {
        if (!desc->consumed)
        {
            signalDesc = desc;
            break;
        }
    }

    VERIFY(signalDesc && signalDesc->wasRegistered(),
           "Uninitialized SignalEvent used: {}. Did you forget to register this event? 0x{:x} 0x{:x} {}",
           signalEvent,
           (uint64_t)m_graph,
           (uint64_t)signalDesc,
           signalDesc != nullptr ? signalDesc->wasRegistered() : false);

    signalDesc->consumed = true;

    // Use HclGraphSync to retrieve the address of the WaitMethod associated with the instance we found.
    uint32_t addr = getSoAddress(signalDesc->signalWaitDesc->method, signalDesc->signalWaitDesc->longtermIdx);
    LOG_HCL_TRACE(HCL, "returning ({}) for signal {}", m_utils->printSOBInfo(addr), signalEvent);
    return addr;
}

uint32_t SignalsManager::getSoAddress(WaitMethod waitMethod, unsigned longtermIdx) const
{
    switch (waitMethod)
    {
        case WaitMethod::GPSO_LONGTERM:
        case WaitMethod::GPSO_LONGTERM_1:
        case WaitMethod::GPSO_LONGTERM_2:
        case WaitMethod::GPSO_LONGTERM_3:
        case WaitMethod::GPSO_LONGTERM_4:
        case WaitMethod::GPSO_LONGTERM_5:
        case WaitMethod::GPSO_LONGTERM_6:
        case WaitMethod::GPSO_LONGTERM_7:
        case WaitMethod::GPSO_LONGTERM_8:
            return m_graphSync.getCurrentLongtermSoAddr(longtermIdx);
        case WaitMethod::GPSO_0:
        case WaitMethod::GPSO_1:  // FALLTHROUGH
            return m_graphSync.getCurrentGpsoAddr(waitMethod);
        case WaitMethod::EXTERNAL_CG_SO:
            return m_graphSync.getCurrentCgSoAddr(CgType::eExternal);
        case WaitMethod::INTERNAL_CG_SO:
            return m_graphSync.getCurrentCgSoAddr(CgType::eInternal);
        default:
            VERIFY(false);
            return -1;
    }
}

void SignalsManager::markMethodForCleanup(WaitMethod waitMethod)
{
    LOG_HCL_TRACE(HCL, "marking method {} for deletion", waitMethod);
    m_graph->m_waitMethodsToClean[(unsigned)waitMethod] = true;
}

/**
 * @brief Retrieves the array of methods that need to be cleaned.
 *
 * @return A constant reference to the array of methods to be cleaned.
 */
const std::array<bool, (unsigned)WaitMethod::WAIT_METHOD_MAX>& SignalsManager::getWaitMethodsToClean() const
{
    return m_graph->m_waitMethodsToClean;
}

void SignalsManager::DFA(uint64_t deviceTargetValue)
{
    // if the value is X, then it's stuck on value X+1
    SignalsManager::CompletionTracker& tracker = m_completionTracker[(deviceTargetValue + 1) & (m_cgSize - 1)];
    for (unsigned i = 0; i < tracker.entries.size(); i++)
    {
        SignalsManager::CompletionTracker::CompletionEntry& entry = tracker.entries[i];
        if (entry.sob.dcore == 0) continue;

        uint64_t addr = m_utils->calculateSoAddressFromIdxAndSM(entry.sob.dcore, entry.sob.sobId);
        uint32_t val  = 0;
        int rc = hlthunk_device_memory_read_block_experimental(hccl_device()->getFd(), &val, addr, sizeof(uint32_t), 0);
        if (rc != 0)
        {
            LOG_HCL_CRITICAL(HCL,
                             "failed to read SOB value from fd {} 0x{:x} with rc {} errno {} {}. NOTE: calculation "
                             "below are using val of 0",
                             hccl_device()->getFd(),
                             addr,
                             rc,
                             errno,
                             strerror(errno));
        }

        WaitMethod waitMethod = (WaitMethod)i;
        switch (waitMethod)
        {
            case WaitMethod::EXTERNAL_CG_SO:
                LOG_HCL_CRITICAL(HCL,
                                 "expecting resource {} ({}) to reach value CMAX (0x{:x}) by incrementing {} signals. "
                                 "current value: 0x{:x} -- missing {} signals:",
                                 waitMethod,
                                 m_utils->printSOBInfo(entry.sob),
                                 m_utils->getCMaxTargetValue(),
                                 entry.value,
                                 val,
                                 m_utils->getCMaxTargetValue() - val);
                break;
            case WaitMethod::GPSO_0:  // FALLTHROUGH
            case WaitMethod::GPSO_1:  // FALLTHROUGH
            case WaitMethod::GPSO_LONGTERM:
            case WaitMethod::GPSO_LONGTERM_1:
            case WaitMethod::GPSO_LONGTERM_2:
            case WaitMethod::GPSO_LONGTERM_3:
            case WaitMethod::GPSO_LONGTERM_4:
            case WaitMethod::GPSO_LONGTERM_5:
            case WaitMethod::GPSO_LONGTERM_6:
            case WaitMethod::GPSO_LONGTERM_7:
            case WaitMethod::GPSO_LONGTERM_8:
            case WaitMethod::INTERNAL_CG_SO:

                LOG_HCL_CRITICAL(
                    HCL,
                    "expecting resource {} ({}) to reach value {}. current value: {} -- missing {} signals:",
                    waitMethod,
                    m_utils->printSOBInfo(entry.sob),
                    entry.value,
                    val,
                    entry.value - val);
                break;
            default:
                LOG_HCL_CRITICAL(HCL, "unexpected waitMethod: {}", waitMethod);
                break;
        }

        LOG_CONTEXT_INIT(HCL);  // manually open log context to indent by 4
        for (CompletionTracker::CompletionEntry::Signal& signal : entry.signals)
        {
            LOG_HCL_CRITICAL(HCL, "signal {} (signaling {} times)", signal.event, signal.numSignals);
        }
    }
}

/**
 * @brief Retrieves the last phase of a given wait method.
 *
 * @param waitMethod The wait method for which the last phase is to be retrieved.
 * @param ignoreSignals A flag indicating whether to ignore the number of signals when determining the last phase.
 * @return The last phase of the specified wait method.
 */
WaitPhase SignalsManager::getLastPhase(WaitMethod waitMethod, bool ignoreSignals) const
{
    WaitPhase ret = WAIT_PHASE_MAX;

    const std::array<WaitPhaseEntry, WAIT_PHASE_MAX>& phases = m_graph->m_waitMethods[(unsigned)waitMethod];
    for (unsigned i = 0; i < m_graph->m_maxPhases && phases[i].waitEvent != nullptr; i++)
    {
        if (phases[i].waitEvent->numSignals > 0 || ignoreSignals)
        {
            ret = i;
        }
    }

    return ret;
}

/**
 * @brief Retrieves the next wait phase based on the provided wait method.
 *
 * @param waitMethod The method used to determine the wait phase.
 * @return The next wait phase.
 */
WaitPhase SignalsManager::getNextPhase(WaitMethod waitMethod) const
{
    WaitPhase lastPhase = getLastPhase(waitMethod, true);
    return lastPhase == WAIT_PHASE_MAX ? 0 : lastPhase + 1;
}
