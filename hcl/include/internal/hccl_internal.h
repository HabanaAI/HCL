#pragma once

#include <synapse_api_types.h>  // for synStreamHandle, synDeviceId
#include "hccl_types.h"         // for hcclResult_t
#include "dfa_defines.hpp"

struct hcclEventHandle
{
    uint64_t event  = 0;
    uint64_t index  = 0;
    uint64_t pIndex = 0;
};

const uint64_t HCCL_InfinityWait = 0xFFFFFFFFFFFFFFFF;

hcclResult_t hcclInitDevice(const synDeviceId deviceId);
hcclResult_t hcclInitDeviceGaudi(const synDeviceId deviceId);
hcclResult_t hcclDestroyDevice(const synDeviceId deviceId = 0);
hcclResult_t hcclEventRecord(hcclEventHandle* eventHandle, synStreamHandle streamHandle);
hcclResult_t hcclSynchronizeEvent(const hcclEventHandle& eventHandle, uint64_t microSeconds = HCCL_InfinityWait);
hcclResult_t hcclFlushSubmissions(synStreamHandle streamHandle);
hcclResult_t hcclFlushSubmissionsAllStreams();
hcclResult_t hcclSynchronizeStream(synStreamHandle streamHandle);
hcclResult_t hcclSynchronizeAllStreams();
hcclResult_t hcclDFA(DfaStatus& dfaStatus, void (*dfaLogFunc)(int, const char*));
hcclResult_t hcclDfaUpdateState(DfaPhase dfaPhase);
hcclResult_t hcclGetVersionString(char* pVersion, const unsigned len);
bool         hcclIsACcbHalfFull(const unsigned archStreamIdx);
void         hcclSetTraceMarker(const synStreamHandle stream_handle, uint32_t val);
