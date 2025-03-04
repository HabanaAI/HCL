#include "hcl_public_streams.h"

#include <cstddef>
#include <hl_logger/hllog_core.hpp>
#include <vector>   // for vector
#include <cstdint>  // for uint32_t, uint...
#include <memory>   // for unique_ptr
#include <set>      // for set, _Rb_tree_...
#include <string>   // for operator+, string

#include "hcl_types.h"       // for remoteInfoNicToIndex
#include "hcl_exceptions.h"  // for hcl
#include "platform/gen2_arch_common/host_stream.h"
#include "scal.h"  // for scal_handle_t
#include "hcl_utils.h"
#include "platform/gen2_arch_common/hccl_device.h"
#include "dfa_defines.hpp"                        // for DfaErrorCode
#include "interfaces/hcl_icollective_routines.h"  // for IHclCollective...
#include "infra/hcl_debug_stats.h"                // for DEBUG_STATS_...
#include "infra/scal/gen2_arch_common/scal_manager.h"
#include "platform/gen2_arch_common/hcl_collective_routines.h"
#include "scaleout_provider.h"  // for isHostNic()
#include "hccl_context.h"
#include "hcl_api.hpp"  // for getDfaLoggersV3
#include "hcl_device_control_factory.h"

// #define HCL_API_CALL __attribute__((visibility("default")))

bool HCL_API_CALL tdrDetectionFlag = false;  // Timeout Detection and Recovery

class hccl_communicator;

using namespace hcl;
struct hcl::InternalHclStreamHandle
{
    InternalHclStreamHandle(int id) : m_streamID(id), m_deviceController(HclControlDeviceFactory::getDeviceControl()) {}
    int                          m_streamID = -1;
    HclDeviceControllerGen2Arch& m_deviceController;
};

int HCL_API_CALL hcl::getStreamID(hclStreamHandle stream)
{
    VERIFY(stream);
    VERIFY(stream->m_streamID != -1);

    return stream->m_streamID;
}

NotImplementedApiException::NotImplementedApiException(const std::string& apiName)
: ApiException(-1, apiName + " not implemented.")
{
}

HclPublicStreams::HclPublicStreams([[maybe_unused]] scal_handle_t scal) {}

HclPublicStreams::~HclPublicStreams() {}

int HclPublicStreams::getFreeStreamId()
{
    if (m_freeStreams.size() == 0) return -1;

    return *m_freeStreams.begin();
}

hclStreamHandle HclPublicStreams::createStream()
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);

    int id = getFreeStreamId();

    if (id == -1) return nullptr;

    m_freeStreams.erase(id);

    hclStreamHandle handle = new InternalHclStreamHandle(id);

    return handle;
}

void HclPublicStreams::destroyStream(hclStreamHandle streamHandle)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);

    VERIFY(streamHandle);

    m_freeStreams.insert(streamHandle->m_streamID);

    delete streamHandle;
}

syncInfo HclPublicStreams::eventRecord(hclStreamHandle streamHandle,
                                       bool            isCollectTime /*= false*/,
                                       uint64_t        timestampHandle /*= 0*/,
                                       uint32_t        timestampsOffset /*= 0*/)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);

    VERIFY(streamHandle);
    return streamHandle->m_deviceController.eventRecord(streamHandle->m_streamID,
                                                        isCollectTime,
                                                        timestampHandle,
                                                        timestampsOffset);
}

void HclPublicStreams::streamWaitEvent(hclStreamHandle streamHandle, syncInfo params)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);

    VERIFY(streamHandle);
    streamHandle->m_deviceController.streamWaitEvent(streamHandle->m_streamID, params);
}
std::unique_ptr<HclPublicStreams> HclPublicStreamsFactory::createHclPublicStreams(scal_handle_t scal)
{
    return std::make_unique<HclPublicStreams>(scal);
}

void HclPublicStreams::streamSynchronize(hclStreamHandle streamHandle)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);

    streamHandle->m_deviceController.synchronizeStream(streamHandle->m_streamID);
}

