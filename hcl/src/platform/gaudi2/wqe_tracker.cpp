#include "platform/gaudi2/wqe_tracker.h"
#include "platform/gaudi2/hccl_device.h"
#include "hcl_types.h"
#include "hcl_dynamic_communicator.h"  // for HclDynamicCommunicator
#include "hcl_utils.h"                 // for VERIFY

WqeTrackerGaudi2::WqeTrackerGaudi2(unsigned cgSize) : WqeTracker(cgSize)
{
    VERIFY(m_recvWqeEntriesNum == cgSize >> 1,
           "recv-wqe tracking num set to {}, instead of {}",
           m_recvWqeEntriesNum,
           cgSize >> 1);

    LOG_HCL_DEBUG(HCL, "setting m_recvWqeEntriesNum={}", m_recvWqeEntriesNum);

    // allocate initial data for communicators
    // don't allocate per communicator since it's size is unknown
    for (unsigned qpType = 0; qpType < (unsigned)QpType::QPTypeSize; ++qpType)
    {
        m_wqePerConnection[qpType].resize(DEFAULT_COMMUNICATORS_SIZE);
        m_wqeWraparoundBits[qpType].resize(DEFAULT_COMMUNICATORS_SIZE);
    }
}

void WqeTrackerGaudi2::incWqe(const HCL_Comm commId, const unsigned rank, const QpType qpType)
{
    unsigned qpTypeIdx = (unsigned)qpType;
    VERIFY(qpTypeIdx < (unsigned)QpType::QPTypeSize);
    VERIFY((int)qpType >= (int)QpType::ScaleOutAllGather || rank < DEFAULT_BOX_SIZE);

    if (((++m_wqePerConnection[qpTypeIdx][commId][rank]) & (m_recvWqeEntriesNum - 1)) == 0)
    {
        switch (qpType)
        {
            case QpType::ScaleUpAllGather:
            case QpType::ScaleUpReduceScatter:
                m_wqeWraparoundBits[qpTypeIdx][commId][0].notify_rndv_ack = true;
                break;
            case QpType::ScaleOutAllGather:
            case QpType::ScaleOutReduceScatter:
                m_wqeWraparoundBits[qpTypeIdx][commId][rank].notify_rndv_ack = true;
                break;
            default:
                assert(false);
        }
    }
}

/**
 * @brief allocate data for communicators on commInitRank
 * allocate data for new communicators if needed
 * allocate data for new communicator based on its size
 * @param commId - new communicator
 */
void WqeTrackerGaudi2::resizeDB(const HCL_Comm commId)
{
    if (commId >= m_wqePerConnection[0].size())
    {
        const size_t prevSize = m_wqePerConnection[0].size();
        for (unsigned qpType = 0; qpType < (unsigned)QpType::QPTypeSize; ++qpType)
        {
            VERIFY(prevSize == m_wqePerConnection[qpType].size());
            VERIFY(prevSize == m_wqeWraparoundBits[qpType].size());
            LOG_HCL_DEBUG(HCL,
                          "Resizing m_wqePerConnection/m_wqeWraparoundBits[{}] for new comm({}) from({}) by({})",
                          qpType,
                          commId,
                          prevSize,
                          DEFAULT_COMMUNICATORS_SIZE);
            m_wqePerConnection[qpType].resize(prevSize + DEFAULT_COMMUNICATORS_SIZE);
            m_wqeWraparoundBits[qpType].resize(prevSize + DEFAULT_COMMUNICATORS_SIZE);
        }
    }

    // allocate space for new communicator data
    const int commSize    = hccl_device()->getCommSize(commId);
    const int scaleupSize = hccl_device()->getScaleupGroupSize(commId);
    const int nodes       = commSize / scaleupSize;
    LOG_HCL_DEBUG(HCL,
                  "allocate wqe data for comm({}), size({}), scaleupSize({}), nodes({})",
                  commId,
                  commSize,
                  scaleupSize,
                  nodes);
    for (unsigned qpType = 0; qpType < (unsigned)QpType::QPTypeSize; ++qpType)
    {
        VERIFY(m_wqePerConnection[qpType][commId].size() == 0, "wqe data already allocated qptype({})", qpType);
        VERIFY(m_wqeWraparoundBits[qpType][commId].size() == 0, "wqe data already allocated qptype({})", qpType);
        // resize scale-out QPs to #of boxes
        const unsigned vecSize = (int)qpType < (int)QpType::ScaleOutAllGather ? DEFAULT_BOX_SIZE : nodes;
        m_wqePerConnection[qpType][commId].resize(vecSize, 0);
        m_wqeWraparoundBits[qpType][commId].resize(vecSize, {false, false});
    }
}

WqeWraparoundBits WqeTrackerGaudi2::getWqeWraparoundBits(HCL_Comm commId, unsigned rank, QpType qpType)
{
    unsigned qpTypeIdx = (unsigned)qpType;
    VERIFY(qpTypeIdx < (unsigned)QpType::QPTypeSize);
    VERIFY(commId < m_wqePerConnection[qpTypeIdx].size());
    VERIFY(rank < m_wqePerConnection[qpTypeIdx][commId].size());

    auto bitsToReturn = m_wqeWraparoundBits[qpTypeIdx][commId][rank];

    auto& ptr              = m_wqeWraparoundBits[qpTypeIdx][commId][rank];
    ptr.wait_for_rndv_acks = ptr.notify_rndv_ack;
    ptr.notify_rndv_ack    = false;

    return bitsToReturn;
}
