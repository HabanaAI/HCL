#pragma once

#include <array>    // for array
#include <cstdint>  // for uint16_t
#include <vector>   // for vector
#include <memory>   // for allocator, unique_ptr
#include <map>
#include <mutex>  // for mutex, unique_lock

#include "hcl_api_types.h"                        // for HCL_Rank
#include "hccl_types.h"                           // for hcclResult_t
#include "hcl_types.h"                            // for MAX_RANKS, INVALID_SCALEUP_GROUP, HCL_StreamId
#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector
#include "interfaces/hcl_remote_device.h"         // for HclRemoteDevice
#include "hccl/ofi_communicator.h"                // for ofi_communicator_handle
#include "interfaces/hcl_hal.h"                   // for HalPtr
#include "hccl_internal_defs.h"                   // for internal_unique_id_t

class HclStaticBuffersManager;
class IHclDevice;
class Gen2ArchServerDef;
class Gen2ArchServerConnectivity;

enum class FaultToleranceState
{
    FTidle               = 0,
    FTstopApi            = 1,
    FTcreateMigrationQPs = 2,
    FTmoveToRts          = 3,
    FTwaitMaxCounters    = 4,
    FTcommUpdate         = 5
};

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

    nics_mask_t m_enabledExternalPortsMask {};  // After masking by LKD & HCL & NICs shutdown, physical scaleout ics
    nics_mask_t m_enabledScaleoutPorts {};      // After LKD, HCL Mask, & NICs shutdown, physical scaleout NICs
    std::unordered_map<uint16_t, uint16_t>
        m_enabledScaleoutSubPorts;  // Key => Physical scaleout nic, Value => max sub port index
};

/**
 * @brief holds the API counters for send/recv per rank
 */
struct SendRecvApiCounters
{
    uint64_t sendCounter = 0;
    uint64_t recvCounter = 0;
};

/**
 * @brief holds the API counters for collectives and all remote ranks send/recv counters
 */
struct RankApiCounters
{
    RankApiCounters() = default;
    RankApiCounters(const size_t commSize) : ranksSendRecv(commSize) {}
    RankApiCounters(const size_t commSize, const uint64_t initValue)
    : collectivesCounter(initValue), ranksSendRecv(commSize, {initValue, initValue})
    {
    }

    // Returns true if this is less than other, false if equal or greater than other
    // Compares the collective counter and all send/recv counters per rank
    // If at least one of the counters is less than the other, returns true
    bool compareLessThanCounters(const RankApiCounters& other) const
    {
        if (collectivesCounter < other.collectivesCounter)
        {
            return true;
        }
        // for (size_t i = 0; i < ranksSendRecv.size(); i++)
        // {
        //     if (ranksSendRecv[i].sendCounter < other.ranksSendRecv[i].sendCounter)
        //     {
        //         return true;
        //     }
        //     if (ranksSendRecv[i].recvCounter < other.ranksSendRecv[i].recvCounter)
        //     {
        //         return true;
        //     }
        // }
        return false;
    }

    // Returns true if this is equal than other, false if not
    // Compares the collective counter and all send/recv counters per rank
    // All the counters must be equal to return true
    bool compareEqualCounters(const RankApiCounters& other) const
    {
        if (collectivesCounter != other.collectivesCounter)
        {
            return false;
        }
        // for (size_t i = 0; i < ranksSendRecv.size(); i++)
        // {
        //     if (ranksSendRecv[i].sendCounter != other.ranksSendRecv[i].sendCounter)
        //     {
        //         return false;
        //     }
        //     if (ranksSendRecv[i].recvCounter != other.ranksSendRecv[i].recvCounter)
        //     {
        //         return false;
        //     }
        // }
        return true;
    }