void HclPublicStreams::eventSynchronize(syncInfo params)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);
    hccl_device()->getScalManager().eventSynchronize(params.cp_handle, params.targetValue);
}

bool HclPublicStreams::streamQuery(hclStreamHandle streamHandle)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);

    return streamHandle->m_deviceController.streamQuery(streamHandle->m_streamID);
}

bool HclPublicStreams::eventQuery(syncInfo params)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);
    return hccl_device()->getScalManager().eventQuery(params.cp_handle, params.targetValue);
}

bool HclPublicStreams::DFA(DfaStatus& dfaStatus, void (*logFunc)(int, const char*))
{
    return DFA(dfaStatus, logFunc, DfaLogPhase::Main);
}

bool HclPublicStreams::DFA(DfaStatus& dfaStatus,
                           [[maybe_unused]] void (*dfaLogFunc)(int, const char*),
                           DfaLogPhase options)
{
    DfaLoggersV3 dfaLoggers = getDfaLoggersV3();

    if ((dfaLoggers.dfaSynDevFailLogger == nullptr) || (dfaLoggers.dfaFailedRecipeLogger == nullptr) ||
        (dfaLoggers.dfaDmesgLogger == nullptr) || (dfaLoggers.dfaNicInfoLogger == nullptr) ||
        (dfaLoggers.dfaApi == nullptr) || (dfaLoggers.dfaApiInfo == nullptr))
    {
        LOG_HCL_ERR(HCL, "dfaLogFunc provided to HCL is null");
        return false;
    }

    hl_logger::LoggerSPtr synDevFailLog = dfaLoggers.dfaSynDevFailLogger;

    if (!hccl_device().initialized)
    {
        HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "No HCL device");
        return false;
    }

    switch (options)
    {
        case DfaLogPhase::Main:
            return logDfaMain(dfaStatus, nullptr, dfaLoggers);

        case DfaLogPhase::Ccb:
            return dfaLogCcb(dfaLoggers.dfaSynDevFailLogger);

        default:
        {
            HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "Requested DFA option is unknown {}", options);
            return false;
        }
    }
}

bool HclPublicStreams::logDfaMain(DfaStatus& dfaStatus,
                                  [[maybe_unused]] void (*dfaLogFunc)(int, const char*),
                                  DfaLoggersV3& dfaLoggers)
{
    try
    {
        HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_LOW);  // ??

        if (dfaStatus.hasError(DfaErrorCode::scalTdrFailed))
        {
            tdrDetectionFlag = true;
        }

        hl_logger::LoggerSPtr synDevFailLog = dfaLoggers.dfaSynDevFailLogger;

        if (!hccl_device().initialized)
        {
            HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "HCL is not initialized");
            return false;
        }

        for (auto inst : hccl_device().collectives)
        {
            inst->getSignalsManager()->DFA(
                hccl_device()->getScalManager().getCurrentLongSoValue(inst->getArchStream()));
        }

        if (hccl_device()->getDeviceTypeStr() == "synDeviceGaudi2")
        {
            int      rc;
            uint32_t val;
            uint64_t nic4RxbAddr  = 0x1000007ffd66c440;
            uint64_t nic11RxbAddr = 0x1000007ffd9ec440;

            rc = hlthunk_device_memory_read_block_experimental(hccl_device()->getFd(),
                                                               &val,
                                                               nic4RxbAddr,
                                                               sizeof(uint32_t),
                                                               0);
            if (rc == 0)
            {
                if ((val & 0x40) == 0)
                {
                    LOG_HCL_ERR(HCL, "RXB is not empty for NIC4");
                    HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "RXB is not empty for NIC4");
                }
            }
            else
            {
                LOG_HCL_CRITICAL(HCL, "Device memory read failure for NIC 4");
                HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "Device memory read failure for NIC 4");
            }

            rc = hlthunk_device_memory_read_block_experimental(hccl_device()->getFd(),
                                                               &val,
                                                               nic11RxbAddr,
                                                               sizeof(uint32_t),
                                                               0);
            if (rc == 0)
            {
                if ((val & 0x40) == 0)
                {
                    LOG_HCL_ERR(HCL, "RXB is not empty for NIC11");
                    HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "RXB is not empty for NIC11");
                }
            }
            else
            {
                LOG_HCL_CRITICAL(HCL, "Device memory read failure for NIC 11");
                HLLOG_UNTYPED(synDevFailLog, HLLOG_LEVEL_ERROR, "Device memory read failure for NIC 11");
            }
        }

        hccl_device()->dfa(dfaLoggers.dfaNicInfoLogger);
        hccl_ctx.dfaLog(synDevFailLog);
        dfaLogCommInfo(hccl_device(), dfaLoggers);
        dfaLogHostFences(hccl_device(), synDevFailLog);
        dfaLogCmdBuff(hccl_device(), synDevFailLog);

        return true;
    }
    catch (std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Caught unknown exception." << std::endl;
    }

    return false;
}

