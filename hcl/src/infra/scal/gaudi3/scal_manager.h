#pragma once

#include <stdint.h>                                    // for uint64_t
#include <utility>                                     // for pair
#include <vector>                                      // for vector
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalMa...
#include "scal.h"                                      // for scal_handle_t
#include "platform/gaudi3/hcl_device.h"
class HclCommandsGen2Arch;
class HclDeviceGen2Arch;
namespace hcl
{
class ScalStreamBase;
}

namespace hcl
{
/**
 * @brief
 *
 * ScalManager is the API entry point to all Scal needs in HCL.
 * Its resposible for all logic needed buy HCL and its the only contact to the scal SW layer.
 * It hold all static data: Arch Streams, Internal/External Compilation Groups, Sync Manager Info,
 * Memory pools, MicroArchStreams and its buffers.
 * It also repsonsole for managing cyclic buffers AKA MicroArchStreams
 */
class Gaudi3ScalManager : public Gen2ArchScalManager
{
public:
    Gaudi3ScalManager(int fd, HclCommandsGen2Arch& commands);
    Gaudi3ScalManager(Gaudi3ScalManager&&)      = delete;
    Gaudi3ScalManager(const Gaudi3ScalManager&) = delete;
    Gaudi3ScalManager& operator=(Gaudi3ScalManager&&) = delete;
    Gaudi3ScalManager& operator=(const Gaudi3ScalManager&) = delete;
    virtual ~Gaudi3ScalManager();

    virtual void initSimb(HclDeviceGen2Arch* device, uint8_t apiID) override;
    virtual void configQps(HCL_Comm comm, HclDeviceGen2Arch* device) override;

protected:
    virtual void init() override;

private:
    virtual void configScaleupQps(HCL_Comm comm, HclDeviceGaudi3* device, bool isSend);
    void         waitOnCg(Gen2ArchScalWrapper::CgComplex& cgComplex, const uint64_t target);
    int          m_configurationCount = -1;
};

}  // namespace hcl