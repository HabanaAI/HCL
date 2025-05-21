#include "infra/scal/gaudi3/scal_manager.h"

#include <cstdint>                           // for uint64_t
#include "gaudi3/gaudi3_arc_host_packets.h"  // for gaudi3 FW COMP_SYNC_GROUP_CMAX_TARGET
#include "infra/scal/gaudi3/scal_utils.h"
#include "infra/scal/gaudi3/scal_wrapper.h"  // for Gaudi3ScalWrapper
#include "infra/scal/gen2_arch_common/scal_exceptions.h"
#include "infra/scal/gen2_arch_common/scal_wrapper.h"  // for Gen2ArchScalWr...
#include "platform/gaudi3/commands/hcl_commands.h"     // for HclCommandsGaudi3
#include "platform/gaudi3/hcl_device.h"                // for HclDeviceGaudi3
#include "platform/gaudi3/hcl_graph_sync.h"
#include "platform/gaudi3/commands/hcl_commands.h"
#include "platform/gen2_arch_common/simb_pool_container_allocator.h"  // for getAllBufferBaseAddr, getSliceSize
#include "hcl_api_types.h"
#include "hcl_math_utils.h"
#include "platform/gen2_arch_common/hcl_packets_utils.h"  // for getCompCfg

class HclCommandsGen2Arch;  // lines 9-9
class HclDeviceGen2Arch;    // lines 10-10

namespace hcl
{
class ScalStreamBase;
}

using namespace hcl;

Gaudi3ScalManager::Gaudi3ScalManager(int fd, HclCommandsGen2Arch& commands, const Gen2ArchStreamLayout& streamLayout)
: Gen2ArchScalManager(fd, commands)
{
    if (fd == -1) return;
    m_scalWrapper.reset(new Gaudi3ScalWrapper(fd, m_scalNames));
    init(streamLayout, CyclicBufferType::GAUDI3);
}

Gaudi3ScalManager::~Gaudi3ScalManager() {}

uint64_t Gaudi3ScalManager::getInitCgNextSo()
{
    Gen2ArchScalWrapper::CgComplex cgComplex = m_scalWrapper->getCgInfo("network_scaleup_init_completion_queue");
    return (cgComplex.cgInfo.cgBaseAddr + (mod(++m_configurationCount, cgComplex.cgInfo.size) * 4));
}

// return the gaudi3 value from QMAN FW gaudi3_arc_host_packets.h
uint32_t Gaudi3ScalManager::getCMaxTargetValue()
{
    return COMP_SYNC_GROUP_CMAX_TARGET;
}