bool HclPublicStreams::dfaLogCcb(hl_logger::LoggerSPtr logger)
{
    for (auto inst : hccl_device().collectives)
    {
        hccl_device()->getScalManager().dfaLog(inst->getArchStream(), logger);
    }
    return true;
}

void HclPublicStreams::dfaLogHostFences(IHclDevice* iDev, hl_logger::LoggerSPtr logger)
{
    HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "============================ Host Fences ==========================");

    HclDeviceGen2Arch* devGen2 = dynamic_cast<HclDeviceGen2Arch*>(iDev);

    if (devGen2 == nullptr)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "Device is not HclDeviceGen2Arch");
        return;
    }

    ScaleoutProvider* scaleoutProvider = devGen2->getScaleOutProvider();
    if (scaleoutProvider == nullptr)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "Device doesn't have scaleoutProvider");
        return;
    }

    bool isHostNic = scaleoutProvider->isHostNic();
    if (!isHostNic)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "--- Device doesn't have host NICs");
        return;
    }

    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "Fence              |syncMgr   |Pointers                             |Values");
    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "ArchStream|FenceIdx|core|  idx|Device            |Host              |Device              |Host      "
                  "          ");
    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "----------------------------------------------------------------------------------------------------"
                  "----------");

    for (size_t i = 0; i < ScalJsonNames::numberOfArchsStreams; i++)
    {
        for (unsigned j = 0; j < HOST_FENCES_NR; ++j)
        {
            const InternalHostFenceInfo& fenceInfo = devGen2->getScalManager().getHostFenceInfo(i, j);

            std::string out = fmt::format(FMT_COMPILE("{:10}|{:8}|{:4}|{:5}|{:18p}|{:18p}|"),
                                          i,
                                          j,
                                          fenceInfo.hostFenceInfo.smDcore,
                                          fenceInfo.hostFenceInfo.smIndex,
                                          fmt::ptr(fenceInfo.decrementsPtr),
                                          fmt::ptr(fenceInfo.incrementsPtr));

            out += fenceInfo.decrementsPtr ? fmt::format(FMT_COMPILE("{:20}|"), *fenceInfo.decrementsPtr)
                                           : "             nullptr|";
            out += fenceInfo.incrementsPtr ? fmt::format(FMT_COMPILE("{:20}"), (uint64_t)(*fenceInfo.incrementsPtr))
                                           : "nullptr";

            HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", out);
        }
    }
}

static int getAccelNum(IHclDevice* device)
{
    const std::string accelPath = getHLDevice(device->getFd());
    const std::string accel     = accelPath.substr(accelPath.find_last_of("/") + 1);
    return accel[accel.length() - 1] - '0';
}

