#pragma once

#include <cstdint>                                     // for uint64_t
#include <utility>                                     // for pair
#include <vector>                                      // for vector
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalMa...
#include "scal.h"                                      // for scal_handle_t
#include "infra/scal/gen2_arch_common/stream_layout.h"

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
 * Its responsible for all logic needed buy HCL and its the only contact to the scal SW layer.
 * It hold all static data: Arch Streams, Internal/External Compilation Groups, Sync Manager Info,
 * Memory pools, MicroArchStreams and its buffers.
 * It also responsible for managing cyclic buffers AKA MicroArchStreams
 */
class Gaudi2ScalManager : public Gen2ArchScalManager
{
public:
    Gaudi2ScalManager(int fd, HclCommandsGen2Arch& commands, Gen2ArchStreamLayout& streamLayout);
    Gaudi2ScalManager(Gaudi2ScalManager&&)                 = delete;
    Gaudi2ScalManager(const Gaudi2ScalManager&)            = delete;
    Gaudi2ScalManager& operator=(Gaudi2ScalManager&&)      = delete;
    Gaudi2ScalManager& operator=(const Gaudi2ScalManager&) = delete;
    virtual ~Gaudi2ScalManager();

    virtual uint32_t getCMaxTargetValue() override;
};

}  // namespace hcl