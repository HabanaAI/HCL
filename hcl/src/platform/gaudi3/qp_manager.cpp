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

QPManager::QPManager(HclDeviceGaudi3* device) : m_device(device) {}

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

void QPManager::registerQPs(HCL_Comm comm, const QpsVector& qp_vec, HCL_Rank remoteRank, uint8_t qpSets)
{
    if (comm >= getDBSize())
    {
        LOG_HCL_DEBUG(HCL, "Resizing m_qpInfoDb for new comm({})", comm);
        resizeDb();
    }

    for (uint8_t qpSet = 0; qpSet < qpSets; qpSet++)
    {
        uint8_t qpSetBase = m_device->getHal()->getMaxQPsPerNic() * qpSet;

        // RS, recv, my rank indexed
        *getQpInfo(comm, eHCLReduceScatter, false, remoteRank, qpSet) =
            QpInfo(qp_vec[qpSetBase + QPE_RS_RECV], QPE_RS_RECV);
        // AG, recv, remote rank indexed
        *getQpInfo(comm, eHCLAllGather, false, remoteRank, qpSet) =
            QpInfo(qp_vec[qpSetBase + QPE_AG_RECV], QPE_AG_RECV);
        // A2A, recv, remote rank indexed
        *getQpInfo(comm, eHCLAll2All, false, remoteRank, qpSet) =
            QpInfo(qp_vec[qpSetBase + QPE_A2A_RECV], QPE_A2A_RECV);
        // RS, send, remote rank indexed
        *getQpInfo(comm, eHCLReduceScatter, true, remoteRank, qpSet) =
            QpInfo(qp_vec[qpSetBase + QPE_RS_SEND], QPE_RS_SEND);
        // AG, send, my rank indexed
        *getQpInfo(comm, eHCLAllGather, true, remoteRank, qpSet) = QpInfo(qp_vec[qpSetBase + QPE_AG_SEND], QPE_AG_SEND);
        // A2A, send, remote rank indexed
        *getQpInfo(comm, eHCLAll2All, true, remoteRank, qpSet) = QpInfo(qp_vec[qpSetBase + QPE_A2A_SEND], QPE_A2A_SEND);
    }
}

uint32_t QPManager::getBaseQp(HCL_Comm comm, HCL_CollectiveOp collectiveOp, bool isSend, HCL_Rank remoteRank)
{
    return getQpInfo(comm, collectiveOp, isSend, remoteRank)->getQpn();
}

uint32_t QPManager::getQpi(HCL_Comm comm, uint8_t nic, uint32_t qpn, HCL_Rank remoteRank)
{
    QpInfo* qp = seek(comm, nic, qpn, remoteRank);
    return qp->getQpi();
}

QpInfo* QPManager::seek(HCL_Comm comm, uint8_t nic, uint32_t qpn, HCL_Rank remoteRank)
{
    uint32_t idx   = 0;
    uint8_t  qpSet = 0;

    for (qpSet = 0; qpSet < m_device->getComm(comm).getMaxScaleOutQpSetsNum(); qpSet++)
    {
        for (idx = 0; idx < m_device->getHal()->getMaxQPsPerNic(); idx++)
        {
            if (getQpInfo(comm, idx, remoteRank, qpSet)->getQpn() + m_device->getNicToQpOffset(nic) == qpn)
            {
                return getQpInfo(comm, idx, remoteRank, qpSet);
            }
        }
    }

    VERIFY(false, "Couldn't find QpInfo for comm={}, nic={}, qpn={}, remoteRank={}", comm, nic, qpn, remoteRank);

    return nullptr;
}

uint32_t QPManager::getLastRankPortMask(HclDynamicCommunicator&  dynamicComm,
                                        HCL_CollectiveOp         collectiveOp,
                                        bool                     isSend,
                                        Gaudi3DevicePortMapping& portMapping)
{
    if ((collectiveOp == eHCLAllGather && isSend) || (collectiveOp == eHCLReduceScatter && !isSend))
    {
        return isScaleUp() ? portMapping.getInnerRanksPortMask(dynamicComm) : portMapping.getExternalPortsMask();
    }
    return 0;
}

void QPManager::setNicOffsets(hcl::ScalStream& Stream,
                              HclDeviceGaudi3* device,
                              HCL_Comm         comm,
                              HCL_CollectiveOp collectiveOp,
                              bool             isSend)
{
    Gaudi3DevicePortMapping& portMapping = device->getPortMappingGaudi3();
    HclDynamicCommunicator&  dynamicComm = device->getComm(comm);

    // for each scenario all nics use the same qpn
    const uint32_t qpn = getBaseQp(dynamicComm, collectiveOp, isSend);
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
    commands.serializeUpdateNicOffsets(Stream, isSend, isScaleUp(), qpn, remoteIndices);
}