void HclPublicStreams::dfaLogCmdBuff(IHclDevice* iDev, hl_logger::LoggerSPtr logger)
{
    HclDeviceGen2Arch* devGen2 = dynamic_cast<HclDeviceGen2Arch*>(iDev);

    if (devGen2 == nullptr)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "Device is not HclDeviceGen2Arch");
        return;
    }

    ScaleoutProvider* scaleoutProvider = devGen2->getScaleOutProvider();
    if (scaleoutProvider == nullptr)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "Device doesn't have scaleoutProvider");
        return;
    }
    LibfabricScaleoutProvider* libfabricScaleoutProvider;
    if (scaleoutProvider->isHostNic())
    {
        libfabricScaleoutProvider = reinterpret_cast<LibfabricScaleoutProvider*>(scaleoutProvider);
    }
    else
    {
        return;
    }
    HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "============================ Command Buffer ==========================");
    for (unsigned archStream = 0; archStream < ScalJsonNames::numberOfArchsStreams; archStream++)
    {
        for (size_t uarchStream = 0;
             uarchStream < (GCFG_ENABLE_HNIC_MICRO_STREAMS.value() ? HOST_MICRO_ARCH_STREAMS : 1);
             uarchStream++)
        {
            HostStream* send_stream =
                libfabricScaleoutProvider->m_hostStreamVec[archStream][uarchStream][HOST_STREAM_SEND];
            HostStream* send_wait_stream =
                libfabricScaleoutProvider->m_hostStreamVec[archStream][uarchStream][HOST_STREAM_WAIT_FOR_SEND_COMP];

            spHostStreamFifo inner_queue           = send_stream->getInnerQueue();
            spHostStreamFifo outer_queue           = send_stream->getOuterQueue();
            spHostStreamFifo send_wait_outer_queue = send_wait_stream->getOuterQueue();

            printStreamQueuesDFALog(archStream,
                                    uarchStream,
                                    &inner_queue,
                                    &outer_queue,
                                    &send_wait_outer_queue,
                                    logger,
                                    "SEND");

            HostStream* recv_stream =
                libfabricScaleoutProvider->m_hostStreamVec[archStream][uarchStream][HOST_STREAM_RECV];
            HostStream* recv_wait_stream =
                libfabricScaleoutProvider->m_hostStreamVec[archStream][uarchStream][HOST_STREAM_WAIT_FOR_RECV_COMP];

            inner_queue                            = recv_stream->getInnerQueue();
            outer_queue                            = recv_stream->getOuterQueue();
            spHostStreamFifo recv_wait_outer_queue = recv_wait_stream->getOuterQueue();

            printStreamQueuesDFALog(archStream,
                                    uarchStream,
                                    &inner_queue,
                                    &outer_queue,
                                    &recv_wait_outer_queue,
                                    logger,
                                    "RECV");
        }
    }
}

void HclPublicStreams::printStreamQueuesDFALog(unsigned              archStream,
                                               size_t                uarchStream,
                                               void*                 inner_queue,
                                               void*                 outer_queue,
                                               void*                 wait_outer_queue,
                                               hl_logger::LoggerSPtr logger,
                                               const std::string     stream_name)
{
    spHostStreamFifo inner_queue_obj      = *reinterpret_cast<spHostStreamFifo*>(inner_queue);
    spHostStreamFifo outer_queue_obj      = *reinterpret_cast<spHostStreamFifo*>(outer_queue);
    spHostStreamFifo wait_outer_queue_obj = *reinterpret_cast<spHostStreamFifo*>(wait_outer_queue);
    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "archStream: {} uarchStream: {} HOST_STREAM_{} Inner Buffer ci: {} pi: {} next_pi: {} watermark: {}",
                  archStream,
                  uarchStream,
                  stream_name,
                  inner_queue_obj->getCi(),
                  inner_queue_obj->getPi(),
                  inner_queue_obj->getNextPi(),
                  inner_queue_obj->getWatermark());

    printQueueDFALog(archStream, uarchStream, inner_queue, logger, stream_name);
    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "archStream: {} uarchStream: {} HOST_STREAM_{} Outer Buffer ci: {} pi: {} next_pi: {} watermark: {}",
                  archStream,
                  uarchStream,
                  stream_name,
                  outer_queue_obj->getCi(),
                  outer_queue_obj->getPi(),
                  outer_queue_obj->getNextPi(),
                  outer_queue_obj->getWatermark());

    printQueueDFALog(archStream, uarchStream, outer_queue, logger, stream_name);

    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "archStream: {} uarchStream: {} HOST_STREAM_WAIT_FOR_{}_COMP Outer Buffer ci: {} pi: {} next_pi: {} "
                  "watermark: {}",
                  archStream,
                  uarchStream,
                  stream_name,
                  wait_outer_queue_obj->getCi(),
                  wait_outer_queue_obj->getPi(),
                  wait_outer_queue_obj->getNextPi(),
                  wait_outer_queue_obj->getWatermark());
    printQueueDFALog(archStream, uarchStream, wait_outer_queue, logger, stream_name);
}

