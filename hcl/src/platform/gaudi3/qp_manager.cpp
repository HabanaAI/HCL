#include "qp_manager.h"

#include <ext/alloc_traits.h>                 // for __alloc_traits<>::value...
#include <algorithm>                          // for max
#include <cstdint>                            // for uint32_t, uint8_t

#include "hcl_utils.h"                        // for VERIFY
#include "platform/gaudi3/hal.h"              // for Gaudi3Hal
#include "platform/gen2_arch_common/types.h"  // for QpInfo
#include "platform/gaudi3/hcl_device.h"       // for HclDeviceGaudi3
#include "platform/gaudi3/commands/hcl_commands.h"
#include "hcl_math_utils.h"

inline G3QP_e getQpIndex(HCL_CollectiveOp collectiveOp, bool isSend)
{
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            return isSend ? QPE_RS_SEND : QPE_RS_RECV;
            break;
        case eHCLAllGather:
            return isSend ? QPE_AG_SEND : QPE_AG_RECV;
            break;
        case eHCLAll2All:
            return isSend ? QPE_A2A_SEND : QPE_A2A_RECV;
            break;
        default:
            VERIFY(false, "invalid op({})", collectiveOp);
    }

    VERIFY(false, "unreachable code");
    return (G3QP_e)0;
}

QPManagerGaudi3::QPManagerGaudi3(HclDeviceGaudi3* device) : m_device(device) {}

/* scale up */

QPUsage QPManagerGaudi3::getBaseQpAndUsage(HclDynamicCommunicator& dynamicComm,
                                           HCL_CollectiveOp        collectiveOp,
                                           bool                    isSend,
                                           bool                    isComplexCollective,
                                           bool                    isReductionInIMB,
                                           bool                    isHierarchical,
                                           uint64_t                count,
                                           uint64_t                cellCount,
                                           HclConfigType           boxType,
                                           bool                    isScaleOut,
                                           HCL_Rank                remoteRank,
                                           uint8_t                 qpSet,
                                           const bool              isReproReduction,
                                           HCL_CollectiveOp        complexCollective,
                                           bool                    isRoot)
{
    QPUsage ret = {0, false};

    G3QP_e qpIndex;
    bool outOfBounds = count != INVALID_COUNT &&
                       ((cellCount * mod(dynamicComm.getMyRank(), dynamicComm.getScaleupGroupSize())) >= count);
    switch (collectiveOp)
    {
        case eHCLReduceScatter:
            if (isSend)
            {
                qpIndex = QPE_RS_SEND;
            }
            else if (isComplexCollective && !isReductionInIMB && (!isHierarchical || outOfBounds))
            {
                if (complexCollective == eHCLReduce && !isRoot && !outOfBounds)
                {
                    ret.disregardRank = true;
                }
                qpIndex = QPE_RS_RECV;
            }
            else if ((isComplexCollective && isReductionInIMB && outOfBounds) || isReproReduction)
            {
                qpIndex = QPE_RS_RECV;
            }
            else if (complexCollective == eHCLReduce && isRoot && !isReductionInIMB && isHierarchical)
            {
                qpIndex = QPE_RS_RECV;
            }
            else
            {
                qpIndex           = QPE_RS_RECV;
                ret.disregardRank = true;
            }
            break;
        case eHCLGather: // FALLTHROUGH
        case eHCLAllGather:
            if (isSend)
            {
                qpIndex = QPE_AG_SEND;
                if (!isComplexCollective || collectiveOp == eHCLGather)
                {
                    ret.disregardRank = true;
                }
            }
            else
            {
                qpIndex = QPE_AG_RECV;
            }
            break;
        case eHCLAll2All:
            if (isScaleOut)
            {
                if (isSend)
                {
                    qpIndex = QPE_RS_SEND;
                }
                else
                {
                    qpIndex = QPE_RS_RECV;
                }
            }
            else
            {
                if (isSend)
                {
                    qpIndex = QPE_A2A_SEND;
                }
                else
                {
                    qpIndex = QPE_A2A_RECV;
                }
            }
            break;
        case eHCLReduce:
        case eHCLScatter:
            if (boxType == LOOPBACK) ret.disregardRank = true;
            if (isSend)
            {
                qpIndex = QPE_RS_SEND;
            }
            else
            {
                qpIndex           = QPE_RS_RECV;
                ret.disregardRank = true;
            }
            break;
        case eHCLBroadcast:  // FALLTHROUGH
        case eHCLSinglePeerBroadcast:  // FALLTHROUGH
        case eHCLSimpleBroadcast:
            if (isSend)
            {
                qpIndex = QPE_AG_SEND;
            }
            else
            {
                qpIndex = QPE_AG_RECV;
            }
            ret.disregardRank = true;
            break;
        case eHCLNoCollective:  // send recv
            if (isSend)
            {
                qpIndex = QPE_RS_SEND;
            }
            else
            {
                qpIndex = QPE_RS_RECV;
            }
            ret.disregardRank = true;
            break;
        default:
            VERIFY(false, "Cannot run collectiveOp {} on Gaudi3 device", (int)collectiveOp);
    }

    ret.qpn = getQP(dynamicComm, qpIndex, remoteRank, qpSet);

    // we use offset 0 for all collective in scaleOut
    if (isScaleOut) ret.disregardRank = true;

    return ret;
}