QPUsage QPManager::getBaseQpAndUsage(HclDynamicCommunicator& dynamicComm,
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
    QPUsage ret = {
        0,
    };

    G3QP_e qpIndex;
    bool outOfBounds =
        count != INVALID_COUNT && ((cellCount * mod(dynamicComm.getMyRank(), dynamicComm.getPodSize())) >= count);
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
                    ret.disregardRank = 1;
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
                ret.disregardRank = 1;
            }
            break;
        case eHCLGather: // FALLTHROUGH
        case eHCLAllGather:
            if (isSend)
            {
                qpIndex = QPE_AG_SEND;
                if (!isComplexCollective || collectiveOp == eHCLGather)
                {
                    ret.disregardRank = 1;
                }
            }
            else
            {
                qpIndex = QPE_AG_RECV;
            }
            break;
        case eHCLAll2All:
            if (isSend)
            {
                qpIndex = QPE_A2A_SEND;
            }
            else
            {
                qpIndex = QPE_A2A_RECV;
            }
            break;
        case eHCLReduce:
        case eHCLScatter:
            if (boxType == LOOPBACK) ret.disregardRank = 1;
            if (isSend)
            {
                qpIndex = QPE_RS_SEND;
            }
            else
            {
                qpIndex           = QPE_RS_RECV;
                ret.disregardRank = 1;
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
            ret.disregardRank = 1;
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
            ret.disregardRank = 1;
            break;
        default:
            VERIFY(false, "Cannot run collectiveOp {} on Gaudi3 device", (int)collectiveOp);
    }

    ret.qpn = getQpInfo(dynamicComm, qpIndex, remoteRank, qpSet)->getQpn();

    // we use offset 0 for all collective in scaleOut
    if (isScaleOut) ret.disregardRank = 1;

    return ret;
}

QPManagerScaleUp::QPManagerScaleUp(HclDeviceGaudi3* device) : QPManager(device)
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
    m_qpInfoDb.resize(DEFAULT_COMMUNICATORS_SIZE);
}

bool QPManagerScaleUp::isScaleUp() const
{
    return true;
}

QpInfo* QPManagerScaleUp::getQpInfo(HCL_Comm         comm,
                                    HCL_CollectiveOp collectiveOp,
                                    bool             isSend,
                                    HCL_Rank         remoteRank,
                                    uint8_t          qpSet)
{
    return getQpInfo(comm, getQpIndex(collectiveOp, isSend), remoteRank);
}

QpInfo* QPManagerScaleUp::getQpInfo(HCL_Comm comm, unsigned index, HCL_Rank remoteRank, uint8_t qpSet)
{
    return &(m_qpInfoDb[comm][index]);
}

size_t QPManagerScaleUp::getDBSize() const
{
    return m_qpInfoDb.size();
}

void QPManagerScaleUp::resizeDb()
{
    m_qpInfoDb.resize(m_qpInfoDb.size() + DEFAULT_COMMUNICATORS_SIZE);
}

void QPManagerScaleUp::resizeOffsetsDB(HCL_Comm comm)
{
    VERIFY(m_remoteRankOffsets.size() == m_myRankOffsets.size(), "Offsets DBs must be equal");
    size_t old_size = m_remoteRankOffsets.size();
    LOG_HCL_DEBUG(HCL, "Resizing m_remoteRankOffsets and m_myRankOffsets for new comm({})", comm);
    m_remoteRankOffsets.resize(old_size + DEFAULT_COMMUNICATORS_SIZE);
    m_myRankOffsets.resize(old_size + DEFAULT_COMMUNICATORS_SIZE);
    for (size_t i = old_size; i < m_remoteRankOffsets.size(); i++)
    {
        m_remoteRankOffsets[i].fill((uint16_t)-1);
        m_myRankOffsets[i].fill((uint16_t)-1);
    }
}

std::array<uint16_t, MAX_NICS_GEN2ARCH>& QPManagerScaleUp::getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
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
        resizeOffsetsDB(comm);
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
                        mod(rank, dynamicComm.getPodSize()) -
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
        myRankOffsets[nicIndex] = mod(dynamicComm.getMyRank(), dynamicComm.getPodSize());
    }
    return myRankOffsets;
}

