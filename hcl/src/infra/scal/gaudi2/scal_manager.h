#pragma once

#include <cstdint>                                     // for uint64_t
#include <utility>                                     // for pair
#include <vector>                                      // for vector
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalMa...
#include "scal.h"                                      // for scal_handle_t
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
class Gaudi2ScalManager : public Gen2ArchScalManager
{
public:
    Gaudi2ScalManager(int fd, HclCommandsGen2Arch& commands);
    Gaudi2ScalManager(Gaudi2ScalManager&&)      = delete;
    Gaudi2ScalManager(const Gaudi2ScalManager&) = delete;
    Gaudi2ScalManager& operator=(Gaudi2ScalManager&&) = delete;
    Gaudi2ScalManager& operator=(const Gaudi2ScalManager&) = delete;
    virtual ~Gaudi2ScalManager();

    void         initGlobalContext(HclDeviceGen2Arch* device, uint8_t api_id) override;
    virtual void serializeInitSequenceCommands(hcl::ScalStreamBase&                  recvStream,
                                               hcl::ScalStreamBase&                  recvSOStream,
                                               hcl::ScalStreamBase&                  dmaStream,
                                               unsigned                              indexOfCg,
                                               uint64_t                              soAddressLSB,
                                               const std::vector<sibAddressAndSize>& sibAddressesAndSizes,
                                               HclDeviceGen2Arch*                    device,
                                               uint8_t                               apiId);

protected:
    virtual void init() override;
};

}  // namespace hcl