QPManagerScaleUpGaudi3::QPManagerScaleUpGaudi3(HclDeviceGaudi3* device) : QPManagerGaudi3(device)
{
    m_remoteRankOffsets.resize(DEFAULT_COMMUNICATORS_SIZE);
    m_myRankOffsets.resize(DEFAULT_COMMUNICATORS_SIZE);

    for (auto& commRemoteRankOffsets : m_remoteRankOffsets)
    {
        commRemoteRankOffsets.fill((uint16_t)-1);
    }
    for (auto& commmMyRankOffsets : m_myRankOffsets)
    {
        commmMyRankOffsets.fill((uint16_t)-1);
    }

    m_qpInfoScaleUp.resize(DEFAULT_COMMUNICATORS_SIZE);
    for (auto& qpi : m_qpInfoScaleUp)
    {
        qpi.fill(INVALID_QP);
    }
}

void QPManagerScaleUpGaudi3::resizeDB(HCL_Comm comm)
{
    size_t oldSize = m_qpInfoScaleUp.size();
    size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    m_qpInfoScaleUp.resize(newSize);
    for (unsigned index = oldSize; index < newSize; index++)
    {
        for (auto& qpn : m_qpInfoScaleUp.at(index))
        {
            qpn = INVALID_QP;
        }
    }

    LOG_HCL_INFO(HCL, "resizing m_qpInfoScaleUp for comm {} from {} to {}", comm, oldSize, newSize);
}

void QPManagerScaleUpGaudi3::registerQPs(HCL_Comm         comm,
                                         const QpsVector& qps,
                                         const HCL_Rank   remoteRank,
                                         unsigned         qpSets)
{
    VERIFY(MAX_QPS_PER_CONNECTION_G3 == m_device->getHal()->getMaxQPsPerNic());
    VERIFY(qps.size() == m_device->getHal()->getMaxQPsPerNic(),
           "Each connection should hold {} QPs but opened {} QPs for comm {}",
           m_device->getHal()->getMaxQPsPerNic(),
           qps.size(),
           comm);

    if (unlikely(comm >= m_qpInfoScaleUp.size()))
    {
        resizeDB(comm);
    }

    for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
    {
        m_qpInfoScaleUp.at(comm).at(qpi) = qps[qpi];

        LOG_HCL_DEBUG(HCL, "m_qpInfoScaleUp[comm {}][qpi {}] = qpn {}", comm, qpi, m_qpInfoScaleUp.at(comm).at(qpi));
    }
}

uint32_t
QPManagerScaleUpGaudi3::getQP(HCL_Comm comm, const unsigned qpi, const HCL_Rank remoteRank, const uint8_t qpSet)
{
    return m_qpInfoScaleUp.at(comm).at(qpi);
}

uint32_t QPManagerScaleUpGaudi3::getQPi(HCL_Comm comm, const uint8_t nic, const uint32_t qpn, const HCL_Rank remoteRank)
{
    for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
    {
        if (m_qpInfoScaleUp.at(comm).at(qpi) + m_device->getNicToQpOffset(nic) == qpn)
        {
            return qpi;
        }
    }

    VERIFY(false, "could not find a match for comm {} qpn {}", comm, qpn);
}

uint32_t QPManagerScaleUpGaudi3::getLastRankPortMask(HclDynamicCommunicator&  dynamicComm,
                                                     HCL_CollectiveOp         collectiveOp,
                                                     bool                     isSend,
                                                     Gaudi3DevicePortMapping& portMapping)
{
    if ((collectiveOp == eHCLAllGather && isSend) || (collectiveOp == eHCLReduceScatter && !isSend))
    {
        return portMapping.getInnerRanksPortMask(dynamicComm);
    }
    return 0;
}

