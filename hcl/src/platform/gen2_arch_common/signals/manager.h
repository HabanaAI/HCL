#pragma once

#include <array>

#include "platform/gen2_arch_common/hcl_graph_sync.h"
#include "platform/gen2_arch_common/signals/calculator.h"
#include "platform/gen2_arch_common/signals/types.h"
#include "platform/gen2_arch_common/types.h"
#include "platform/gen2_arch_common/collective_states.h"
#include "llvm/small_vector.h"

class HclGraphSyncGen2Arch;
class Gen2ArchScalUtils;

class SignalsManager
{
private:
    struct SignalWaitEvent;

public:
    struct SignalDescription
    {
        SignalEvent event;
        bool        consumed;

        SignalWaitEvent* signalWaitDesc;

        SignalDescription();
        SignalDescription(SignalEvent signalEvent, bool startConsumed = false);

        bool wasRegistered() const;
        bool wasSignalled() const;
    };

private:
    struct FenceCheckResult
    {
        bool      isFenced;
        WaitEvent notFencedEvent;
    };

    struct SignalWaitEvent
    {
        WaitEvent                                        event;
        llvm_vecsmall::SmallVector<SignalDescription, 8> signals;
        WaitMethod                                       method;
        WaitPhase                                        currentPhase = 0;
        unsigned                                         longtermIdx  = 0;

        bool     accumulateSignals  = true;
        bool     expectAnotherPhase = false;
        unsigned numSignals;

        unsigned numExecutedFences;
        unsigned numExpectedFences;

        SignalWaitEvent* nextPhaseEvent = nullptr;

        SignalWaitEvent();
        SignalWaitEvent(WaitEvent                                        waitEvent,
                        llvm_vecsmall::SmallVector<SignalDescription, 8> signalDescs,
                        WaitMethod                                       waitMethod,
                        WaitPhase                                        waitPhase,
                        unsigned                                         longtermSyncObjIdx,
                        bool                                             accSignals,
                        bool                                             expectAnotherPhase_);

        SignalWaitEvent& operator=(const SignalWaitEvent& other);
        SignalWaitEvent& operator=(SignalWaitEvent&& other) = default;

        bool             wasSignalled() const;  // returns true if this and all 'nextPhaseEvent' have been signalled
        FenceCheckResult wasFenced(bool checkPhases = true)
            const;  // true if this and all 'nextPhaseEvent' have 'numExecutedFences == numExpectedFences'
        bool wasCompleted() const;  // returns true if wasSignalled() and wasFenced() and not expectAnotherPhase
    };

    struct WaitPhaseEntry
    {
        SignalWaitEvent* waitEvent       = nullptr;
        uint16_t         signalsPerPhase = 0;
    };

public:
    SignalsManager(HclGraphSyncGen2Arch& graphSync, Gen2ArchScalUtils* utils, unsigned cgSize, unsigned archStream);
    virtual ~SignalsManager() = default;

    void initialize(CommonState* commonState, uint64_t cuid);
    void finalize(bool entireCollective = false);
    void finalize(WaitEvent waitEvent);

    void enqueueCompletion(llvm_vecsmall::SmallVector<SignalDescription, 8>&& signalEvents);

    void enqueueWait(WaitEvent                                          waitEvent,
                     llvm_vecsmall::SmallVector<SignalDescription, 8>&& signalEvents,
                     WaitMethod                                         waitMethod,
                     WaitPhase                                          waitPhase          = 0,
                     unsigned                                           numExpectedFences  = 1,
                     unsigned                                           longtermIdx        = 0,
                     bool                                               accSignals         = true,
                     bool                                               expectAnotherPhase = false);

    void enqueueInternalCompletion(SignalEvent signalEvent);

    void allocateResources();
    void updateCompletionTracker(uint64_t targetValue, uint64_t cuid);
    void printGraph();
    bool isGraphLoaded() { return !m_graph->m_firstUse && !m_graph->m_firstCollective; }
    void invalidateCommCache(const HCL_Comm comm);
    void newCollective(const HCL_Comm comm);

    unsigned getNumSignalsForCompletion() const;
    unsigned getNumSignalsForInternal() const;

    uint32_t getSoAddress(WaitMethod waitMethod, unsigned longtermIdx = 0) const;

    SyncObjectDescriptor getSobDesc(WaitEvent waitEvent);

    uint32_t dequeueSoAddress(SignalEvent signalEvent);

    bool isEventRegistered(SignalEvent signalEvent);

    void                                                           markMethodForCleanup(WaitMethod waitMethod);
    const std::array<bool, (unsigned)WaitMethod::WAIT_METHOD_MAX>& getMethodsToClean() const;

    void DFA(uint64_t deviceTargetValue);

    WaitPhase getNextPhase(WaitMethod waitMethod) const;

private:
    struct Graph
    {
        std::array<SignalWaitEvent, (unsigned)WaitEvent::WAIT_EVENT_MAX> m_events;
        std::array<llvm_vecsmall::SmallVector<SignalDescription*, 8>, (unsigned)SignalEvent::SIGNAL_EVENT_MAX>
                                                                                                      m_signals;
        std::array<std::array<WaitPhaseEntry, WAIT_PHASE_MAX>, (unsigned)WaitMethod::WAIT_METHOD_MAX> m_methods {};

        uint32_t m_requestedEventsBitmap = 0;

        std::array<bool, (unsigned)WaitMethod::WAIT_METHOD_MAX> m_methodsToClean {};

        bool m_firstUse        = true;
        bool m_firstCollective = true;

        // max number phases based on communicator size
        uint64_t m_maxPhases = 0;

        Graph();
        Graph(Graph&&)             = delete;
        Graph&  operator=(Graph&)  = delete;
        Graph&& operator=(Graph&&) = delete;
    };

    bool isCachingRequired(CommonState& commonState);
    bool updateGraph(uint64_t cuid, CommonState* commonState);
    void handleLongtermOnGraphSwitch(bool created, Graph* oldGraph);
    void updateEventsOnLongterm(Graph* oldGraph);
    void resetGraph();

    std::vector<std::unordered_map<uint64_t, Graph>> m_cache;  // every vector entry is for a specific comm
    Graph*                                           m_graph = nullptr;
    Graph                                            m_nonCachedGraph;
    bool                                             m_usingCache = false;

    struct CompletionTracker
    {
        struct CompletionEntry : public SyncObjectDescriptor
        {
            struct Signal
            {
                SignalEvent event;
                unsigned    numSignals;
            };
            llvm_vecsmall::SmallVector<Signal, 8> signals;
        };
        uint64_t                                                           cuid;
        std::array<CompletionEntry, (unsigned)WaitMethod::WAIT_METHOD_MAX> entries;
    };
    std::vector<CompletionTracker> m_completionTracker;

    HclGraphSyncGen2Arch& m_graphSync;
    Gen2ArchScalUtils*    m_utils;

    const unsigned m_cgSize;
    unsigned       m_archStream;
    bool           m_allowGraphCaching = false;

    CommonState* m_commonState   = nullptr;
    int          m_prevIteration = -1;

    bool hasWaitEvent(WaitEvent waitEvent) const;
    bool isNextReusable(WaitMethod method, int phase, Graph* graph) const;

    unsigned getNumSignals(WaitMethod waitMethod) const;

    unsigned calculateNumSignals(const SignalDescription& desc) const;
    unsigned calculateNumSignals(WaitEvent waitEvent) const;

    WaitPhase getLastPhase(WaitMethod waitMethod, bool ignoreSignals = false) const;
};

typedef llvm_vecsmall::SmallVector<SignalsManager::SignalDescription, 8> signalEvents_t;
