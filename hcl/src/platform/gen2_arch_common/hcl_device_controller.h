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

constexpr unsigned STREAMS_NR                     = 6;
constexpr unsigned SCHED_NR                       = (unsigned)hcl::SchedulersIndex::count;
constexpr unsigned TOTAL_SCHED_NR                 = 5;
constexpr unsigned MAX_STREAM_TO_INC              = 6;
constexpr unsigned RR_BUFFER_GRANULARITY_SCALEUP  = RR_SCALEUP_FACTOR;
constexpr unsigned RR_BUFFER_GRANULARITY_SCALEOUT = RR_SCALEOUT_FACTOR;
constexpr unsigned ARB_STREAM_IDX                 = 2;

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
    uint64_t                  m_submittedTargetValue           = 0;
    uint64_t                  m_submittedInternalCgTargetValue = 0;
    uint64_t                  m_InternalCgTargetValue          = 0;
    unsigned                  m_requestedExtraCredits          = 0;
    bool                      m_isPrevWaitEvent                = false;

    hcl::syncInfo*            m_longSo           = nullptr;
    hcl::syncInfo*            m_longSoNullSubmit = nullptr;
    hcl::SmInfo               m_smInfo;
    SchedState                m_schedulers[SCHED_NR];
    CreditManager*            m_regularGPSOManager             = nullptr;
    CreditManager*            m_longtermGPSOManager            = nullptr;
    std::mutex                m_streamLock;

    std::function<void(void)> m_signalFinalize = nullptr;
};

namespace hcl
{
class ScalStream;
}

class HclDeviceControllerGen2Arch
{
public:
    HclDeviceControllerGen2Arch(int numOfStreams);
    virtual ~HclDeviceControllerGen2Arch();

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

    void streamAddWait(hcl::ScalStream& scalStream, const SyncObjectDescriptor& descriptor, bool useEqual = false);
    void addInternalWait(hcl::ScalStream& scalStream, uint64_t soValue, unsigned soIdx);

    void advanceProg(int archStreamId, bool nopOp);
    void addNop(int archStreamId);
    void addBarrierArm(hcl::ScalStream&                                               scalStream,
                       bool                                                           external,
                       unsigned                                                       creditsNr,
                       const llvm_vecsmall::SmallVector<unsigned, MAX_STREAM_TO_INC>& streamsToInc,
                       bool                                                           shouldAddWait = true);

    void waitForBarrierArm(hcl::ScalStream& scalStream);

    unsigned int handleExtraCredits(int archStreamId, unsigned extraCreditsNeeded);

    void setHostFences(int                                                     archStreamId,
                       int                                                     stream_idx,
                       bool                                                    isSend,
                       uint8_t                                                 scaleoutInternalFences,
                       llvm_vecsmall::SmallVector<fence_info, HOST_FENCES_NR>& scaleoutFences);

    inline void incInternalCgTargetValue(int archStreamId) { m_streamSyncParams[archStreamId].m_InternalCgTargetValue++; }

    inline std::mutex& getStreamLock(int archStreamId) { return m_streamSyncParams[archStreamId].m_streamLock; }

    hcl::syncInfo eventRecord(int      archStreamId,
                              bool     isCollectTime    = false,
                              uint64_t timestampHandle  = 0,
                              uint32_t timestampsOffset = 0);
    void          streamWaitEvent(int archStreamId, hcl::syncInfo commonState);
    void          synchronizeStream(int archStreamId);
    bool          streamQuery(int archStreamId);

    void enableNullSubmit(int archStreamId, bool enable);

    inline hcl::ScalStream& getScalStream(unsigned archStreamIdx, unsigned schedIdx, unsigned streamIdx)
    {
        return m_scalManager->getScalStream(archStreamIdx, schedIdx, streamIdx);
    }

protected:
    const int                                                m_numOfStreams;
    ArchStreamSyncParams*                                    m_streamSyncParams = nullptr;
    HclDeviceGen2Arch*                                       m_device           = nullptr;
    std::unique_ptr<std::unique_ptr<HclGraphSyncGen2Arch>[]> m_graphSync;
    std::unique_ptr<hcl::Gen2ArchScalManager>                m_scalManager;
    std::unique_ptr<HclCommandsGen2Arch>                     m_commands;

    void setupCompCfg(int archStreamId);

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

    void addWait(hcl::ScalStream& scalStream, unsigned uarchStreamId);
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
    int m_archStreamId;
    HclDeviceControllerGen2Arch& m_hclDeviceController;
};