void QPManagerScaleUpGaudi3::setNicOffsets(hcl::ScalStream& Stream,
                                           HclDeviceGaudi3* device,
                                           HCL_Comm         comm,
                                           HCL_CollectiveOp collectiveOp,
                                           bool             isSend)
{
    Gaudi3DevicePortMapping& portMapping = device->getPortMappingGaudi3();
    HclDynamicCommunicator&  dynamicComm = device->getComm(comm);

    // for each scenario all nics use the same qpn
    const uint32_t qpn = getQP(dynamicComm, getQpIndex(collectiveOp, isSend));
    LOG_HCL_TRACE(HCL, "comm={}, collectiveOp={}, qpn={}, isSend={}", comm, collectiveOp, qpn, isSend);

    // get nic to remote rank index map
    std::array<uint16_t, MAX_NICS_GEN2ARCH>& remoteIndices = getRemoteRankIndices(dynamicComm,
                                                                                  collectiveOp,
                                                                                  isSend,
                                                                                  portMapping,
                                                                                  device->getNicsStatusMask(),
                                                                                  device->getHal()->getMaxNics());

    // add the command to the cyclic buffer
    HclCommandsGaudi3& commands = ((HclCommandsGaudi3&)(device->getGen2ArchCommands()));
    commands.serializeUpdateNicOffsets(Stream, isSend, true, qpn, remoteIndices);
}

void QPManagerScaleUpGaudi3::resizeOffsetDBs(HCL_Comm comm)
{
    VERIFY(m_remoteRankOffsets.size() == m_myRankOffsets.size(), "Offsets DBs must be equal");
    size_t old_size = m_remoteRankOffsets.size();
    LOG_HCL_INFO(HCL, "Resizing m_remoteRankOffsets and m_myRankOffsets for new comm({})", comm);

    m_remoteRankOffsets.resize(old_size + DEFAULT_COMMUNICATORS_SIZE);
    m_myRankOffsets.resize(old_size + DEFAULT_COMMUNICATORS_SIZE);
    for (size_t i = old_size; i < m_remoteRankOffsets.size(); i++)
    {
        m_remoteRankOffsets[i].fill((uint16_t)-1);
        m_myRankOffsets[i].fill((uint16_t)-1);
    }
}

std::array<uint16_t, MAX_NICS_GEN2ARCH>&
QPManagerScaleUpGaudi3::getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
                                             HCL_CollectiveOp         collectiveOp,
                                             bool                     isSend,
                                             Gaudi3DevicePortMapping& portMapping,
                                             uint64_t                 nicsStatusMask,
                                             const uint64_t           maxNics)
{
    LOG_HCL_DEBUG(HCL,
                  "collectiveOp={}, isSend={}, nicsStatusMask={:024b}, maxNics={}",
                  collectiveOp,
                  isSend,
                  nicsStatusMask,
                  maxNics);

    const HCL_Comm comm = dynamicComm;
    // resize if needed
    if (comm >= m_remoteRankOffsets.size())
    {
        resizeOffsetDBs(comm);
    }

    // this is an array of offsets for the nics, please note that all offsets can be set later to zero
    // if the disregard rank bit is set to true in the collectiveOp command
    std::array<uint16_t, MAX_NICS_GEN2ARCH>& remoteRankOffsets = m_remoteRankOffsets[comm];

    bool needsRemoteRankIndex = (collectiveOp == eHCLAll2All ||
                                 ((collectiveOp == eHCLAllGather && !isSend) || (collectiveOp == eHCLReduceScatter)));

    if (needsRemoteRankIndex)
    {
        // Loop through all the nics
        for (uint16_t nicIndex = 0; nicIndex < maxNics; nicIndex++)
        {
            // If a nic is not active we do not need to configure it
            if ((nicsStatusMask & (1 << nicIndex)) == 0)
            {
                remoteRankOffsets[nicIndex] = 0;
                continue;
            }
            // Find the rank associated with this nic
            for (HCL_Rank rank : dynamicComm.getInnerRanksInclusive())
            {
                // For each nic, we want to find the rank that it goes out to
                if ((unsigned)portMapping.getRemoteDevice(nicIndex, dynamicComm.getSpotlightType()) ==
                    dynamicComm.m_remoteDevices[rank]->header.hwModuleID)
                {
                    remoteRankOffsets[nicIndex] =
                        mod(rank, dynamicComm.getScaleupGroupSize()) -
                        (((collectiveOp == eHCLReduceScatter) && !isSend && (rank > dynamicComm.getMyRank())) ? 1 : 0);
                    break;
                }
            }
        }
        return remoteRankOffsets;
    }

    // Loop through all the nics
    std::array<uint16_t, MAX_NICS_GEN2ARCH>& myRankOffsets = m_myRankOffsets[comm];
    for (uint16_t nicIndex = 0; nicIndex < maxNics; nicIndex++)
    {
        // If a nic is not acive we do not need to configure it
        if ((nicsStatusMask & (1 << nicIndex)) == 0)
        {
            myRankOffsets[nicIndex] = 0;
            continue;
        }
        myRankOffsets[nicIndex] = mod(dynamicComm.getMyRank(), dynamicComm.getScaleupGroupSize());
    }
    return myRankOffsets;
}

