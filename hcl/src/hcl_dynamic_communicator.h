#pragma once

#include <array>    // for array
#include <cstdint>  // for uint16_t
#include <vector>   // for vector
#include <memory>   // for allocator, unique_ptr
#include <map>

#include "hcl_api_types.h"                        // for HCL_Rank
#include "hccl_types.h"                           // for hcclResult_t
#include "hcl_types.h"                            // for MAX_RANKS, INVALID_SCALEUP_GROUP
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "interfaces/hcl_remote_device.h"         // for HclRemoteDevice
#include "hccl/ofi_communicator.h"                // for ofi_communicator_handle
#include "interfaces/hcl_hal.h"                   // for HalPtr
#include "hccl_internal_defs.h"                   // for internal_unique_id_t

class HclStaticBuffersManager;
class IHclDevice;
class Gen2ArchServerDef;
class Gen2ArchServerConnectivity;

class CommConnectivity
{
public:
    CommConnectivity(const HCL_Comm hclComm, const Gen2ArchServerConnectivity& serverConnectivity);
    CommConnectivity(CommConnectivity&)  = delete;
    CommConnectivity(CommConnectivity&&) = delete;

    void updateScaleOutPortsMask(const Gen2ArchServerConnectivity& serverConnectivity,
                                 const nics_mask_t                 operationalScaleOutPortsMask);

    nics_mask_t getExternalPortsMask() const { return m_enabledExternalPortsMask; }
    nics_mask_t getScaleOutPorts() const { return m_enabledScaleoutPorts; }
    uint16_t    getNumScaleOutPorts() const { return m_enabledScaleoutPorts.count(); }
    uint16_t    getScaleoutSubPortIndex(const uint16_t port) const { return m_enabledScaleoutSubPorts.at(port); }

private:
    void setPortsMasks(const Gen2ArchServerConnectivity& serverConnectivity,
                       const nics_mask_t                 operationalScaleOutPortsMask);
    void setNumScaleOutPorts(const Gen2ArchServerConnectivity& serverConnectivity);

    const HCL_Comm m_comm;

    nics_mask_t                            m_enabledExternalPortsMask {};  // After masking by LKD & HCL & NICs-Down
    nics_mask_t                            m_enabledScaleoutPorts {};      // After LKD, HCL Mask
    std::unordered_map<uint16_t, uint16_t> m_enabledScaleoutSubPorts;      // Key => Port, Value => max sub port index
};

class HclDynamicCommunicator
{
public:
    HclDynamicCommunicator(const HCL_Comm comm, Gen2ArchServerDef& serverDef, hcl::HalPtr hal);
    virtual ~HclDynamicCommunicator() = default;

    bool init(const uint32_t hcclCommSize, const HCL_Rank rank, const int box_size);

    void setUniqueID(const internal_unique_id_t* internal_unique_id);

    /**
     * determine whether comm is inside the ScaleupGroup communicator
     */
    bool isCommunicatorInScaleupGroup() const;

    /**
     * determine whether comm is a ScaleupGroup peers communicator
     */
    bool isCommunicatorScaleupGroupPeers() const;
    bool isCommunicatorMultiScaleupGroup() const;  // determine if comm requires scaleout
    bool commScaleupGroupHasMultipleRanks() const;
    bool isCommunicatorHierarchical() const;  // determine if comm requires scaleout & scaleup

    bool isPeer(HCL_Rank rank) const;
    bool arePeers(HCL_Rank rank1, HCL_Rank rank2) const;

    bool isRankInsideScaleupGroup(HCL_Rank rank) const;
    bool isRanksInSameScaleupGroup(HCL_Rank rank1, HCL_Rank rank2) const;
    bool isPeerOrInsideSameScaleupGroup(HCL_Rank rank);

