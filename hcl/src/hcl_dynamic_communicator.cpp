#include "hcl_dynamic_communicator.h"

#include <algorithm>  // for max
#include <array>      // for array
#include <cstdint>    // for uint16_t
#include <set>        // for set
#include <vector>     // for vector
#include <cstddef>    // for size_t
#include <memory>     // for __shared_ptr_access
#include <string>     // for string, basic_st...
#include <set>        // for set

#include "hcl_api_types.h"                        // for HCL_Rank
#include "hccl_types.h"                           // for hcclInternalError
#include "hcl_global_conf.h"                      // for GCFG...
#include "interfaces/hcl_remote_device.h"         // for HclRemoteDevice
#include "hcl_utils.h"                            // for LOG_HCL_INFO, LOG_H...
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "hcl_log_manager.h"                      // for LOG*
#include "platform/gen2_arch_common/types.h"      // for MAX_NICS_GEN2ARCH
#include "hccl/hccl_context.h"                    // for hccl_context
#include "hccl/ofi_communicator.h"                // for ofi_communicator_handle
#include "hcl_sockaddr.h"                         // for address_to_string
#include "interfaces/hcl_hal.h"                   // for HalPtr
#include "hcl_math_utils.h"
#include "hcl_types.h"                             // for HCL_HwModuleId
#include "platform/gen2_arch_common/server_def.h"  // for Gen2ArchServerDef

class IHclDevice;

static constexpr unsigned MAX_SEND_RECV_PEER_COUNTER = 16;

HclDynamicCommunicator::HclDynamicCommunicator(const HCL_Comm comm, Gen2ArchServerDef& serverDef, hcl::HalPtr hal)
: m_commId(comm), m_commConnectivity(comm, serverDef.getServerConnectivity()), m_serverDef(serverDef), m_hal(hal)
{
    m_streamLatestLongSo.resize(m_hal->getMaxStreams());
    m_streamLatestLongSo.assign(m_hal->getMaxStreams(), 0);
}

void HclDynamicCommunicator::setUniqueID(const internal_unique_id_t* internal_unique_id)
{
    if (internal_unique_id != nullptr)
    {
        m_commUniqueId = *internal_unique_id;
    }
    m_commUniqueIdStr = sockaddr_str_t(m_commUniqueId.address);
}

/**
 * @brief initialize dynamic communicator resources
 * allocate memory etc
 *
 * @param hcclCommSize - the hccl communicator size, number of communicator ranks
 * @param hclCommSize - the hcl communicator size, number of communicator ranks
 * @param rank - hccl rank
 * @param box_size - #ranks in a box
 * @param internal_unique_id - hccl unique id
 * @return true on success, false on failure (invalid configuration)
 */
bool HclDynamicCommunicator::init(const uint32_t hcclCommSize, const HCL_Rank rank, const int box_size)
{
    LOG_HCL_DEBUG(HCL,
                  "Init dynamic communicator({}) hccl ({}), rank({}), box({})",
                  m_commId,
                  hcclCommSize,
                  rank,
                  box_size);

    m_commSize = hcclCommSize;

    // allocate maps memory
    m_rankToScaleupGroupMap.resize(hcclCommSize, INVALID_SCALEUP_GROUP);
    m_scaleupGroupToRankMap.resize(hcclCommSize, HCL_INVALID_RANK);
    m_remoteDevices.resize(hcclCommSize);
    m_rankInfo.remoteInfo.resize(hcclCommSize);
    m_rankInfo.header.boxSize = box_size;

    for (size_t i = 0; i < m_remoteDevices.size(); i++)
    {
        m_remoteDevices[i] = std::make_unique<HclRemoteDevice>();
    }

    m_sendCounter.clear();
    m_recvCounter.clear();

    if (GCFG_HCCL_OVER_OFI.value())
    {
        m_maxScaleOutQpSetsNum = (unsigned)m_commSize < GCFG_HCL_HNIC_QP_SETS_COMM_SIZE_THRESHOLD.value()
                                     ? GCFG_HCL_HNIC_SCALE_OUT_QP_SETS.value()
                                     : 1;
    }
    else
    {
        m_maxScaleOutQpSetsNum = (unsigned)m_commSize < GCFG_HCL_GNIC_QP_SETS_COMM_SIZE_THRESHOLD.value()
                                     ? GCFG_HCL_GNIC_SCALE_OUT_QP_SETS.value()
                                     : 1;
    }

    return true;
}