void QPManagerScaleUp::setLastRankScaleup(hcl::ScalStream& Stream,
                                          HclDeviceGaudi3* device,
                                          HCL_Comm         comm,
                                          HCL_CollectiveOp collectiveOp,
                                          bool             isSend)
{
    Gaudi3DevicePortMapping& portMapping = device->getPortMappingGaudi3();
    HclDynamicCommunicator& dynamicComm = device->getComm(comm);

    // for each scenario all nics use the same qpn
    uint32_t qpn = getBaseQp(comm, collectiveOp, isSend);

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

void QPManagerScaleUp::registerQPs(HCL_Comm comm, const QpsVector& qp_vec, HCL_Rank remoteRank)
{
    VERIFY(MAX_QPS_PER_NIC_G3 == m_device->getHal()->getMaxQPsPerNic());
    VERIFY(qp_vec.size() == m_device->getHal()->getMaxQPsPerNic(),
           "Each connection should hold {} QPs but opened {} QPs for comm {}",
           m_device->getHal()->getMaxQPsPerNic(),
           qp_vec.size(),
           comm);

    QPManager::registerQPs(comm, qp_vec, remoteRank, SINGLE_QP_SET);
}

QPManagerScaleOut::QPManagerScaleOut(HclDeviceGaudi3* device) : QPManager(device)
{
    m_qpInfoDb.resize(DEFAULT_COMMUNICATORS_SIZE);
}

bool QPManagerScaleOut::isScaleUp() const
{
    return false;
}

QpInfo* QPManagerScaleOut::getQpInfo(HCL_Comm         comm,
                                     HCL_CollectiveOp collectiveOp,
                                     bool             isSend,
                                     HCL_Rank         remoteRank,
                                     uint8_t          qpSet)
{
    return getQpInfo(comm, getQpIndex(collectiveOp, isSend), remoteRank, qpSet);
}

QpInfo* QPManagerScaleOut::getQpInfo(HCL_Comm comm, unsigned index, HCL_Rank remoteRank, uint8_t qpSet)
{
    return &(this->m_qpInfoDb[comm][remoteRank][qpSet][index]);
}

size_t QPManagerScaleOut::getDBSize() const
{
    return m_qpInfoDb.size();
}

void QPManagerScaleOut::resizeDb()
{
    m_qpInfoDb.resize(m_qpInfoDb.size() + DEFAULT_COMMUNICATORS_SIZE);
}

std::array<uint16_t, MAX_NICS_GEN2ARCH>& QPManagerScaleOut::getRemoteRankIndices(HclDynamicCommunicator&  dynamicComm,
                                                                                 HCL_CollectiveOp         collectiveOp,
                                                                                 bool                     isSend,
                                                                                 Gaudi3DevicePortMapping& portMapping,
                                                                                 uint64_t       nicsStatusMask,
                                                                                 const uint64_t maxNics)
{
    // TODO GAUDI3 SCALEOUT All2All: SW-126608
    // we dont care about most offsets since they should all be 0, except for All2All
    std::unique_ptr<std::array<uint16_t, MAX_NICS_GEN2ARCH>> arr(new std::array<uint16_t, MAX_NICS_GEN2ARCH>);
    std::array<uint16_t, MAX_NICS_GEN2ARCH>&                 arrRef = *arr;
    return arrRef;
}

/**
 * @brief allocate the QP vector buffer for new comm
 */
void QPManagerScaleOut::allocateCommQPs(HCL_Comm comm, uint32_t comm_size)
{
    if (comm >= getDBSize())
    {
        LOG_HCL_DEBUG(HCL, "Resizing m_qpInfoDb for new comm({})", comm);
        resizeDb();
    }
    m_qpInfoDb[comm].resize(comm_size);
}

void QPManagerScaleOut::registerQPs(HCL_Comm comm, const QpsVector& qp_vec, HCL_Rank remoteRank)
{
    VERIFY(MAX_QPS_PER_NIC_G3 == m_device->getHal()->getMaxQPsPerNic());
    uint8_t qpSets = m_device->getNumQpSets(true, comm, remoteRank);
    VERIFY(qp_vec.size() == m_device->getHal()->getMaxQPsPerNic() * qpSets,
           "Each connection should hold {} QPs but opened {} QPs for comm {}",
           m_device->getHal()->getMaxQPsPerNic() * qpSets,
           qp_vec.size(),
           comm);

    QPManager::registerQPs(comm, qp_vec, remoteRank, qpSets);
}