    const RankInfoHeader&        getRemoteConnectionHeader(HCL_Rank rank) const;
    const UniqueSortedVector&    getInnerRanksExclusive();
    const UniqueSortedVector&    getInnerRanksInclusive();
    const UniqueSortedVector&    getOuterRanksExclusive();
    const UniqueSortedVector&    getOuterRanksInclusive();
    const UniqueSortedVector&    getConnectedRanks();
    const std::vector<uint32_t>& getRankToScaleupGroupMap();
    const std::vector<HCL_Rank>& getScaleupGroupToRankMap();

    HCL_Rank                  getMyRank() const;
    HCL_Rank                  getScaleUpLastRank();
    HCL_Rank                  getScaleOutLastRank();
    bool                      isLastRankInScaleupGroup();
    uint16_t                  getMyScaleupGroup();
    const UniqueSortedVector& getRanks() const;
    uint32_t                  getCommSize();
    uint32_t                  getScaleupGroupSize() const;
    const uint64_t            getCollectiveCtr() const;
    void                      incCollectiveCtr();
    const uint64_t            incSendCtr(int peer);
    const uint64_t            getSendCtr(int peer);
    const uint64_t            incRecvCtr(int peer);
    const uint64_t            getRecvCtr(int peer);
    HCL_Rank                  getRankInScaleupGroup() const;
    void                      setRankInScaleupGroup();
    unsigned                  getMaxScaleOutQpSetsNum();
    uint64_t                  getSliceSize() const;

    hcclResult_t      prepareAndValidateComm(bool isLoopbackModeOrNullSubmission = false);
    void              AddNewRemoteDevice(HCL_Rank newRank);
    const std::string getCommUniqueId() const;

    Gen2ArchServerDef& getServerDef() { return m_serverDef; };

    void getAsyncError(hcclResult_t* asyncError);

    HclRemoteDeviceArray m_remoteDevices;
    RankInfo             m_rankInfo           = {};
    uint32_t             m_commSize           = -1;
    HCL_Rank             m_rankInScaleupGroup = -1;

    bool initializeHostNicBridge(const UniqueSortedVector& outerRanks);

    ofi_communicator_handle m_hostNicBridge;

    const std::vector<HCL_Rank>& getRemoteRanks() const;
    hcclResult_t                 setCommScaleupGroupSize();

    uint32_t m_scaleupGroupSize = -1;

    mutable UniqueSortedVector m_ranksCache;

    std::vector<uint64_t> m_streamLatestLongSo;

    operator HCL_Comm() const { return m_commId; }
    const CommConnectivity& getCommConnectivity() const { return m_commConnectivity; }
    CommConnectivity&       getCommConnectivity() { return m_commConnectivity; }

private:
    HCL_Comm         m_commId;
    CommConnectivity m_commConnectivity;

    hcclResult_t validateComm();
    hcclResult_t validateRankIds();
    /**
     * @brief Set the Slice Size based on communicator size and scaleout method.
     *
     * If GDR and more than 1 box - use increased slice size.
     *
     * @return hcclSuccess if configured slice size is valid
     */
    hcclResult_t setSliceSize();

    UniqueSortedVector    m_innerRanksExclusiveCache;  // exclude rank itself
    UniqueSortedVector    m_innerRanksInclusiveCache;  // include rank itself
    UniqueSortedVector    m_outerRanksExclusiveCache;  // exclude rank itself
    UniqueSortedVector    m_outerRanksInclusiveCache;  // include rank itself
    UniqueSortedVector    m_connectedRanks;            // exclude rank itself (inside ScaleupGroup + peers)
    std::vector<uint32_t> m_rankToScaleupGroupMap = {};
    std::vector<HCL_Rank> m_scaleupGroupToRankMap = {};

    std::map<int, uint64_t> m_sendCounter;
    std::map<int, uint64_t> m_recvCounter;

    internal_unique_id_t  m_commUniqueId;
    std::string           m_commUniqueIdStr;
    Gen2ArchServerDef&    m_serverDef;
    hcl::HalPtr           m_hal;
    std::vector<HCL_Rank> m_remoteRanks   = {};
    uint64_t              m_collectiveCtr = 0;
    uint64_t              m_sliceSize;
    unsigned              m_maxScaleOutQpSetsNum;
};