void HclPublicStreams::printQueueDFALog([[maybe_unused]] unsigned          archStream,
                                        [[maybe_unused]] size_t            uarchStream,
                                        void*                              queue,
                                        hl_logger::LoggerSPtr              logger,
                                        [[maybe_unused]] const std::string stream_name)
{
    const unsigned   margin          = 8;
    spHostStreamFifo queue_obj       = *reinterpret_cast<spHostStreamFifo*>(queue);
    auto&            queue_buff      = queue_obj->getBuf();
    int              start_index     = (size_t)queue_obj->getCi() - margin;
    size_t           end_index       = (size_t)queue_obj->getPi() + margin;
    size_t           buffer_idx_diff = 0;
    const size_t     step            = 4;

    if (start_index < 0)
    {
        for (size_t idx = (size_t)queue_obj->getCapacity() + start_index; idx < queue_obj->getCapacity(); idx += step)
        {
            std::string out = fmt::format(FMT_COMPILE("[{:8},{:8}]:{:10x}|{:10x}|{:10x}|{:10x}"),
                                          idx,
                                          idx + 3,
                                          queue_buff[idx],
                                          queue_buff[idx + 1],
                                          queue_buff[idx + 2],
                                          queue_buff[idx + 3]);

            HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", out);
        }
        start_index = 0;
    }
    else if (queue_obj->getCi() > queue_obj->getPi())
    {
        for (size_t idx = start_index; idx < (size_t)queue_obj->getCapacity(); idx += step)
        {
            std::string out = fmt::format(FMT_COMPILE("[{:8},{:8}]:{:10x}|{:10x}|{:10x}|{:10x}"),
                                          idx,
                                          idx + 3,
                                          queue_buff[idx],
                                          queue_buff[idx + 1],
                                          queue_buff[idx + 2],
                                          queue_buff[idx + 3]);

            HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", out);
        }
        start_index = 0;
    }
    if (end_index > queue_obj->getCapacity())
    {
        buffer_idx_diff = end_index - queue_obj->getCapacity();
        end_index       = queue_obj->getCapacity();
    }

    for (size_t idx = start_index; (size_t)start_index < end_index; start_index += step)
    {
        std::string out = fmt::format(FMT_COMPILE("[{:8},{:8}]:{:10x}|{:10x}|{:10x}|{:10x}"),
                                      idx,
                                      idx + 3,
                                      queue_buff[idx],
                                      queue_buff[idx + 1],
                                      queue_buff[idx + 2],
                                      queue_buff[idx + 3]);

        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", out);
    }
    for (size_t idx = 0; idx < buffer_idx_diff; idx += step)
    {
        std::string out = fmt::format(FMT_COMPILE("[{:8},{:8}]:{:10x}|{:10x}|{:10x}|{:10x}"),
                                      idx,
                                      idx + 3,
                                      queue_buff[idx],
                                      queue_buff[idx + 1],
                                      queue_buff[idx + 2],
                                      queue_buff[idx + 3]);

        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", out);
    }
}

