#include "platform/gaudi2/hcl_device_controller.h"
#include "platform/gaudi2/hcl_graph_sync.h"
#include "platform/gen2_arch_common/hcl_device.h"
#include "platform/gaudi2/commands/hcl_commands.h"  // for HclCommandsGaudi2
#include "infra/scal/gaudi2/scal_manager.h"         // for Gaudi2S...

HclDeviceControllerGaudi2::HclDeviceControllerGaudi2(int fd, int numOfStreams)
: HclDeviceControllerGen2Arch(numOfStreams)
{
    m_commands    = std::unique_ptr<HclCommandsGaudi2>(new HclCommandsGaudi2());
    m_scalManager = std::unique_ptr<hcl::Gaudi2ScalManager>(new hcl::Gaudi2ScalManager(fd, *m_commands));
    for (int i = 0; i < m_numOfStreams; i++)
    {
        m_streamSyncParams[i].m_smInfo = m_scalManager->getSmInfo(i);
        m_graphSync[i]                 = std::unique_ptr<HclGraphSyncGaudi2>(
            new HclGraphSyncGaudi2(m_streamSyncParams[i].m_smInfo.soSmIndex, *m_commands));
    }
}