
#include "platform/gaudi3/hcl_collective_routines.h"

#include "hcl_api_types.h"
#include "hcl_dynamic_communicator.h"               // for HclDynamicComm...
#include "infra/scal/gaudi3/scal_utils.h"           // for Gaudi3HclScalUtils
#include "hcl_log_manager.h"                        // for LOG_*
#include "platform/gaudi3/commands/hcl_commands.h"  // for HclCommandsGaudi3
#include "platform/gaudi3/hcl_device.h"             // for HclDeviceGaudi3
#include "platform/gaudi3/hcl_graph_sync.h"         // for HclGraphSyncGa...
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gen2_arch_common/hcl_device.h"            // for HclDeviceGen2Arch
#include "platform/gen2_arch_common/hcl_graph_sync.h"        // for HclGraphSyncGe...
#include "platform/gen2_arch_common/send_recv_aggregator.h"  // for SendRecvEntry
#include "platform/gen2_arch_common/scaleout_provider.h"     // for ScaleoutProvider
#include "hcl_math_utils.h"                                  // for mod
#include "platform/gaudi3/send_recv_aggregator.h"            // for SendRecvAggregatorGaudi3
#include "platform/gaudi3/hcl_mem_handler.h"
#include "platform/gaudi3/hcl_address_generator.h"            // for HclAddressGeneratorGaudi3
#include "platform/gaudi3/gaudi3_base_server_connectivity.h"  // for Gaudi3BaseServerConnectivity
#include "platform/gen2_arch_common/server_def.h"             // for Gen2ArchServerDef

class DeviceSimbPoolManagerBase;
class ScaleoutProvider;

HclCollectiveRoutinesGaudi3::HclCollectiveRoutinesGaudi3(HclDeviceGaudi3* device,
                                                         int              archStreamIdx,
                                                         WqeTracker*      wqeTracker)
: HclCollectiveRoutinesGen2Arch(device, archStreamIdx, wqeTracker),
  m_gaudi3Commands((HclCommandsGaudi3&)m_commands),
  m_serverConnectivity(device->getServerConnectivityGaudi3()),
  m_sendAggr(true,
             getDevice().getDeviceConfig().getHwModuleId(),
             device->getServerConnectivityGaudi3(),
             device->getServerDef().getHwModules(),
             m_gaudi3Commands),
  m_recvAggr(false,
             getDevice().getDeviceConfig().getHwModuleId(),
             device->getServerConnectivityGaudi3(),
             device->getServerDef().getHwModules(),
             m_gaudi3Commands)
{
    m_addressGenerator    = std::make_unique<HclAddressGeneratorGaudi3>();
    m_memHandler          = std::make_unique<HclCollectiveMemHandlerGaudi3>(m_streamId,
                                                                   *m_addressGenerator,
                                                                   m_deviceSimbPoolManager,
                                                                   m_commands,
                                                                   m_graphSync);
    m_utils               = new hcl::Gaudi3HclScalUtils();
    m_remainderCalculator = new RemainderCalculatorGaudi3();
    initCollectiveRoutinesGen2Arch();
}

HclCollectiveRoutinesGaudi3::~HclCollectiveRoutinesGaudi3()
{
    if (m_utils) delete m_utils;
    m_utils = nullptr;
    if (m_remainderCalculator) delete m_remainderCalculator;
}

