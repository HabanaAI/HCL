#include "platform/gaudi2/wqe_tracker.h"
#include "hcl_types.h"
#include "hcl_dynamic_communicator.h"  // for HclDynamicCommunicator
#include "hcl_utils.h"                 // for VERIFY

const unsigned DEFAULT_BOX_NUM = 8;

WqeTrackerGaudi2::WqeTrackerGaudi2()
{
    for (unsigned qpType = 0; qpType < (unsigned)QpType::QPTypeSize; ++qpType)
    {
        m_wqePerConnection[qpType].resize(DEFAULT_COMMUNICATORS_SIZE);
        m_wqeWraparoundBits[qpType].resize(DEFAULT_COMMUNICATORS_SIZE);

        unsigned vecSize = qpType < (int) QpType::ScaleOutAllGather ? DEFAULT_BOX_SIZE : DEFAULT_BOX_NUM;
        for (unsigned commIdx = 0; commIdx < DEFAULT_COMMUNICATORS_SIZE; ++commIdx)
        {
            m_wqePerConnection[qpType][commIdx].resize(vecSize, 0);
            m_wqeWraparoundBits[qpType][commIdx].resize(vecSize, {false, false});
        }
    }
}

void WqeTrackerGaudi2::incWqe(const HCL_Comm commId, const unsigned rank, const QpType qpType)
{
    unsigned qpTypeIdx = (unsigned)qpType;
    VERIFY(qpTypeIdx < (unsigned)QpType::QPTypeSize);
    VERIFY((int) qpType >= (int) QpType::ScaleOutAllGather || rank < DEFAULT_BOX_SIZE);

    // TODO: move resize code to CommInitRank
    if (unlikely(commId >= m_wqePerConnection[qpTypeIdx].size()))
    {
        unsigned prevSize = m_wqePerConnection[qpTypeIdx].size();
        VERIFY(prevSize == m_wqeWraparoundBits[qpTypeIdx].size());
        LOG_HCL_DEBUG(HCL,
                      "Resizing m_wqePerConnection/m_wqeWraparoundBits for new comm({}) from({}) by({})",
                      commId,
                      prevSize,
                      DEFAULT_COMMUNICATORS_SIZE);
        m_wqePerConnection[qpTypeIdx].resize(prevSize + DEFAULT_COMMUNICATORS_SIZE);
        m_wqeWraparoundBits[qpTypeIdx].resize(prevSize + DEFAULT_COMMUNICATORS_SIZE);

        // TODO: resize scale-out QPs to #of boxes and not #of ranks?
        unsigned vecSize = (int) qpType < (int) QpType::ScaleOutAllGather ? DEFAULT_BOX_SIZE : DEFAULT_BOX_NUM;
        for (unsigned i = prevSize; i < m_wqePerConnection[qpTypeIdx].size(); ++i)
        {
            m_wqePerConnection[qpTypeIdx][i].resize(vecSize, 0);
            m_wqeWraparoundBits[qpTypeIdx][i].resize(vecSize, {false, false});
        }
    }

    // TODO: merge with above code
    // TODO: resize scale-out QPs to #of boxes and not #of ranks?
    if (unlikely(m_wqePerConnection[qpTypeIdx][commId].size() <= rank))
    {
        unsigned newSize = m_wqePerConnection[qpTypeIdx][commId].size();
        while (newSize <= rank)
        {
            newSize *= 2;
        }
        m_wqePerConnection[qpTypeIdx][commId].resize(newSize, 0);
        m_wqeWraparoundBits[qpTypeIdx][commId].resize(newSize, {false, false});
    }

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