bool HclDynamicCommunicator::isPeer(HCL_Rank rank) const
{
    return getRankInScaleupGroup() == (HCL_Rank)mod(rank, m_scaleupGroupSize);
}

bool HclDynamicCommunicator::arePeers(HCL_Rank rank1, HCL_Rank rank2) const
{
    return mod(rank1, m_scaleupGroupSize) == mod(rank2, m_scaleupGroupSize);
}

bool HclDynamicCommunicator::isRankInsideScaleupGroup(HCL_Rank rank) const
{
    return isRanksInSameScaleupGroup(m_rankInfo.header.hcclRank, m_remoteDevices[rank]->header.hcclRank);
}

bool HclDynamicCommunicator::isPeerOrInsideSameScaleupGroup(HCL_Rank rank)
{
    bool peer     = isPeer(rank);
    bool isInside = isRankInsideScaleupGroup(rank);
    return (isInside || peer);
}

bool HclDynamicCommunicator::isCommunicatorScaleupGroupPeers() const
{
    for (HCL_Rank remote = 0; remote < m_commSize; remote++)
    {
        if (!isPeer(remote))
        {
            return false;
        }
    }
    return true;
}

bool HclDynamicCommunicator::isCommunicatorInScaleupGroup() const
{
    for (HCL_Rank remoteRank = 0; remoteRank < m_commSize; remoteRank++)
    {
        if (!isRankInsideScaleupGroup(remoteRank))
        {
            return false;
        }
    }
    return true;
}

bool HclDynamicCommunicator::isCommunicatorMultiScaleupGroup() const
{
    return m_commSize > m_scaleupGroupSize;
}

bool HclDynamicCommunicator::commScaleupGroupHasMultipleRanks() const
{
    return m_scaleupGroupSize > 1;
}

bool HclDynamicCommunicator::isCommunicatorHierarchical() const
{
    return !isCommunicatorScaleupGroupPeers() && isCommunicatorMultiScaleupGroup();
}

bool HclDynamicCommunicator::isRanksInSameScaleupGroup(HCL_Rank rank1, HCL_Rank rank2) const
{
    return (div((uint32_t)getRemoteConnectionHeader(rank1).hcclRank, (uint32_t)m_scaleupGroupSize) ==
            div((uint32_t)getRemoteConnectionHeader(rank2).hcclRank, (uint32_t)m_scaleupGroupSize));
}

const RankInfoHeader& HclDynamicCommunicator::getRemoteConnectionHeader(HCL_Rank rank) const
{
    return m_remoteDevices[rank]->header;
}

const UniqueSortedVector& HclDynamicCommunicator::getOuterRanksExclusive()
{
    if (m_outerRanksExclusiveCache.size() == 0)
    {
        for (auto& remoteRank : getRemoteRanks())
        {
            if (remoteRank == m_rankInfo.header.hcclRank) continue;
            if (isPeer(remoteRank))
            {
                m_outerRanksExclusiveCache.insert_sorted(remoteRank);
            }
        }
    }
    return m_outerRanksExclusiveCache;
}

const UniqueSortedVector& HclDynamicCommunicator::getOuterRanksInclusive()
{
    m_outerRanksInclusiveCache = getOuterRanksExclusive();
    m_outerRanksInclusiveCache.insert_sorted(m_rankInfo.header.hcclRank);
    return m_outerRanksInclusiveCache;
}

const std::vector<uint32_t>& HclDynamicCommunicator::getRankToScaleupGroupMap()
{
    if (m_rankToScaleupGroupMap[0] == INVALID_SCALEUP_GROUP)
    {
        for (const auto& remoteRank : getRemoteRanks())
        {
            m_rankToScaleupGroupMap[remoteRank] =
                div((uint32_t)remoteRank, (uint32_t)m_scaleupGroupSize);  // this is correct because of integer division
        }
    }
    return m_rankToScaleupGroupMap;
}

const std::vector<HCL_Rank>& HclDynamicCommunicator::getScaleupGroupToRankMap()
{
    if (m_scaleupGroupToRankMap[0] == HCL_INVALID_RANK)
    {
        int k = 0;
        for (const auto& remoteRank : getOuterRanksInclusive())
        {
            m_scaleupGroupToRankMap[k] = remoteRank;
            k++;
        }
    }
    return m_scaleupGroupToRankMap;
}