void QPManagerScaleUpGaudi3::setLastRankScaleup(hcl::ScalStream& Stream,
                                                HclDeviceGaudi3* device,
                                                HCL_Comm         comm,
                                                HCL_CollectiveOp collectiveOp,
                                                bool             isSend)
{
    Gaudi3DevicePortMapping& portMapping = device->getPortMappingGaudi3();
    HclDynamicCommunicator& dynamicComm = device->getComm(comm);

    // for each scenario all nics use the same qpn
    uint32_t qpn = getQP(comm, getQpIndex(collectiveOp, isSend));

    // we need to set the port mask to 1 for port that go out to the last rank
    uint32_t portsMask = 0;

    // get the last rank in scale up
    int lastRank = dynamicComm.getScaleUpLastRank();

    if (lastRank != dynamicComm.getMyRank())
    {
        if (!(collectiveOp == eHCLAllGather && isSend))
        {
            // loop through all the nics
            for (uint16_t nicIndex = 0; nicIndex < device->getHal()->getMaxNics(); nicIndex++)
            {
                // we want to find the nics that go out to the last rank
                if ((unsigned)portMapping.getRemoteDevice(nicIndex, dynamicComm.getSpotlightType()) ==
                    dynamicComm.m_remoteDevices[lastRank]->header.hwModuleID)
                {
                    portsMask |= (1 << nicIndex);
                }
            }
        }
    }
    else
    {
        portsMask = getLastRankPortMask(dynamicComm, collectiveOp, isSend, portMapping);
    }

    // add the command to the cyclic buffer
    HclCommandsGaudi3& commands = ((HclCommandsGaudi3&)(device->getGen2ArchCommands()));
    commands.serializeUpdateLastRank(Stream, isSend, true, qpn, portsMask);
}

void QPManagerScaleUpGaudi3::closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    for (auto& rank : ranks)
    {
        for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
        {
            for (auto nic : m_device->getActiveNics(m_device->getMyRank(comm), rank, 1, comm))
            {
                if (m_device->getPortMappingGaudi3().isScaleoutPort(nic)) continue;

                uint32_t qpBase = m_qpInfoScaleUp.at(comm).at(qpi);
                if (isInvalidQPn(qpBase)) continue;

                uint32_t qpn = qpBase + m_device->getNicToQpOffset(nic);
                LOG_HCL_TRACE(HCL, "closing QP: comm({}) nic({}) qpi({}) qpn({})", comm, nic, qpi, qpn);

                m_device->destroyQp(nic, qpn);
            }

            m_qpInfoScaleUp.at(comm).at(qpi) = 0;
        }
    }
}

/* ScaleOut QP Manager*/

QPManagerScaleOutGaudi3::QPManagerScaleOutGaudi3(HclDeviceGaudi3* device) : QPManagerGaudi3(device)
{
    m_qpInfoScaleOut.resize(DEFAULT_COMMUNICATORS_SIZE);
    for (auto& rank : m_qpInfoScaleOut)
    {
        for (auto& qpSet : rank)
        {
            for (auto& qpi : qpSet)
            {
                qpi.fill(INVALID_QP);
            }
        }
    }
}

void QPManagerScaleOutGaudi3::resizeDB(HCL_Comm comm)
{
    size_t oldSize = m_qpInfoScaleOut.size();
    size_t newSize = oldSize + DEFAULT_COMMUNICATORS_SIZE;

    m_qpInfoScaleOut.resize(newSize);

    for (unsigned index = oldSize; index < newSize; index++)
    {
        for (auto& qpSet : m_qpInfoScaleOut.at(index))
        {
            for (auto& qpi : qpSet)
            {
                qpi.fill(INVALID_QP);
            }
        }
    }

    LOG_HCL_INFO(HCL, "resizing m_qpInfoScaleOut for comm {} from {} to {}", comm, oldSize, newSize);
}