    void resize(const size_t commSize) { ranksSendRecv.resize(commSize, {}); }
    void resize(const size_t commSize, const uint64_t initValue)
    {
        collectivesCounter = initValue;
        ranksSendRecv.resize(commSize, {initValue, initValue});
    }
    void fill(const uint64_t initValue)
    {
        collectivesCounter = initValue;
        for (auto& rank : ranksSendRecv)
        {
            rank.sendCounter = initValue;
            rank.recvCounter = initValue;
        }
    }
    void clear() { fill(0); }
    void logDebug(const HCL_Comm commId, const std::string_view& prefix, const std::string_view& varName) const;
    void logDebugCompare(const HCL_Comm          commId,
                         const std::string_view& prefix,
                         const std::string_view& varName,
                         const RankApiCounters&  myCounters) const;

    uint64_t                         collectivesCounter = 0;   // collectives API counter
    std::vector<SendRecvApiCounters> ranksSendRecv      = {};  // s/r counters per remote rank
};

struct FaultToleranceTargetCounters
{
    RankApiCounters       rankApiCountersData = {};  // collectives API counter and send/recv counters per rank
    std::vector<uint64_t> streamLongSo        = {};  // Long SO target per stream on this comm
};

class FaultToleranceDfaLog
{
public:
    FaultToleranceDfaLog();

    const void addDfaLog(hl_logger::LoggerSPtr logger, const RankApiCounters& counters) const;

    void failoverStart(const uint16_t nic);
    void failoverEnd();
    void failbackStart();
    void failbackEnd();
    void updateFailoverStep(const FaultToleranceState newState, RankApiCounters* counters = nullptr);
    void updateFailbackStep(const FaultToleranceState newState, RankApiCounters* counters = nullptr);

private:
    const bool isPastFailoverAndBack() const;
    const bool isInsideFailover() const;
    const bool isInsideFailback() const;
    const bool isBetweenFailoverAndFailback() const;

    FaultToleranceState                                         m_failoverState;
    FaultToleranceState                                         m_failbackState;
    uint64_t                                                    m_numFailovers = 0;
    uint64_t                                                    m_numFailbacks = 0;
    uint64_t                                                    m_maxCollectiveCounter;
    uint64_t                                                    m_maxSendRecvCounter;
    uint16_t                                                    m_nicDown;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFailoverStartTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFailbackStartTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFailoverEndTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFailbackEndTime;
};

class HclDynamicCommunicator
{
public:
    HclDynamicCommunicator(const HCL_Comm comm, Gen2ArchServerDef& serverDef, hcl::HalPtr hal);
    virtual ~HclDynamicCommunicator() = default;

    bool init(const uint32_t hcclCommSize, const HCL_Rank rank, const int boxSize);

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
    const UniqueSortedVector&    getOuterRanksExclusive();     // Peer outer ranks only
    const UniqueSortedVector&    getOuterRanksInclusive();     // Peer outer ranks and our rank only
    const UniqueSortedVector&    getAllOuterRanksExclusive();  // All outer ranks only
    const UniqueSortedVector&    getConnectedRanks();
    const std::vector<uint32_t>& getRankToScaleupGroupMap();
    const std::vector<HCL_Rank>& getScaleupGroupToRankMap();

    HCL_Rank                  getMyRank() const;
    HCL_Rank                  getScaleUpLastRank();
    HCL_Rank                  getScaleOutLastRank();
    bool                      isLastRankInScaleupGroup();
    uint16_t                  getMyScaleupGroup();
    const UniqueSortedVector& getRanks() const;
    uint32_t                  getCommSize() const;
    uint32_t                  getScaleupGroupSize() const;
    const uint64_t            getCollectiveCtr() const;
    void                      incCollectiveCtr();
    const uint64_t            incSendCtr(const HCL_Rank peer);
    const uint64_t            getSendCtr(const HCL_Rank peer) const;
    const uint64_t            incRecvCtr(const HCL_Rank peer);
    const uint64_t            getRecvCtr(const HCL_Rank peer) const;
    const RankApiCounters&    getApiCounters() const { return m_apiCounters; }
    void                      incApiCollectivesCounter() { m_apiCounters.collectivesCounter++; }