void HclCollectiveRoutinesGaudi3::createScaleUpSendRecvOp(hcl::ScalStreamBase&      scalStream,
                                                          const SendRecvArray&      sendRecvArray,
                                                          int                       selfModuleId,
                                                          HCL_Comm                  comm,
                                                          [[maybe_unused]] unsigned collectiveContextIndex,
                                                          uint32_t                  soAddress,
                                                          bool                      isSend,
                                                          [[maybe_unused]] bool     isLast,
                                                          [[maybe_unused]] bool     notifyRndvAck,
                                                          [[maybe_unused]] bool     waitForRndvAcks)
{
    HclDynamicCommunicator& dynamicComm = m_device->getComm(comm);
    HclDeviceGaudi3*        device      = dynamic_cast<HclDeviceGaudi3*>(m_device);
    VERIFY(device != nullptr);

    QPUsage qpUsage = device->getBaseQpAndUsage(dynamicComm,
                                                eHCLNoCollective,
                                                isSend,
                                                false,
                                                false,
                                                false,
                                                INVALID_COUNT,
                                                INVALID_COUNT,
                                                m_boxType);

    SobInfo sobInfo = ((hcl::Gaudi3HclScalUtils*)(m_utils))->getSOBInfo(soAddress);

    for (const SendRecvEntry& entry : sendRecvArray)
    {
        if (entry.isValid)
        {
            LOG_HCL_TRACE(HCL, "Calculating port mask for rank {}", entry.remoteRank);
            m_serverConnectivity.getRankToPortMask(entry.remoteRank, dynamicComm);
        }
    }

    const RemoteDevicePortMasksVector& remoteDevicesPortMasks = m_serverConnectivity.getRemoteDevicesPortMasks(comm);
    size_t                             index                  = 0;
    for (auto& remoteDevicePortMask : remoteDevicesPortMasks)
    {
        LOG_HCL_TRACE(HCL,
                      "selfModuleId={}, remoteDevicesPortMasks[{}]=0x{:x}",
                      selfModuleId,
                      index++,
                      remoteDevicePortMask);
    }

    m_gaudi3Commands.serializeScaleUpSendRecv(scalStream,
                                              selfModuleId,
                                              isSend,
                                              sobInfo.dcore,
                                              sobInfo.ssm,
                                              sobInfo.sobId,
                                              qpUsage.qpn,
                                              sendRecvArray,
                                              remoteDevicesPortMasks,
                                              comm,
                                              isSend ? m_sendAggr : m_recvAggr,
                                              m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(comm));
}

void HclCollectiveRoutinesGaudi3::createScaleUpCollectiveOp(hcl::ScalStreamBase& scalStream,
                                                            ScaleUpCollectiveOp& scaleUpCollectiveOp)
{
    ScaleUpCollectiveOpG3 scaleUpOp {scaleUpCollectiveOp};
    HclDeviceGaudi3*      device = dynamic_cast<HclDeviceGaudi3*>(m_device);
    VERIFY(device != nullptr);

    QPUsage qpUsage = device->getBaseQpAndUsage(scaleUpOp.m_dynamicComm,
                                                scaleUpOp.m_collectiveOp,
                                                scaleUpOp.m_isSend,
                                                scaleUpOp.m_isComplexCollective,
                                                scaleUpOp.m_isReductionInIMB,
                                                scaleUpOp.m_isHierarchical,
                                                scaleUpOp.m_count,
                                                scaleUpOp.m_cellCount,
                                                m_boxType,
                                                false,
                                                HCL_INVALID_RANK,
                                                SINGLE_QP_SET_INDEX,
                                                scaleUpOp.m_isReduction,
                                                scaleUpOp.m_complexCollective,
                                                scaleUpOp.m_isRoot);

    SobInfo sobInfo        = ((hcl::Gaudi3HclScalUtils*)(m_utils))->getSOBInfo(scaleUpOp.m_soAddress);
    bool    doPortMaskCalc = (scaleUpOp.m_collectiveOp == eHCLSimpleBroadcast && !scaleUpOp.m_isSend) ||
                          (scaleUpOp.m_collectiveOp == eHCLScatter && !scaleUpOp.m_isSend) ||
                          (scaleUpOp.m_collectiveOp == eHCLGather && scaleUpOp.m_isSend);

    // for gather, send is always from offset 0 -> disregardRank = 1 which sets also last_rank = 1
    // overcome this by eliminating remainder for !last_rank ranks
    if (scaleUpOp.m_collectiveOp == eHCLGather && scaleUpOp.m_isSend &&
        scaleUpOp.m_dynamicComm.getMyRank() != scaleUpOp.m_dynamicComm.getScaleUpLastRank())
    {
        scaleUpOp.m_hasBufferSize = false;
    }

    scaleUpOp.m_dcore            = sobInfo.dcore;
    scaleUpOp.m_ssm              = sobInfo.ssm;
    scaleUpOp.m_sobId            = sobInfo.sobId;
    scaleUpOp.m_ScaleupGroupSize = scaleUpOp.m_dynamicComm.getScaleupGroupSize();
    scaleUpOp.m_qpn              = qpUsage.qpn;
    scaleUpOp.m_disregardRank    = qpUsage.disregardRank;
    scaleUpOp.m_ports_mask       = doPortMaskCalc
                                       ? m_serverConnectivity.getDeviceToRemoteIndexPortMask(scaleUpOp.m_dynamicComm,
                                                                                       scaleUpOp.m_deviceToRemoteIndex)
                                       : m_serverConnectivity.getInnerRanksPortMask(scaleUpOp.m_dynamicComm);
    scaleUpOp.m_strideCount =
        (scaleUpOp.m_isReduction && !scaleUpOp.m_isSend)
            ? sizeToCount(m_deviceSimbPoolManager.getSingleBufferSize(SCALEUP_AND_ALL2ALL_POOL), scaleUpOp.m_dataType)
            : scaleUpOp.m_strideCount;

    m_gaudi3Commands.serializeScaleUpCollectiveOp(
        scalStream,
        scaleUpOp,
        m_serverConnectivity.getMaxNumScaleUpPortsPerConnection(scaleUpOp.m_dynamicComm));
}

