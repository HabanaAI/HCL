#pragma once

#include <stdint.h>                                    // for uint64_t
#include <utility>                                     // for pair
#include <vector>                                      // for vector
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalMa...
#include "scal.h"                                      // for scal_handle_t
#include "platform/gaudi3/hcl_device.h"
#include "infra/scal/gen2_arch_common/stream_layout.h"

class HclCommandsGen2Arch;
class HclDeviceGen2Arch;
namespace hcl
{
class ScalStreamBase;
}

constexpr hcl::SchedulersIndex initCgSchedList[] = {hcl::SchedulersIndex::sendScaleUp,
                                                    hcl::SchedulersIndex::recvScaleUp,
                                                    hcl::SchedulersIndex::gp};

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
class Gaudi3ScalManager : public Gen2ArchScalManager
{
public:
    Gaudi3ScalManager(int fd, HclCommandsGen2Arch& commands, const Gen2ArchStreamLayout& streamLayout);
    Gaudi3ScalManager(Gaudi3ScalManager&&)                 = delete;
    Gaudi3ScalManager(const Gaudi3ScalManager&)            = delete;
    Gaudi3ScalManager& operator=(Gaudi3ScalManager&&)      = delete;
    Gaudi3ScalManager& operator=(const Gaudi3ScalManager&) = delete;
    virtual ~Gaudi3ScalManager();

    virtual uint32_t getCMaxTargetValue() override;

    virtual uint64_t getInitCgNextSo() override;
    int              getConfigurationCount() const { return m_configurationCount; };

private:
    int m_configurationCount = -1;
};

}  // namespace hcl