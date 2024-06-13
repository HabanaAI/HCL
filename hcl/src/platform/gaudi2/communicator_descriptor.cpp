#include "platform/gaudi2/communicator_descriptor.h"

#include <ext/alloc_traits.h>               // for __alloc_traits<>::value_type
#include <algorithm>                        // for max, fill
#include <cstdint>                          // for uint32_t, uint8_t
#include <iterator>                         // for distance
#include <memory>                           // for allocator_traits<>::value...
#include "hcl_utils.h"                      // for VERIFY, UNUSED
#include "platform/gaudi2/hal.h"            // for Gen2ArchHal

static hcl::Gaudi2Hal s_hal;

LRU::LRU() : m_size(0)
{
    m_communicators.resize(DEFAULT_COMMUNICATORS_SIZE);
    std::fill(m_communicators.begin(),
              m_communicators.end(),
              Entry {Entry::INACTIVE, 0, m_lruPosition.end(), HCL_INVALID_COMM});
}

bool LRU::isActive(HCL_Comm comm)
{
    if (comm >= m_communicators.size())
    {
        LOG_HCL_DEBUG(HCL, "called with invalid comm({})", comm);
        return false;
    }

    return m_communicators[comm].state == Entry::ACTIVE;
}

void LRU::resizeDB(HCL_Comm comm)
{
    // resize will invalidate pointers in m_lruPosition, need to restore the list
    VERIFY(m_lruPosition.size() <= g_qpnTableSize, "Too many active hot comms({})", m_lruPosition.size());

    // save "hot" active comms order, first is hottest
    std::vector<HCL_Comm> hotComms;
    for (Entry* entry : m_lruPosition)
    {
        hotComms.push_back(entry->comm);
    }

    size_t old_size = m_communicators.size();
    LOG_HCL_DEBUG(HCL,
                  "resizing m_communicators for new comm({}) from({}) to({})",
                  comm,
                  old_size,
                  old_size + DEFAULT_COMMUNICATORS_SIZE);
    m_communicators.resize(old_size + DEFAULT_COMMUNICATORS_SIZE,
                           Entry {Entry::INACTIVE, 0, m_lruPosition.end(), HCL_INVALID_COMM});

    // restore list
    // empty the list from invalidated pointers
    m_lruPosition.clear();
    for (HCL_Comm hotComm : hotComms)
    {
        // all "hot" comms should be ACTIVE
        VERIFY(m_communicators[hotComm].state == Entry::ACTIVE,
               "LRU error, found INACTIVE entry in cache for comm({})",
               hotComm);
        // insert to end
        m_lruPosition.push_back(&m_communicators[hotComm]);
        // update iterator
        m_communicators[hotComm].lruPosition_it = --m_lruPosition.end();
    }
}

unsigned LRU::use(HCL_Comm comm)
{
    // comm must be allocated already
    VERIFY(comm < m_communicators.size(), "LRU::use comm({}) should be resized on registerQPs", comm);

    Entry& entry = m_communicators[comm];
    if (entry.state != Entry::ACTIVE)
    {
        // Currently the requested entry is not active - make it so.
        if (m_size == g_qpnTableSize)
        {
            // No empty comm-desc entry - evict.
            entry.state           = Entry::ACTIVE;
            entry.comm            = comm;
            entry.comm_desc_index = evict();
        }
        else
        {
            // There's an empty comm-desc - lets use it. First, lets' find the next index to use.
            unsigned nextIndex = m_size;
            entry.state        = Entry::ACTIVE;
            entry.comm            = comm;
            entry.comm_desc_index = nextIndex;
            m_size++;
        }
        m_lruPosition.push_front(&entry);
        entry.lruPosition_it = m_lruPosition.begin();
    }
    else
    {
        // Need to update the score since we found the entry already active.
        if (getPlaceInLRU(entry.lruPosition_it) > 0)
        {
            m_lruPosition.splice(m_lruPosition.begin(), m_lruPosition, entry.lruPosition_it);
            entry.lruPosition_it = m_lruPosition.begin();
        }
    }

    return entry.comm_desc_index;
}

size_t LRU::evict()
{
    Entry* last          = *m_lruPosition.rbegin();
    size_t commDescIndex = last->comm_desc_index;
    // remove last from list as it becomes inactive
    m_lruPosition.pop_back();

    last->state           = Entry::INACTIVE;
    last->comm_desc_index = 0;
    last->lruPosition_it  = m_lruPosition.end();

    return commDescIndex;
}

unsigned LRU::getPlaceInLRU(lruPosition_iterator it)
{
    return std::distance(m_lruPosition.begin(), it);
}

CommunicatorDescriptor::CommunicatorDescriptor(uint8_t collectiveIndex) : m_collectiveContextIndex(collectiveIndex)
{
    UNUSED( m_collectiveContextIndex );
    m_qpsPerNic.resize(DEFAULT_COMMUNICATORS_SIZE);
    m_commDownloaded.resize(DEFAULT_COMMUNICATORS_SIZE, false);
}

