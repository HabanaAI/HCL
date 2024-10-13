#include "platform/gaudi3/hcl_device_controller.h"
#include "platform/gaudi3/hcl_graph_sync.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "platform/gen2_arch_common/wqe_tracker.h"
#include "platform/gaudi3/commands/hcl_commands.h"  // for HclCommandsGaudi3
#include "infra/scal/gaudi3/scal_manager.h"

HclDeviceControllerGaudi3::HclDeviceControllerGaudi3(const int fd, const unsigned numOfStreams)
: HclDeviceControllerGen2Arch(numOfStreams)
{
    m_commands    = std::unique_ptr<HclCommandsGaudi3>(new HclCommandsGaudi3());
    m_scalManager = std::unique_ptr<hcl::Gaudi3ScalManager>(new hcl::Gaudi3ScalManager(fd, *m_commands));

    for (unsigned i = 0; i < m_numOfStreams; i++)
    {
        m_streamSyncParams[i].m_smInfo = m_scalManager->getSmInfo(i);
        m_graphSync[i]                 = std::unique_ptr<HclGraphSyncGaudi3>(
            new HclGraphSyncGaudi3(m_streamSyncParams[i].m_smInfo.soSmIndex, *m_commands));
    }
}