static void dumpQpWqes(IHclDevice* device, int nic, uint32_t qp, hl_logger::LoggerSPtr logger)
{
    if (!GCFG_HCL_DFA_DUMP_WQE.value())
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "wqe dump not enabled");
        return;
    }

    // reset wqe_index
    const int          accelNum = getAccelNum(device);
    const std::string& wqe_index_reset_path =
        fmt::format(FMT_COMPILE("/sys/class/infiniband/hbl_{}/wq/ports/{}/qp/{}/reset"), accelNum, nic, qp);

    std::FILE* wqe_index_reset_file = fopen(wqe_index_reset_path.c_str(), "r");
    if (nullptr == wqe_index_reset_file)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Failed opening reset wqe index file: {}", wqe_index_reset_path);
    }

    static constexpr int BUFF_SIZE = 4 * 1024;
    std::vector<char>    wqe_reset(BUFF_SIZE);
    if (0 == fread(wqe_reset.data(), sizeof(char), BUFF_SIZE, wqe_index_reset_file))
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Failed to reset wqe index: {}", wqe_index_reset_path);
    }
    fclose(wqe_index_reset_file);

    // create wqe dump file name
    const std::string& wqe_dump_path =
        fmt::format(FMT_COMPILE("/sys/class/infiniband/hbl_{}/wq/ports/{}/qp/{}/show"), accelNum, nic, qp);

    // start wqe dump
    HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "----------------- wqe dump -----------------");

    // read sysfs max_pi times
    for (uint64_t wqeIndex = 0; wqeIndex < device->getSenderWqeTableSize(); wqeIndex++)
    {
        std::FILE* wqe_dump_file = fopen(wqe_dump_path.c_str(), "r");
        if (nullptr == wqe_dump_file)
        {
            HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Failed opening wqe_dump_file: {}", wqe_dump_path);
            return;
        }

        std::vector<char> wqe_data(BUFF_SIZE);
        if (0 == fread(wqe_data.data(), sizeof(char), BUFF_SIZE, wqe_dump_file))
        {
            HLLOG_UNTYPED(logger,
                          HLLOG_LEVEL_ERROR,
                          "Failed reading wqe data for nic {} qp {} wqe {}",
                          nic,
                          qp,
                          wqeIndex);
            fclose(wqe_dump_file);
            return;
        }
        fclose(wqe_dump_file);

        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", wqe_data.data());
    }
    HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "----------------- wqe dump end -----------------");

    // reset wqe_index again
    wqe_index_reset_file = fopen(wqe_index_reset_path.c_str(), "r");
    if (nullptr == wqe_index_reset_file)
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Failed opening reset wqe index file: {}", wqe_index_reset_path);
        return;
    }

    if (0 == fread(wqe_reset.data(), sizeof(char), BUFF_SIZE, wqe_index_reset_file))
    {
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_ERROR, "Failed to reset wqe index: {}", wqe_index_reset_path);
    }
    fclose(wqe_index_reset_file);
}

static void
dumpQpContext(IHclDevice* iDev, int nic, const std::vector<uint32_t>& qpList, const DfaLoggersV3& dfaLoggers)
{
    hl_logger::LoggerSPtr logger = dfaLoggers.dfaNicInfoLogger;
    const int             fd     = iDev->getFd();

    constexpr int     BUFF_SIZE = 4 * 1024;
    std::vector<char> buff(BUFF_SIZE);

    for (auto qp : qpList)
    {
        for (int req = 0; req < 2; req++)
        {
            std::string header = fmt::format(FMT_COMPILE("---- port qp req: {} {} {}"), nic, qp, req);

            int res = hlthunk_nic_dump_qp(fd, nic, qp, req, buff.data(), buff.size());
            if (res != 0)
            {
                HLLOG_UNTYPED(logger,
                              HLLOG_LEVEL_ERROR,
                              "Failed reading qp status for {} with res {} errno {}",
                              header,
                              res,
                              errno);
                continue;
            }
            HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}\n{}", header, buff.data());
            if (req == 0) continue;

            dumpQpWqes(iDev, nic, qp, logger);
        }
    }
}

