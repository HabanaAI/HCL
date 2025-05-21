#include "platform/gaudi_common/hcl_device_controller.h"

HclDeviceControllerGaudiCommon::HclDeviceControllerGaudiCommon(const unsigned numOfStreams)
: HclDeviceControllerGen2Arch(numOfStreams)
{
}

void HclDeviceControllerGaudiCommon::addStreamWaitOnExternalEvent(hcl::ScalStream& scalStream, unsigned uarchStreamId)
{
    unsigned archStreamIdx = scalStream.getArchStreamIndex();
    unsigned schedIdx      = scalStream.getSchedIdx();
    // Apply the pending waits that were requested in previous eventWait calls.
    m_graphSync[archStreamIdx]->addStreamWaitOnLongSo(
        scalStream,
        archStreamIdx,
        m_streamSyncParams[archStreamIdx].m_smInfo.longMonitorSmIndex,
        getLongMonitorIdx(archStreamIdx, schedIdx, uarchStreamId),
        m_streamSyncParams[archStreamIdx].m_schedulers[schedIdx].streams[uarchStreamId],
        getFenceIdx(archStreamIdx, uarchStreamId, FENCE_MONITOR_IDX));
}

/*
Currently only used for debug
*/
void HclDeviceControllerGaudiCommon::addStreamWaitOnExternalEvent(hcl::ScalStream& scalStream,
                                                                  uint64_t         soValue,
                                                                  unsigned         soIdx)
{
    unsigned archStreamIdx = scalStream.getArchStreamIndex();
    unsigned schedIdx      = scalStream.getSchedIdx();
    unsigned uarchStreamId = scalStream.getUarchStreamIndex();

    m_graphSync[archStreamIdx]->addStreamWaitOnLongSo(scalStream,
                                                      m_streamSyncParams[archStreamIdx].m_smInfo.longMonitorSmIndex,
                                                      getLongMonitorIdx(archStreamIdx, schedIdx, uarchStreamId),
                                                      soValue,
                                                      soIdx,
                                                      getFenceIdx(archStreamIdx, uarchStreamId, FENCE_MONITOR_IDX));
}