void QPManagerScaleOutGaudi3::resizeDBForComm(HCL_Comm comm, const size_t commSize)
{
    m_qpInfoScaleOut.at(comm).resize(commSize);

    for (auto& qpSet : m_qpInfoScaleOut.at(comm))
    {
        for (auto& qpi : qpSet)
        {
            qpi.fill(INVALID_QP);
        }
    }

    LOG_HCL_INFO(HCL, "resizing for comm {} to size {}", comm, commSize);
}

void QPManagerScaleOutGaudi3::allocateCommQPs(HCL_Comm comm, const uint32_t commSize)
{
    if (unlikely(comm >= m_qpInfoScaleOut.size()))
    {
        resizeDB(comm);
    }
    if (m_qpInfoScaleOut[comm].size() == 0)
    {
        resizeDBForComm(comm, commSize);
    }
}

void QPManagerScaleOutGaudi3::registerQPs(HCL_Comm         comm,
                                          const QpsVector& qps,
                                          const HCL_Rank   remoteRank,
                                          unsigned         qpSets)
{
    VERIFY(qpSets <= MAX_QPS_SETS_PER_CONNECTION);
    VERIFY(MAX_QPS_PER_CONNECTION_G3 == m_device->getHal()->getMaxQPsPerNic());
    VERIFY(qps.size() == m_device->getHal()->getMaxQPsPerNic() * qpSets,
           "Each connection should hold {} QPs but opened {} QPs for comm {}",
           m_device->getHal()->getMaxQPsPerNic() * qpSets,
           qps.size(),
           comm);

    if (unlikely(comm >= m_qpInfoScaleOut.size()))
    {
        resizeDB(comm);
    }
    if (unlikely(m_qpInfoScaleOut.at(comm).size() == 0))
    {
        resizeDBForComm(comm, m_device->getCommSize(comm));
    }

    for (unsigned qpSet = 0; qpSet < qpSets; qpSet++)
    {
        for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
        {
            unsigned qpIndex = m_device->getHal()->getMaxQPsPerNic() * qpSet + qpi;
            uint32_t qpn     = qpIndex < qps.size() ? qps[qpIndex] : INVALID_QP;

            m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi) = qpn;

            LOG_HCL_DEBUG(HCL,
                          "m_qpInfoScaleOut[comm {}][rank {}][qpSet {}][qpi {}] = qpn {}",
                          comm,
                          remoteRank,
                          qpSet,
                          qpi,
                          m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi));
        }
    }
}

uint32_t
QPManagerScaleOutGaudi3::getQP(HCL_Comm comm, const unsigned qpi, const HCL_Rank remoteRank, const uint8_t qpSet)
{
    return m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi);
}

uint32_t
QPManagerScaleOutGaudi3::getQPi(HCL_Comm comm, const uint8_t nic, const uint32_t qpn, const HCL_Rank remoteRank)
{
    for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
    {
        for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
        {
            if (m_qpInfoScaleOut.at(comm).at(remoteRank).at(qpSet).at(qpi) + m_device->getNicToQpOffset(nic) == qpn)
            {
                return qpi;
            }
        }
    }

    VERIFY(false, "could not find a match for comm {} rank {} nic {} qpn {}", comm, remoteRank, nic, qpn);
    return 0;
}

void QPManagerScaleOutGaudi3::closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    const nics_mask_t myScaleOutPorts = m_device->getPortMappingGaudi3().getScaleOutPorts();
    for (auto& rank : ranks)
    {
        for (unsigned qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
        {
            for (unsigned qpi = 0; qpi < MAX_QPS_PER_CONNECTION_G3; qpi++)
            {
                for (auto nic : myScaleOutPorts)
                {
                    const uint32_t qpBase = m_qpInfoScaleOut.at(comm).at(rank).at(qpSet).at(qpi);
                    if (isInvalidQPn(qpBase)) continue;

                    const uint32_t qpn = qpBase + m_device->getNicToQpOffset(nic);
                    LOG_HCL_TRACE(HCL,
                                  "closing QP: comm({}) rank({}) nic({}) qpSet({}) qpi({}) qpn({})",
                                  comm,
                                  rank,
                                  nic,
                                  qpSet,
                                  qpi,
                                  qpn);

                    m_device->destroyQp(nic, qpn);
                }

                m_qpInfoScaleOut.at(comm).at(rank).at(qpSet).at(qpi) = 0;
            }
        }
    }
}
