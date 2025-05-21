#include "infra/scal/gaudi2/scal_manager.h"

#include <cstdint>                    // for uint64_t
#include "gaudi2_arc_host_packets.h"  // for gaudi2 FW COMP_SYNC_GROUP_CMAX_TARGET
#include "hcl_utils.h"
#include "infra/scal/gaudi2/scal_wrapper.h"               // for Gaudi2ScalWrapper
#include "infra/scal/gen2_arch_common/scal_wrapper.h"     // for Gen2ArchScalWr...
#include "platform/gaudi2/commands/hcl_commands.h"        // for HclCommandsGaudi2
#include "platform/gaudi2/hcl_device.h"                   // for HclDeviceGaudi2
#include "platform/gen2_arch_common/hcl_packets_utils.h"  // for getCompCfg
#include "infra/scal/gen2_arch_common/scal_exceptions.h"
#include "platform/gen2_arch_common/simb_pool_container_allocator.h"

class HclCommandsGen2Arch;
class HclDeviceGen2Arch;
namespace hcl
{
class ScalStreamBase;
}

using namespace hcl;

Gaudi2ScalManager::Gaudi2ScalManager(int fd, HclCommandsGen2Arch& commands, Gen2ArchStreamLayout& streamLayout)
: Gen2ArchScalManager(fd, commands)
{
    if (fd == -1) return;
    m_scalWrapper.reset(new Gaudi2ScalWrapper(fd, m_scalNames));
    init(streamLayout, CyclicBufferType::GAUDI2);
}

Gaudi2ScalManager::~Gaudi2ScalManager() {}

// return the gaudi2 value from QMAN FW gaudi2_arc_host_packets.h
uint32_t Gaudi2ScalManager::getCMaxTargetValue()
{
    return COMP_SYNC_GROUP_CMAX_TARGET;
}
