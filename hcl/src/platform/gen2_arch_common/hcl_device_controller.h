#pragma once

#include <map>     // for map
#include <vector>  // for vector

#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2Arc...
#include "platform/gen2_arch_common/hcl_graph_sync.h"  // for HclGraphSyncGen2Arch
#include "platform/gen2_arch_common/types.h"           // for fence_info
#include "device_buffer_manager.h"
#include "llvm/small_vector.h"  // for SmallVector

class HclDeviceGen2Arch;
class HclCommandsGen2Arch;
class HclGraphSyncGen2Arch;
struct SyncObjectDescriptor;

using StreamState = std::map<uint32_t, uint64_t>;  // SoIdx to waited value

constexpr unsigned STREAMS_NR        = 6;
constexpr unsigned SCHED_NR          = (unsigned)hcl::SchedulersIndex::count;
constexpr unsigned TOTAL_SCHED_NR    = 5;
constexpr unsigned MAX_STREAM_TO_INC = 6;
constexpr unsigned ARB_STREAM_IDX    = 2;

struct SchedResources
{
    uint32_t monitorBase;
    unsigned monitorsSize;
    uint32_t longMonitorBase;
    unsigned longMonitorsSize;
};

struct SchedState
{
    StreamState    streams[STREAMS_NR];
    SchedResources internalResources;
};

struct ArchStreamSyncParams
{
    uint64_t m_submittedTargetValue           = 0;
    uint64_t m_submittedInternalCgTargetValue = 0;
    uint64_t m_InternalCgTargetValue          = 0;
    unsigned m_requestedExtraCredits          = 0;
    bool     m_isPrevWaitEvent                = false;

    hcl::syncInfo* m_longSo           = nullptr;
    hcl::syncInfo* m_longSoNullSubmit = nullptr;
    hcl::SmInfo    m_smInfo;
    SchedState     m_schedulers[SCHED_NR];
    CreditManager* m_regularGPSOManager  = nullptr;
    CreditManager* m_longtermGPSOManager = nullptr;
    std::mutex     m_streamLock;

    std::function<void(void)> m_signalFinalize = nullptr;
};

namespace hcl
{
class ScalStream;
}

class HclDeviceControllerGen2Arch
{
public:
    HclDeviceControllerGen2Arch(const unsigned numOfStreams);
    virtual ~HclDeviceControllerGen2Arch();
    HclDeviceControllerGen2Arch(const HclDeviceControllerGen2Arch&)            = delete;
    HclDeviceControllerGen2Arch& operator=(const HclDeviceControllerGen2Arch&) = delete;

    void setDevice(HclDeviceGen2Arch* device) { m_device = device; }

    HclCommandsGen2Arch&      getGen2ArchCommands();
    hcl::Gen2ArchScalManager& getGen2ArchScalManager();

    HclGraphSyncGen2Arch& getGraphSync(int archStreamId) { return *m_graphSync[archStreamId]; }

    void setSignalFinalize(int archStreamId, std::function<void(void)> signalFinalize)
    {
        if (m_streamSyncParams[archStreamId].m_signalFinalize == nullptr)
            m_streamSyncParams[archStreamId].m_signalFinalize = signalFinalize;
    }

    inline ArchStreamSyncParams& getSyncParams(int archStreamId) { return m_streamSyncParams[archStreamId]; }

    void setupMonitors(int archStreamId);
    void initDeviceForCollectiveRoutine(int archStreamId, hcl::syncInfo* longSo, hcl::syncInfo* longSoNullSubmit);
    void submitWork(int archStreamId, bool submitToHw = true);

    /**
     * @brief
     * 1. Arms a monitor to look at the SO in the descriptor
     * 2. Blocks the microArch stream by decrementing its fence counter.
     * once the SO reaches its value, the monitor will increment the fence and free the stream.
     */
    void streamAddWait(hcl::ScalStream& scalStream, const SyncObjectDescriptor& descriptor, bool useEqual = false);

    /**
     * @brief
     * !!!Currently only used for debug
     * 1. Arms a long monitor to look at the LSO index from the sync manager
     * 2. Blocks the microArch stream by decrementing its fence counter.
     * once the LSO reaches its value, the monitor will increment the fence and free the stream.
     * 3. In the past this was used wait for scale out phase to finish before performing dma of the entire result
     */
    void addStreamWaitOnLongSo(hcl::ScalStream& scalStream, uint64_t soValue, unsigned soIdx);

    void advanceProg(int archStreamId, bool nopOp);
    void addNop(int archStreamId);

    /**
     * @brief In most cases scalStream=arbitrator, and we use it to free streamsToInc when we have enough credits
     * usually this method is used with waitForBarrierArm which blocks the stream
     * this function does 3 things on the scalStream
     * 1. if an external wait is needed it arms a monitor and decrements the fence on the arb stream.
     * 2. wait for creditsNr to be available on the completion group, and acquire them.
     * 3. increments the fences on all the streams in streamsToInc after receiving the credits.
     * 4. performs the LBWs in lbwBurstData after receiving the credits.
     **/
    void addBarrierArm(hcl::ScalStream&                                               scalStream,
                       bool                                                           external,
                       unsigned                                                       creditsNr,
                       const llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC>& streamsToInc,
                       bool                                                           shouldAddWait = true,
                       LBWBurstData_t* lbwBurstData = nullptr);  // external so address and cmax - completionSignals
    /**
     * @brief Each stream has two fences
     * the scheduler will mask the stream if one of its fence counters < 0
     * This function decrements the fence counter and by doing so, blocks the stream until the barrier arm will
     * increment the fence counter and release it.
     **/
    void waitForBarrierArm(hcl::ScalStream& scalStream);