const UniqueSortedVector& HclDynamicCommunicator::getInnerRanksExclusive()
{
    if (m_innerRanksExclusiveCache.size() == 0)
    {
        for (auto& remoteRank : getRemoteRanks())
        {
            if (remoteRank == m_rankInfo.header.hcclRank) continue;
            if (isRankInsideScaleupGroup(remoteRank))
            {
                m_innerRanksExclusiveCache.insert_sorted(remoteRank);
            }
        }
    }
    return m_innerRanksExclusiveCache;
}

const UniqueSortedVector& HclDynamicCommunicator::getInnerRanksInclusive()
{
    if (m_innerRanksInclusiveCache.size() == 0)
    {
        m_innerRanksInclusiveCache = getInnerRanksExclusive();
        m_innerRanksInclusiveCache.insert_sorted(m_rankInfo.header.hcclRank);
    }
    return m_innerRanksInclusiveCache;
}

const UniqueSortedVector& HclDynamicCommunicator::getConnectedRanks()
{
    // based assumption - ranks caches are already initialized in initCommunicator
    const UniqueSortedVector& outerRanks = getOuterRanksExclusive();

    if (m_connectedRanks.size() == 0)
    {
        m_connectedRanks = getInnerRanksExclusive();
        m_connectedRanks.insert_range_sorted(outerRanks.begin(), outerRanks.end());
    }
    return m_connectedRanks;
}

HCL_Rank HclDynamicCommunicator::getMyRank() const
{
    return m_rankInfo.header.hcclRank;
}

HCL_Rank HclDynamicCommunicator::getScaleUpLastRank()
{
    return getInnerRanksInclusive()[getInnerRanksInclusive().size() - 1];
}

HCL_Rank HclDynamicCommunicator::getScaleOutLastRank()
{
    return getOuterRanksInclusive()[getOuterRanksInclusive().size() - 1];
}

bool HclDynamicCommunicator::isLastRankInScaleupGroup()
{
    return getMyRank() == getScaleUpLastRank();
}

uint16_t HclDynamicCommunicator::getMyScaleupGroup()
{
    return getRankToScaleupGroupMap()[getMyRank()];
}

const UniqueSortedVector& HclDynamicCommunicator::getRanks() const
{
    UniqueSortedVector& ranksVec = m_ranksCache;

    if (ranksVec.size() == 0)
    {
        LOG_HCL_TRACE(HCL, "First call myRank({}) Remote({})", getMyRank(), getRemoteRanks().size());
        ranksVec.insert_sorted(getMyRank());

        for (auto& rank : getRemoteRanks())
        {
            ranksVec.insert_sorted(rank);
        }
    }

    return ranksVec;
}

const std::vector<HCL_Rank>& HclDynamicCommunicator::getRemoteRanks() const
{
    return m_remoteRanks;
}

uint32_t HclDynamicCommunicator::getScaleupGroupSize() const
{
    return m_scaleupGroupSize;
}

void HclDynamicCommunicator::incCollectiveCtr()
{
    m_collectiveCtr++;
}

const uint64_t HclDynamicCommunicator::getCollectiveCtr() const
{
    return m_collectiveCtr;
}

const uint64_t HclDynamicCommunicator::incSendCtr(int peer)
{
    return (m_sendCounter.size() < MAX_SEND_RECV_PEER_COUNTER || m_sendCounter.count(peer) > 0) ? ++m_sendCounter[peer]
                                                                                                : 0;
}

const uint64_t HclDynamicCommunicator::getSendCtr(int peer)
{
    return m_sendCounter.count(peer) > 0 ? m_sendCounter[peer] : 0;
}

const uint64_t HclDynamicCommunicator::incRecvCtr(int peer)
{
    return (m_recvCounter.size() < MAX_SEND_RECV_PEER_COUNTER || m_recvCounter.count(peer) > 0) ? ++m_recvCounter[peer]
                                                                                                : 0;
}

const uint64_t HclDynamicCommunicator::getRecvCtr(int peer)
{
    return m_recvCounter.count(peer) > 0 ? m_recvCounter[peer] : 0;
}

uint32_t HclDynamicCommunicator::getCommSize()
{
    return getRanks().size();
}

uint64_t HclDynamicCommunicator::getSliceSize() const
{
    return m_sliceSize;
}