    const uint64_t getApiPreGroupEndSendCounter(const HCL_Rank remoteRank) const
    {
        return m_apiPreGroupEndCounters.ranksSendRecv[remoteRank].sendCounter;
    }
    void incApiPreGroupEndSendCounter(const HCL_Rank remoteRank)
    {
        m_apiPreGroupEndCounters.ranksSendRecv[remoteRank].sendCounter++;
    }
    const uint64_t getApiPreGroupEndRecvCounter(const HCL_Rank remoteRank) const
    {
        return m_apiPreGroupEndCounters.ranksSendRecv[remoteRank].recvCounter;
    }
    void incApiPreGroupEndRecvCounter(const HCL_Rank remoteRank)
    {
        m_apiPreGroupEndCounters.ranksSendRecv[remoteRank].recvCounter++;
    }

    HCL_Rank getRankInScaleupGroup() const;
    void     setRankInScaleupGroup();
    unsigned getMaxScaleOutQpSetsNum();
    uint64_t getSliceSize() const;

    hcclResult_t      prepareAndValidateComm(bool isLoopbackModeOrNullSubmission = false);
    void              AddNewRemoteDevice(HCL_Rank newRank);
    const std::string getCommUniqueId() const;

    Gen2ArchServerDef& getServerDef() { return m_serverDef; };

    void getAsyncError(hcclResult_t* asyncError);

    HclRemoteDeviceArray m_remoteDevices;
    RankInfo             m_rankInfo           = {};
    uint32_t             m_commSize           = -1;
    HCL_Rank             m_rankInScaleupGroup = -1;

    std::unordered_map<HCL_Rank, BackupGaudiNicQPs> m_backupRankQPs;

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

    const FaultToleranceTargetCounters getFaultToleranceTargetCounters()
    {
        std::unique_lock<std::mutex> lock(m_faultToleranceTargetCountersMutex);
        return m_faultToleranceTargetCounters;
    }

    void updateFaultToleranceCollectivesCounters(
        const HCL_StreamId streamId,
        const uint64_t     streamLongSo);  // Set long SO and increment collectives API Counter
    void updateFaultToleranceSendRecvCounters(
        const HCL_StreamId streamId,
        const uint64_t     streamLongSo);  // Set long SO and update send/recv counters from API calls
    void updateApiSendRecvCounters();

    bool isDfaNicExists(const uint16_t dfaNic, const HCL_Rank rank);

    FaultToleranceDfaLog m_dfaData;

private:
    const HCL_Comm   m_commId;
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

    UniqueSortedVector    m_innerRanksExclusiveCache;     // exclude rank itself
    UniqueSortedVector    m_innerRanksInclusiveCache;     // include rank itself
    UniqueSortedVector    m_outerRanksExclusiveCache;     // exclude rank itself, peer ranks
    UniqueSortedVector    m_outerRanksInclusiveCache;     // include rank itself, peer ranks
    UniqueSortedVector    m_allOuterRanksExclusiveCache;  // exclude rank itself, all remote ranks including non-peers
    UniqueSortedVector    m_connectedRanks;               // exclude rank itself (inside ScaleupGroup + peers)
    std::vector<uint32_t> m_rankToScaleupGroupMap = {};
    std::vector<HCL_Rank> m_scaleupGroupToRankMap = {};

    std::map<HCL_Rank, uint64_t> m_sendCounter;
    std::map<HCL_Rank, uint64_t> m_recvCounter;

    RankApiCounters m_apiCounters =
        {};  // Used by fault tolerance to hold the API counters, s/r are full vectors always

    RankApiCounters m_apiPreGroupEndCounters =
        {};  // Used by fault tolerance to hold the API counters before group end for s/r, s/r are full vectors always.
             // This vector is only set and read by the user thread

    internal_unique_id_t  m_commUniqueId;
    std::string           m_commUniqueIdStr;
    Gen2ArchServerDef&    m_serverDef;
    hcl::HalPtr           m_hal;
    std::vector<HCL_Rank> m_remoteRanks   = {};
    uint64_t              m_collectiveCtr = 0;
    uint64_t              m_sliceSize;
    unsigned              m_maxScaleOutQpSetsNum;

    FaultToleranceTargetCounters m_faultToleranceTargetCounters;
    std::mutex                   m_faultToleranceTargetCountersMutex;
};