void HclCollectiveRoutinesGaudi3::createScaleOutCollectiveOp(hcl::ScalStreamBase&  scalStream,
                                                             ScaleOutCollectiveOp& scaleOutCollectiveOp)
{
    ScaleOutCollectiveOpG3 scaleOutOpG3 {scaleOutCollectiveOp};

    SobInfo          sobInfo       = ((hcl::Gaudi3HclScalUtils*)(m_utils))->getSOBInfo(scaleOutOpG3.m_soAddress);
    auto&            m_dynamicComm = m_device->getComm(scaleOutOpG3.m_comm);
    HclDeviceGaudi3* device        = dynamic_cast<HclDeviceGaudi3*>(m_device);
    VERIFY(device != nullptr);

    QPUsage qpUsage = device->getBaseQpAndUsage(m_dynamicComm,
                                                scaleOutOpG3.m_collectiveOp,
                                                scaleOutOpG3.m_isSend,
                                                false,
                                                scaleOutOpG3.m_isReductionInIMB,
                                                true,
                                                scaleOutOpG3.m_count,
                                                scaleOutOpG3.m_cellCount,
                                                m_boxType,
                                                true,
                                                scaleOutOpG3.m_remoteRank,
                                                scaleOutOpG3.m_qpSet);

    scaleOutOpG3.m_dcore            = sobInfo.dcore;
    scaleOutOpG3.m_ssm              = sobInfo.ssm;
    scaleOutOpG3.m_sobId            = sobInfo.sobId;
    scaleOutOpG3.m_ScaleupGroupSize = m_dynamicComm.getScaleupGroupSize();
    scaleOutOpG3.m_qpn              = qpUsage.qpn;
    scaleOutOpG3.m_disregardRank    = qpUsage.disregardRank;
    scaleOutOpG3.m_ports_mask       = m_dynamicComm.getCommConnectivity().getExternalPortsMask();
    scaleOutOpG3.m_lagSize          = m_dynamicComm.getCommConnectivity().getNumScaleOutPorts();

    m_gaudi3Commands.serializeScaleOutCollectiveOp(scalStream, scaleOutOpG3);
}

unsigned HclCollectiveRoutinesGaudi3::countScaleUpSignalsSendRecv(CommonState&   commonState,
                                                                  const uint32_t numberOfSendBuckets,
                                                                  const uint32_t numberOfRecvBuckets,
                                                                  const uint32_t numberOfSends,
                                                                  const uint32_t numberOfRecvs,
                                                                  const HCL_Comm comm)
{
    unsigned numSignals = getDevice().getServerConnectivity().getMaxNumScaleUpPortsPerConnection(comm);
    if (commonState.m_dynamicComm.getCommSize() == 1 && !commonState.m_isMultiScaleupGroup)
    {
        numSignals = 0;
    }

    numSignals = numSignals * (numberOfSends + (2 * numberOfRecvs));
    // When using RNDV ack signaling we need to multiply the numberOfRecvs by 2
    // nicsPerConnection for data and nicsPerConnection for acks
    LOG_HCL_TRACE(HCL,
                  "numberOfSendBuckets={}, numberOfRecvBuckets={}, numberOfSends={}, numberOfRecvs={}, numSignals={}",
                  numberOfSendBuckets,
                  numberOfRecvBuckets,
                  numberOfSends,
                  numberOfRecvs,
                  numSignals);
    return numSignals;
}

unsigned HclCollectiveRoutinesGaudi3::countScaleOutSignalsSendRecv(const uint32_t numberOfSends,
                                                                   const uint32_t numberOfRecvs,
                                                                   const HCL_Comm comm)
{
    const unsigned signalsPerRecv = m_scaleoutProvider->isHostNic() ? 1 : 2;  // GNICs require additional signal for ACK
    const unsigned scaleoutSignals =
        (numberOfSends + numberOfRecvs * signalsPerRecv) * m_scaleoutProvider->getNumOfNicsPerDevice(comm);
    LOG_HCL_TRACE(HCL,
                  "numberOfSends={}, numberOfRecvs={}, scaleoutSignals={}",
                  numberOfSends,
                  numberOfRecvs,
                  scaleoutSignals);
    return scaleoutSignals;
}