hcclResult_t HclDynamicCommunicator::validateRankIds()
{
    LOG_DEBUG(HCL, "validating rank Ids");
    std::set<HCL_Rank> rankIds;
    for (auto& rank : getRemoteRanks())
    {
        HCL_Rank rankId = m_remoteDevices[rank]->header.hcclRank;
        if (rankId < 0 || rankId > m_commSize - 1)
        {
            LOG_ERR(HCL, "Found invalid rank. invalid rank id is {} where comm size is {}", rankId, m_commSize);
            return hcclInternalError;
        }
        rankIds.insert(rankId);
    }
    if (rankIds.size() != (unsigned)m_commSize)
    {
        LOG_ERR(HCL,
                "Invalid amount of rank IDs. comm size is {} but {} rank IDs appeared",
                m_commSize,
                rankIds.size());
        return hcclInternalError;
    }
    return hcclSuccess;
}

hcclResult_t HclDynamicCommunicator::setSliceSize()
{
    const bool isMultiNode = div((uint32_t)getCommSize(), (uint32_t)getScaleupGroupSize()) > 1;
    LOG_HCL_DEBUG(HCL, "m_commId={}, isMultiNode={}", m_commId, isMultiNode);
    if (isMultiNode && hccl_device()->getScaleOutProvider()->isGaudiDirect() &&
        !GCFG_HCL_SLICE_SIZE.isSetFromUserConfig())
    {
        LOG_HCL_DEBUG(HCL,
                      "Using slice size of {}MB since Gaudi-direct is enabled",
                      B2MB(GCFG_HCL_GDR_SLICE_SIZE.value()));
        m_sliceSize = GCFG_HCL_GDR_SLICE_SIZE.value();
    }
    else
    {
        m_sliceSize = GCFG_HCL_SLICE_SIZE.value();
    }

    if (hccl_device()->getSIBBufferSize() < m_sliceSize)
    {
        LOG_HCL_ERR(HCL,
                    "HCL_SLICE_SIZE (0x{:x}) is expected to be equal or less than HCL_IMB_SIZE (0x{:x})",
                    m_sliceSize,
                    hccl_device()->getSIBBufferSize());
        return hcclInvalidArgument;
    }

    return hcclSuccess;
}

hcclResult_t HclDynamicCommunicator::validateComm()
{
    LOG_HCL_DEBUG(HCL, "m_commId={}", m_commId);
    hcclResult_t res = hcclInternalError;

    if (m_commSize < 1)
    {
        LOG_ERR(HCL, "Invalid commSize({}). The minimal commSize is 1", m_commSize);
        return res;
    }

    res = validateRankIds();
    if (res != hcclSuccess)
    {
        LOG_ERR(HCL, "Comm ({}) validation failed on rank ids validation", m_commId);
        return res;
    }

    return hcclSuccess;
}