void HclPublicStreams::dfaLogCommInfo(IHclDevice* iDev, DfaLoggersV3& dfaLoggers)
{
    hl_logger::LoggerSPtr logger = dfaLoggers.dfaSynDevFailLogger;

    HLLOG_UNTYPED(logger,
                  HLLOG_LEVEL_INFO,
                  "============================ HCCL communicators "
                  "================================================================");
    HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "My moduleId {}", iDev->getDeviceConfig().getHwModuleId());

    for (unsigned comm = 0; comm < iDev->getMaxCommNum(); comm++)
    {
        if (!iDev->isCommExist(comm))
        {
            continue;
        }

        HclDynamicCommunicator&   hclDynamicCommunicator = iDev->getComm(comm);
        HCL_Rank                  myRank                 = hclDynamicCommunicator.getMyRank();
        RankInfo&                 rankInfo               = hclDynamicCommunicator.m_rankInfo;
        const UniqueSortedVector& rankVector             = hclDynamicCommunicator.getRanks();

        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "");
        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "comm {} myRank {} num-ranks {}", comm, myRank, rankVector.size());
        HLLOG_UNTYPED(logger,
                      HLLOG_LEVEL_INFO,
                      "------------------------------------------------------------------------------");

        hclDynamicCommunicator.m_dfaData.addDfaLog(logger, hclDynamicCommunicator.getApiCounters());

        for (uint8_t nic = 0; nic < iDev->getHal().getMaxNics(); nic++)
        {
            bool nicHeaderLogged = false;

            for (auto rank : rankVector)
            {
                // skip same rank - no open QPs
                if (rank != myRank)
                {
                    int                   validQps = 0;
                    std::vector<uint32_t> qpNums;
                    std::string           qpList;

                    const RankInfoHeader& remoteRankHeader = hclDynamicCommunicator.getRemoteConnectionHeader(rank);

                    // only 3/6 nics out of 24 may be connected, check if current nic is connected
                    for (uint8_t activeNic : iDev->getActiveNics(myRank, rank, 1, comm))
                    {
                        if (activeNic == nic)
                        {
                            for (uint8_t qpSet = 0; qpSet < MAX_QPS_SETS_PER_CONNECTION; qpSet++)
                            {
                                for (uint32_t j = 0; j < QPS_ARRAY_LENGTH; j++)
                                {
                                    NicQPs& nicQPs = rankInfo.remoteInfo[rank].gaudiNicQPs[activeNic];
                                    if (nicQPs.qp[qpSet][j] != 0)
                                    {
                                        qpNums.push_back(nicQPs.qp[qpSet][j]);
                                        if (!nicHeaderLogged)
                                        {
                                            HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "--- nic {} ---", nic);
                                            nicHeaderLogged = true;
                                        }

                                        if (validQps == 0)  // first in this nic
                                        {
                                            std::string_view remoteName(remoteRankHeader.hostname);
                                            qpList = fmt::format("    Rank {:4} hwModuleID {} name {}, QPs: ",
                                                                 rank,
                                                                 remoteRankHeader.hwModuleID,
                                                                 remoteName);
                                        }

                                        qpList += fmt::format(FMT_COMPILE("{:6}"), nicQPs.qp[qpSet][j]);
                                        validQps++;
                                        if (hclDynamicCommunicator.isDfaNicExists(nic, rank))
                                        {
                                            NicQPs* dfaNicQPs =
                                                &(hclDynamicCommunicator.m_backupRankQPs[rank][activeNic]);
                                            qpNums.push_back(dfaNicQPs->qp[qpSet][j]);
                                            qpList += fmt::format(FMT_COMPILE("{:6}"), dfaNicQPs->qp[qpSet][j]);
                                            validQps++;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (!qpList.empty())
                    {
                        HLLOG_UNTYPED(logger, HLLOG_LEVEL_INFO, "{}", qpList);
                    }
                    dumpQpContext(iDev, nic, qpNums, dfaLoggers);
                }
            }
        }
    }
}