uint64_t RemainderCalculatorGaudi3::getBufferClearSize(HCL_CollectiveOp collective,
                                                       uint64_t         originalSize,
                                                       e_devicePoolID   bufferId,
                                                       uint64_t         dataTypeSize,
                                                       uint64_t         scaleOutSendCount,
                                                       uint64_t         scaleOutRecvCount,
                                                       bool             isRoot,
                                                       bool             isBf16Reduction,
                                                       BoxNumInfo&      sendBoxNumInfo,
                                                       uint64_t         rootBox)
{
    VERIFY(sendBoxNumInfo.m_orientation == BoxNumInfo::boxOrientation::NEXT_BOX);
    if (collective == eHCLAllReduce)
    {
        return scaleOutSendCount * dataTypeSize;
    }
    if (collective == eHCLReduce)
    {
        if (bufferId == SCALEOUT_POOL)
        {
            if (isBf16Reduction || isRoot)
            {
                return scaleOutRecvCount * dataTypeSize;
            }
            return scaleOutSendCount * dataTypeSize;
        }
        if (bufferId == REDUCE_POOL)
        {
            if (sendBoxNumInfo.m_boxNum == rootBox)
            {
                return scaleOutSendCount * dataTypeSize;
            }
            return scaleOutRecvCount * dataTypeSize;
        }
    }
    return originalSize;
}

uint64_t RemainderCalculatorGaudi3::getBoxCount(uint64_t                  nonRemainderBoxCount,
                                                uint64_t                  numBoxes,
                                                [[maybe_unused]] uint64_t ScaleupGroupSize,
                                                uint64_t                  boxIndex,
                                                [[maybe_unused]] uint64_t scaleUpCount,
                                                uint64_t                  remainderCount)
{
    if (boxIndex == (numBoxes - 1))
    {
        return nonRemainderBoxCount + remainderCount;
    }
    return nonRemainderBoxCount;
}

uint64_t RemainderCalculatorGaudi3::getScaleOutCount(uint64_t                  nonRemainderScaleOutCount,
                                                     uint64_t                  numBoxes,
                                                     [[maybe_unused]] uint64_t boxCount,
                                                     uint64_t                  boxIndex,
                                                     [[maybe_unused]] uint64_t myRankInScaleupGroup,
                                                     [[maybe_unused]] uint64_t scaleUpCount,
                                                     uint64_t                  remainderCount,
                                                     bool                      lastRankInScaleupGroup)
{
    if (boxIndex == (numBoxes - 1) && lastRankInScaleupGroup)
    {
        return nonRemainderScaleOutCount + remainderCount;
    }
    return nonRemainderScaleOutCount;
}

uint64_t RemainderCalculatorGaudi3::getDiv(uint64_t a, uint64_t b)
{
    return div(a, b);
}

uint64_t RemainderCalculatorGaudi3::getRemainderCount(uint64_t totalCount, uint64_t scaleUpCount, uint64_t commSize)
{
    return totalCount - (scaleUpCount * commSize);
}

bool RemainderCalculatorGaudi3::isValidSlicing(uint32_t originalBufferCount,
                                               uint32_t sliceCount,
                                               uint64_t collectiveCount,
                                               uint32_t numSlices,
                                               uint32_t numRanks,
                                               uint32_t minBufferCount)
{
    uint64_t lastSliceCount = mod(collectiveCount, sliceCount * numRanks);
    uint64_t countPerRank   = this->getDiv(lastSliceCount, numRanks);
    uint64_t lastRankCount  = countPerRank + (lastSliceCount - (countPerRank * numRanks));

    if (lastRankCount > originalBufferCount || (sliceCount < minBufferCount && numSlices > 1))
    {
        return false;
    }
    return true;
}

bool RemainderCalculatorGaudi3::isSlicing(uint64_t totalCount,
                                          uint64_t totalCountPerRank,
                                          uint32_t bufferCount,
                                          uint32_t numRanks)
{
    if (totalCount > totalCountPerRank)
    {
        // add remainder to get count for last rank
        totalCountPerRank += (totalCount - (totalCountPerRank * numRanks));
    }

    return totalCountPerRank > bufferCount;
}