hcclResult_t HclDynamicCommunicator::setCommScaleupGroupSize()
{
    // for performance we want to run this only once
    if (m_scaleupGroupSize != (uint32_t)-1)
    {
        LOG_HCL_ERR(HCL, "ScaleupGroup size for comm ({}) was already set", m_commId);
        return hcclInternalError;
    }

    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        m_scaleupGroupSize = m_serverDef.getDefaultBoxSize();
        return hcclSuccess;
    }

    // set a default
    m_scaleupGroupSize = m_serverDef.getDefaultScaleupGroupSize();

    // get a vector of all the module ids per host
    std::vector<std::string> hostnames;

    DevicesSet hwModulesInBox = {};  // Will include all the hwModules inside our box;

    const DevicesSet& hwModules = m_serverDef.getHwModules();
    LOG_HCL_DEBUG(HCL, "My hwModuleID={}, hwModules=[{}]", m_rankInfo.header.hwModuleID, hwModules);

    // Get the remote h/w module ids within same box as my h/w module id, this includes self rank
    for (auto& remote : getRemoteRanks())
    {
        const std::string& hostName = m_remoteDevices[remote]->header.hostname;
        hostnames.push_back(hostName);
        const HCL_HwModuleId remoteModuleId = m_remoteDevices[remote]->header.hwModuleID;
        if ((hostName.compare(m_rankInfo.header.hostname) == 0) && (hwModules.count(remoteModuleId) != 0))
        {
            hwModulesInBox.insert(remoteModuleId);
            LOG_HCL_DEBUG(HCL, "Adding hostName={}, hwModuleID[{}]={}", hostName, remote, remoteModuleId);
        }
    }

    const unsigned boxSize = m_serverDef.getDefaultBoxSize();
    // count the number of ranks in my box, adjust by boxSize for cases of server with 2 boxes
    const unsigned ranksInBox =
        hwModulesInBox.size();  // the number of active comm ranks within the my box is the ScaleupGroup size
    LOG_HCL_DEBUG(HCL, "boxSize={}, ranksInBox={}, hwModulesInBox=[{}]", boxSize, ranksInBox, hwModulesInBox);

    // in loopback mode, its always fixed comm size even only one rank is running
    if (isLoopbackMode())
    {
        m_scaleupGroupSize = GCFG_LOOPBACK_SCALEUP_GROUP_SIZE.value();
    }
    else
    {
        // "fix" ScaleupGroup size to match partial box size
        if (ranksInBox > 0 && ranksInBox <= boxSize)
        {
            if (ranksInBox < boxSize)
            {
                LOG_HCL_INFO(HCL,
                             "Using partial Box: Setting Communicator ({}) ScaleupGroup Size from ({}), to "
                             "amount of devices in the same host ({}) - ({})",
                             m_commId,
                             m_serverDef.getDefaultScaleupGroupSize(),
                             m_rankInfo.header.hostname,
                             ranksInBox);
            }
            m_scaleupGroupSize = ranksInBox;
        }
        else
        {
            LOG_HCL_ERR(HCL, "Invalid ranksInBox size {}", ranksInBox);
            return hcclInternalError;
        }
    }

    std::vector<uint32_t> hostSizes(m_commSize, 0);
    unsigned              hostIndex = 0;
    for (auto& remote : getRemoteRanks())
    {
        hostIndex = div((uint32_t)m_remoteDevices[remote]->header.hcclRank, (uint32_t)m_scaleupGroupSize);
        hostSizes[hostIndex]++;
    }

    // validate all box sizes are equal
    hostIndex = 1;
    while (hostIndex < hostSizes.size() && hostSizes[hostIndex] > 0)
    {
        if (hostSizes[hostIndex] != ranksInBox)
        {
            LOG_HCL_ERR(
                HCL,
                "Comm ({}) contains ScaleupGroups with different sized. make sure to use same amount of devices "
                "from each box",
                m_commId);
            return hcclInternalError;
        }
        hostIndex++;
    }

    LOG_HCL_DEBUG(HCL, "ScaleupGroup size for comm ({}) was set to ({})", m_commId, m_scaleupGroupSize);
    return hcclSuccess;
}

hcclResult_t HclDynamicCommunicator::prepareAndValidateComm(bool isLoopbackModeOrNullSubmission)
{
    LOG_HCL_DEBUG(HCL, "m_commId={}, isLoopbackModeOrNullSubmission={}", m_commId, isLoopbackModeOrNullSubmission);
    hcclResult_t res;
    res = setCommScaleupGroupSize();
    if (res != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "Wasn't able to find valid ScaleupGroup size for comm ({})", m_commId);
        return res;
    }

    // override value from init
    setRankInScaleupGroup();

    if (!isLoopbackModeOrNullSubmission)
    {
        res = validateComm();
        if (res != hcclSuccess)
        {
            LOG_HCL_ERR(HCL, "comm ({}) validation failed", m_commId);
            return res;
        }
    }

    res = setSliceSize();
    if (res != hcclSuccess)
    {
        LOG_HCL_ERR(HCL, "Wasn't able to set proper slice size for comm ({})", m_commId);
        return res;
    }

    return res;
}

void HclDynamicCommunicator::AddNewRemoteDevice(HCL_Rank newRank)
{
    bool addRank = !m_remoteDevices[newRank]->m_initialized;
    if (addRank)
    {
        m_remoteDevices[newRank]->m_initialized = true;
        m_remoteRanks.push_back(newRank);
    }
}

bool HclDynamicCommunicator::initializeHostNicBridge(const UniqueSortedVector& outerRanks)
{
    LOG_HCL_TRACE(HCL, "outerRanks=[ {} ]", outerRanks);
    return m_hostNicBridge->initializeCommunicator(getMyRank(),
                                                   getCommSize(),
                                                   outerRanks,
                                                   hccl_device(),
                                                   m_rankInfo,
                                                   getMaxScaleOutQpSetsNum());
}

const std::string HclDynamicCommunicator::getCommUniqueId() const
{
    return m_commUniqueIdStr;
}

HCL_Rank HclDynamicCommunicator::getRankInScaleupGroup() const
{
    return m_rankInScaleupGroup;
}

void HclDynamicCommunicator::setRankInScaleupGroup()
{
    m_rankInScaleupGroup = (HCL_Rank)mod(getMyRank(), m_scaleupGroupSize);
}

unsigned HclDynamicCommunicator::getMaxScaleOutQpSetsNum()
{
    return m_maxScaleOutQpSetsNum;
}

