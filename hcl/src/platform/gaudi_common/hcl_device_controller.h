#pragma once

#include "platform/gen2_arch_common/hcl_device_controller.h"  // for HclDeviceControllerGen2Arch
#include "infra/scal/gen2_arch_common/scal_stream.h"          // for ScalStream"scal_stream.h"

class HclDeviceControllerGaudiCommon : public HclDeviceControllerGen2Arch
{
public:
    HclDeviceControllerGaudiCommon(const unsigned numOfStreams);
    ~HclDeviceControllerGaudiCommon()                                             = default;
    HclDeviceControllerGaudiCommon(const HclDeviceControllerGen2Arch&)            = delete;
    HclDeviceControllerGaudiCommon& operator=(const HclDeviceControllerGen2Arch&) = delete;

protected:
    /**
     * @brief Apply the pending waits that were requested in previous streamWaitEvent calls.
     * we apply a wait by arming a monitor and decrementing the fence on uarchStreamId
     * The monitor waits for the LSO to reach the value supplied by streamWaitEvent
     * 1. Arms a long monitor to look at the LSO index from m_pendingWaits
     * 2. Blocks the microArch stream by decrementing its fence counter.
     **/
    virtual void addStreamWaitOnExternalEvent(hcl::ScalStream& scalStream, unsigned uarchStreamId) override;

    /**
     * @brief
     * !!!Currently only used for debug
     * 1. Arms a long monitor to look at the LSO index from the sync manager
     * 2. Blocks the microArch stream by decrementing its fence counter.
     * once the LSO reaches its value, the monitor will increment the fence and free the stream.
     * 3. In the past this was used wait for scale out phase to finish before performing dma of the entire result
     */
    void addStreamWaitOnExternalEvent(hcl::ScalStream& scalStream, uint64_t soValue, unsigned soIdx);

};  // class HclDeviceControllerGaudiCommon