    unsigned int handleExtraCredits(int archStreamId, unsigned extraCreditsNeeded);

    void setHostFences(int                                                    archStreamId,
                       int                                                    stream_idx,
                       bool                                                   isSend,
                       uint8_t                                                scaleoutInternalFences,
                       llvm_vecsmall::SmallVector<FenceInfo, HOST_FENCES_NR>& scaleoutFences);

    inline void incInternalCgTargetValue(int archStreamId)
    {
        m_streamSyncParams[archStreamId].m_InternalCgTargetValue++;
    }

    inline std::mutex& getStreamLock(int archStreamId) { return m_streamSyncParams[archStreamId].m_streamLock; }

    /**
     * @brief returns the value of the external long SO
     **/
    hcl::syncInfo eventRecord(int      archStreamId,
                              bool     isCollectTime    = false,
                              uint64_t timestampHandle  = 0,
                              uint32_t timestampsOffset = 0);

    /**
     * @brief A wait event is when we would like to block the archStreamId (user stream)
     * until we reach a LSO value.
     * the LSO index and its value are held in syncInfo which is usually returned by eventRecord.
     * works in a lazy manner.
     **/
    void streamWaitEvent(int archStreamId, hcl::syncInfo syncInfo);

    /**
     * @brief Wait on the host for all the work on archStreamId to be completed
     **/
    void synchronizeStream(int archStreamId);

    /**
     * @brief Wait on the host for all the work on archStreamId to be completed
     **/
    bool streamQuery(int archStreamId);

    void enableNullSubmit(int archStreamId, bool enable);

    inline hcl::ScalStream& getScalStream(unsigned archStreamIdx, unsigned schedIdx, unsigned streamIdx)
    {
        return m_scalManager->getScalStream(archStreamIdx, schedIdx, streamIdx);
    }

    /**
     * @brief Used externally by hcclSetTraceMarker_impl, should be used with synEventRecord/synStreamWaitEvent in order
     * to be placed correctly
     **/
    void setTraceMarker(int archStreamId, uint32_t val);

    /**
     * @brief Can be used internally for debug, this send a LBW command to the scheduler.
     * it is up to the developer to sync it correctly.
     **/
    void setTraceMarker(int archStreamId, unsigned int schedIdx, unsigned int uArchStream, uint32_t val);

protected:
    const unsigned                                           m_numOfStreams;
    ArchStreamSyncParams*                                    m_streamSyncParams = nullptr;
    HclDeviceGen2Arch*                                       m_device           = nullptr;
    std::unique_ptr<std::unique_ptr<HclGraphSyncGen2Arch>[]> m_graphSync;
    std::unique_ptr<hcl::Gen2ArchScalManager>                m_scalManager;
    std::unique_ptr<HclCommandsGen2Arch>                     m_commands;

    void setupCompCfg(int archStreamId);

    /**
     * @brief take all the credits from the external CG
     **/
    void allocAllExternalBarrier(int archStreamId);

    inline unsigned getFenceIdx(int archStreamId, unsigned uarchStreamId, unsigned fenceIdx)
    {
        return (archStreamId * STREAMS_NR + uarchStreamId) * FENCES_PER_STREAM + fenceIdx;
    }

    inline unsigned getLongMonitorIdx(int archStreamId, unsigned schedIdx, unsigned uarchStreamId)
    {
        return uarchStreamId * LONG_MONITOR_LENGTH +
               m_streamSyncParams[archStreamId].m_schedulers[schedIdx].internalResources.longMonitorBase;
    }

    /**
     * @brief Apply the pending waits that were requested in previous streamWaitEvent calls.
     * we apply a wait by arming a monitor and decrementing the fence on uarchStreamId
     * The monitor waits for the LSO to reach the value supplied by streamWaitEvent
     * 1. Arms a long monitor to look at the LSO index from m_pendingWaits
     * 2. Blocks the microArch stream by decrementing its fence counter.
     **/
    void addStreamWaitOnLongSo(hcl::ScalStream& scalStream, unsigned uarchStreamId);
};

class ScopedNullSubmit
{
public:
    ScopedNullSubmit(int archStreamId, HclDeviceControllerGen2Arch& hclDeviceController)
    : m_archStreamId(archStreamId), m_hclDeviceController(hclDeviceController)
    {
        if (GCFG_HCL_NULL_SUBMIT.value())
        {
            m_hclDeviceController.enableNullSubmit(m_archStreamId, true);
        }
    }

    ~ScopedNullSubmit()
    {
        if (GCFG_HCL_NULL_SUBMIT.value())
        {
            m_hclDeviceController.enableNullSubmit(m_archStreamId, false);
        }
    }

private:
    int                          m_archStreamId;
    HclDeviceControllerGen2Arch& m_hclDeviceController;
};