CommConnectivity::CommConnectivity(const HCL_Comm hclComm, const Gen2ArchServerConnectivity& serverConnectivity)
: m_comm(hclComm)
{
    updateScaleOutPortsMask(serverConnectivity, nics_mask_t(NBITS(64)));
}

void CommConnectivity::updateScaleOutPortsMask(const Gen2ArchServerConnectivity& serverConnectivity,
                                               const nics_mask_t                 operationalScaleOutPortsMask)
{
    setPortsMasks(serverConnectivity, operationalScaleOutPortsMask);
    setNumScaleOutPorts(serverConnectivity);
}

void CommConnectivity::setPortsMasks(const Gen2ArchServerConnectivity& m_serverConnectivity,
                                     const nics_mask_t                 operationalScaleOutPortsMask)
{
    RuntimePortsMasksUtils::SetPortsMaskInput input {.comm                         = m_comm,
                                                     .serverConnectivity           = m_serverConnectivity,
                                                     .operationalScaleOutPortsMask = operationalScaleOutPortsMask};

    RuntimePortsMasksUtils::SetPortsMaskOutput output = RuntimePortsMasksUtils::setPortsMasksCommon(input);

    m_enabledExternalPortsMask = output.enabledExternalPortsMask;
}

void CommConnectivity::setNumScaleOutPorts(const Gen2ArchServerConnectivity& serverConnectivity)
{
    uint16_t sub_port_index_min = 0;
    uint16_t sub_port_index_max = serverConnectivity.getMaxNumScaleOutPorts() - 1;  // Includes LKD mask
    m_enabledScaleoutSubPorts.clear();
    m_enabledScaleoutPorts = 0;
    // collect all ports that are pre-defined as scaleout ports and enabled in hl-thunk port mask
    for (uint16_t port_index = 0; port_index < MAX_NICS_GEN2ARCH; port_index++)
    {
        if (serverConnectivity.isScaleoutPort(port_index))
        {
            // Accordingly to FW implementation, the port with the lowest sub port index
            // will be used for scaleout if some of the ports were disabled.
            // Example:
            // |         sub port indices      |    number of used ports   |         active ports        |
            // +-------------------------------+---------------------------+-----------------------------+
            // |        8->2, 22->0, 23->1     |             2             |             22,23           |
            // +-------------------------------+---------------------------+-----------------------------+
            // |        8->2, 22->0, 23->1     |             1             |               22            |
            // +-------------------------------+---------------------------+-----------------------------+
            if (m_enabledExternalPortsMask[port_index])
            {
                m_enabledScaleoutPorts[port_index] = true;
                m_enabledScaleoutSubPorts.insert(std::make_pair(port_index, sub_port_index_min));
                sub_port_index_min++;
            }
            else
            {
                m_enabledScaleoutSubPorts.insert(std::make_pair(port_index, sub_port_index_max));
                sub_port_index_max--;
            }
        }
    }
    LOG_HCL_INFO(HCL,
                 "Enabled number of scaleout ports for comm {} by LKD/user mask is: {} out of {} possible.",
                 m_comm,
                 m_enabledScaleoutPorts.to_str(),
                 serverConnectivity.getAllScaleoutPorts().to_str());
    for (const auto kv : m_enabledScaleoutSubPorts)
    {
        LOG_HCL_DEBUG(HCL, "m_enabled_scaleout_sub_ports for comm {}: [{}, {}]", m_comm, kv.first, kv.second);
    }
}

void HclDynamicCommunicator::getAsyncError(hcclResult_t* asyncError)
{
    // we need to pass to HclDevice all the module IDs in this comm
    // we get all the inner ranks' module IDs, and check if we have outer ranks.
    // if yes, we add SCALEOUT_DEVICE_ID to the list of module IDs
    std::vector<HCL_HwModuleId> remoteModuleIDs;
    for (auto& remoteRank : getInnerRanksExclusive())
    {
        const HCL_HwModuleId moduleID = m_remoteDevices[remoteRank]->header.hwModuleID;
        remoteModuleIDs.push_back(moduleID);
        LOG_HCL_DEBUG(HCL, "adding module ID {} for remote rank {}", moduleID, remoteRank);
    }

    if (isCommunicatorMultiScaleupGroup())
    {
        LOG_HCL_DEBUG(HCL, "adding scaleout device ID");
        remoteModuleIDs.push_back(SCALEOUT_DEVICE_ID);
    }

    hccl_device()->getAsyncError(remoteModuleIDs, m_commId, asyncError);
}