void CommunicatorDescriptor::resizeDBs(HCL_Comm comm)
{
    size_t old_size = m_qpsPerNic.size();
    LOG_HCL_INFO(HCL,
                 "Resizing m_qpsPerNic/m_commDownloaded for new comm({}) from({}), to({})",
                 comm,
                 old_size,
                 old_size + DEFAULT_COMMUNICATORS_SIZE);
    VERIFY(m_commDownloaded.size() == old_size,
           "Invalid QP DB sizes detected: m_qpsPerNic({}), m_commDownloaded({})",
           m_qpsPerNic.size(),
           m_commDownloaded.size());
    m_qpsPerNic.resize(old_size + DEFAULT_COMMUNICATORS_SIZE);
    m_commDownloaded.resize(old_size + DEFAULT_COMMUNICATORS_SIZE, false);
}

void CommunicatorDescriptor::registerQPs(HCL_Comm comm, uint8_t nic, unsigned qpi, const QpsVector& qps)
{
    if (unlikely(comm >= m_qpsPerNic.size()))
    {
        resizeDBs(comm);
        m_lru.resizeDB(comm);
    }

    m_qpsPerNic[comm][nic] = ScaleUpQpInfo(qps[qpi], qpi);
}

ScaleUpQpInfo* CommunicatorDescriptor::seek(HCL_Comm comm, uint8_t nic)
{
    if (unlikely(comm >= m_qpsPerNic.size()))
    {
        resizeDBs(comm);
        m_lru.resizeDB(comm);
    }

    return &m_qpsPerNic[comm][nic];
}

std::pair<unsigned, uint32_t> CommunicatorDescriptor::useQP(HCL_Comm comm, uint8_t nic)
{
    ScaleUpQpInfo* qp = seek(comm, nic);
    return std::make_pair(m_lru.use(comm), qp->getQpn());
}

unsigned CommunicatorDescriptor::getCommDescIndex(HCL_Comm comm)
{
    return m_lru.use(comm);
}

bool CommunicatorDescriptor::isActive(HCL_Comm comm, uint8_t nic)
{
    return m_lru.isActive(comm);
}

g2fw::nic_coll_ctxt_dword_t& CommunicatorDescriptor::getRemoteDescriptor(HCL_Comm comm, uint8_t nic)
{
    ScaleUpQpInfo* qp = seek(comm, nic);
    return qp->getRemoteDescriptor();
}

uint32_t CommunicatorDescriptor::getQP(HCL_Comm comm, uint8_t nic)
{
    ScaleUpQpInfo* qp = seek(comm, nic);
    return qp->getQpn();
}

uint32_t CommunicatorDescriptor::getQPi(HCL_Comm comm, uint8_t nic)
{
    ScaleUpQpInfo* qp = seek(comm, nic);
    return qp->getQpi();
}

ScaleOutQPsTracker::ScaleOutQPsTracker()
{
    m_qpInfoDb.resize(DEFAULT_COMMUNICATORS_SIZE);
}

void ScaleOutQPsTracker::allocatCommQPs(HCL_Comm comm, uint32_t commSize)
{
    if (comm >= m_qpInfoDb.size())
    {
        LOG_HCL_DEBUG(HCL, "Resizing m_qpInfoDb for new comm({})", comm);
        m_qpInfoDb.resize(m_qpInfoDb.size() + DEFAULT_COMMUNICATORS_SIZE);
    }

    if (m_qpInfoDb[comm].size() == 0)
    {
        m_qpInfoDb[comm].resize(commSize);
    }
    VERIFY(m_qpInfoDb[comm].size() == commSize,
           "Invalid communicator({}) size({}) detected",
           comm,
           m_qpInfoDb[comm].size());
}

void ScaleOutQPsTracker::registerQPs(HclDynamicCommunicator& dynamicComm,
                                     uint8_t                 SubNicIndex,
                                     HCL_Rank                remoteRank,
                                     unsigned                qpi,
                                     const QpsVector&        qps)
{
    HCL_Comm comm = dynamicComm;

    for (uint8_t qpSet = 0; qpSet < dynamicComm.getMaxScaleOutQpSetsNum(); qpSet++)
    {
        unsigned qpIndex                                 = (s_hal.getMaxQPsPerNic() * qpSet) + qpi;
        m_qpInfoDb[comm][remoteRank][SubNicIndex][qpSet] =
            ScaleOutQpInfo(qpIndex < qps.size() ? qps[qpIndex] : (uint32_t)-1, qpi);
        LOG_HCL_DEBUG(HCL,
                      "qpSet={}, qpIndex={}, m_qpInfoDb[][{}][{}]=({},{})",
                      qpSet,
                      qpIndex,
                      remoteRank,
                      SubNicIndex,
                      m_qpInfoDb[comm][remoteRank][SubNicIndex][qpSet].getQpn(),
                      m_qpInfoDb[comm][remoteRank][SubNicIndex][qpSet].getQpi());
    }
}

uint32_t ScaleOutQPsTracker::getRankQpn(HCL_Comm comm, HCL_Rank remoteRank, uint8_t subNicIndex, uint8_t qpSet)
{
    return this->m_qpInfoDb[comm][remoteRank][subNicIndex][qpSet].getQpn();
}

uint32_t ScaleOutQPsTracker::getRankQpi(HCL_Comm comm, HCL_Rank remoteRank, uint8_t subNicIndex, uint8_t qpSet)
{
    return this->m_qpInfoDb[comm][remoteRank][subNicIndex][qpSet].getQpi();
}
