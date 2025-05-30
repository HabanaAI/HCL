#pragma once

#include <cstdint>  // for uint64_t, uint32_t
#include <memory>   // for allocator, unique_ptr
#include <set>      // for set
#include <string>   // for string

#include "hcl_exceptions.h"          // for HclException
#include "scal.h"                    // for scal_handle_t, scal_comp_group_handle_t
#include "hl_logger/hllog_core.hpp"  // for hl_logger::LoggerSPtr

#ifndef HCL_API_CALL
#define HCL_API_CALL __attribute__((visibility("default")))
#endif

#define SIZE_IN_DWORDS(type) (sizeof(type) >> 2)

struct DfaStatus;
struct DfaLoggersV3;

extern bool tdrDetectionFlag;

class IHclDevice;

namespace hcl
{
class ApiException : public HclException
{
public:
    ApiException(int fd = -1, const std::string& msg = "") : HclException(msg), m_fd(fd) {}

    int getFd() const noexcept { return m_fd; }

private:
    int m_fd;
};

class NotImplementedApiException : public ApiException
{
public:
    NotImplementedApiException(const std::string& apiName);
};

struct syncInfo
{
    uint64_t                 long_so_index;
    uint64_t                 targetValue;
    scal_comp_group_handle_t cp_handle = nullptr;
};

struct InternalHclStreamHandle;

typedef InternalHclStreamHandle* hclStreamHandle;

int getStreamID(hclStreamHandle stream);

class HCL_API_CALL HclPublicStreams
{
public:
    //!
    /*!
    ***************************************************************************************************
    *   @brief Initiate scal in HCL
    *
    *   Provide HCL the scal handle that was initalized by the RT per file descriptor
    *
    ***************************************************************************************************
    */
    HclPublicStreams(scal_handle_t scal);
    ~HclPublicStreams();

    //!
    /*!
    ***************************************************************************************************
    *   @brief Create a stream
    *
    *   Return a completionGroup that represents user stream handle
    *
    *   @return                  handle which represents the stream by hcl
    ***************************************************************************************************
    */
    hclStreamHandle createStream();

    //!
    /*!
    ***************************************************************************************************
    *   @brief Destroy a stream
    *
    *
    *   @param hclStreamHandle      [in]  Stream to destroy
    *
    *   @return                     hcl stream handle
    ***************************************************************************************************
    */
    void destroyStream(hclStreamHandle streamHandle);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Records an event (configure an incrementation long SO after engine done)
    *
    *
    *   @param streamHandle      [in]  Stream to record
    *   @param isCollectTime     [in]  Collect time activation, optional, default = false;
    *   @param timestampHandle   [in]  Time stamp handle, optional for being used by synElapsedTime, default = 0;
    *   @param timestampsOffset  [in]  Time stamp offset, optional for being used by synElapsedTime, default = 0;
    *
    *   @return                  syncInfo struct that holds the scal compilation group and long SO data
    ***************************************************************************************************
    */
    syncInfo eventRecord(hclStreamHandle streamHandle,
                         bool            isCollectTime    = false,
                         uint64_t        timestampHandle  = 0,
                         uint32_t        timestampsOffset = 0);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Makes a stream wait for a completion group long SO to get to a certain  value.
    *
    *   Makes all future work submitted to streamHandle wait for completion of the params.
    *   params is the return value of the eventRecord method.
    *
    *   @param streamHandle      [in]  Stream to wait on.
    *   @param params            [in]  syncInfo struct.
    ***************************************************************************************************
    */
    void streamWaitEvent(hclStreamHandle streamHandle, syncInfo params);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Wait on stream to finish all sent jobs / Blocking
    *
    *   @param streamHandle      [in]  Stream to wait on.
    ***************************************************************************************************
    */
    void streamSynchronize(hclStreamHandle streamHandle);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Wait on event to finish all sent jobs / Blocking
    *
    *   @param params      [in]  syncInfo struct.
    ***************************************************************************************************
    */
    void eventSynchronize(syncInfo params);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Check if stream finished all sent jobs
    *
    *   @param streamHandle      [in]  Stream to wait on.
    *
    *   @return  True if event finished all jobs / False if still busy
    ***************************************************************************************************
    */
    bool streamQuery(hclStreamHandle streamHandle);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Check if event finished all sent jobs
    *
    *   @param params      [in]  syncInfo struct.
    *
    *   @return  True if event finished all jobs / False if still busy
    ***************************************************************************************************
    */
    bool eventQuery(syncInfo params);

    //!
    /*!
    ***************************************************************************************************
    *   @brief Raise the TDR detection flag
    ***************************************************************************************************
    */
    inline void tdrDetection() { tdrDetectionFlag = true; }

    bool DFA(DfaStatus& dfaStatus, void (*logFunc)(int, const char*));

    enum DfaLogPhase
    {
        Main = 0,
        Ccb  = 1
    };
    bool DFA(DfaStatus& dfaStatus, void (*logFunc)(int, const char*), DfaLogPhase options);

private:
    int getFreeStreamId();

    static void dfaLogCommInfo(IHclDevice* iDev, DfaLoggersV3& dfaLoggers);
    static void dfaLogHostFences(IHclDevice* iDev, hl_logger::LoggerSPtr logger);
    static void dfaLogCmdBuff(IHclDevice* iDev, hl_logger::LoggerSPtr logger);
    static bool dfaLogCcb(hl_logger::LoggerSPtr logger);
    static void printStreamQueuesDFALog(unsigned              archStream,
                                        size_t                uarchStream,
                                        void*                 inner_queue,
                                        void*                 outer_queue,
                                        void*                 send_wait_outer_queue,
                                        hl_logger::LoggerSPtr logger,
                                        const std::string     stream_name);
    struct CmdHandler
    {
        std::function<std::string(const void*)> function;
        size_t                                  cmdSizeInDwords;
    };

    static void
    printQueueDFALog(const hl_logger::LoggerSPtr logger, const void* queue, const std::vector<CmdHandler>& handlers);
    static std::string getRawQueueEntry(const void* address, const size_t size);
    static bool
    LogQueueEntry(const hl_logger::LoggerSPtr logger, const CmdHandler& handler, const size_t idx, const void* address);

    static std::string handleInnerQueueMsg(const void* address);
    static std::string handleOfiCompCallbackParams(const void* address);
    static std::string handle_host_sched_cmd_scale_out_nic_op(const void* address);
    static std::string handle_host_sched_cmd_scale_out_with_fence_nic_op(const void* address);
    static std::string handle_host_sched_cmd_wait_for_completion(const void* address);
    static std::string handle_host_sched_cmd_fence_wait(const void* address);
    static std::string handle_host_sched_cmd_signal_so(const void* address);

    bool logDfaMain(DfaStatus& dfaStatus, void (*logFunc)(int, const char*), DfaLoggersV3& dfaLoggers);

    std::set<int> m_freeStreams = {0, 1, 2, 3};
};

class HCL_API_CALL HclPublicStreamsFactory
{
public:
    static std::unique_ptr<HclPublicStreams> createHclPublicStreams(scal_handle_t scal);
};

}  // namespace hcl
