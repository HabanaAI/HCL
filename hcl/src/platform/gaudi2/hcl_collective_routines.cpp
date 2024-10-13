#include "platform/gaudi2/hcl_collective_routines.h"

#include "hcl_dynamic_communicator.h"                  // for HclDynamicComm...
#include "infra/scal/gaudi2/scal_utils.h"              // for Gaudi2HclScalU...
#include "infra/scal/gen2_arch_common/scal_manager.h"  // for Gen2ArchScalMa...
#include "infra/scal/gen2_arch_common/scal_types.h"    // for SmInfo
#include "infra/scal/gen2_arch_common/scal_utils.h"    // for Gen2ArchScalUtils
#include "platform/gaudi2/commands/hcl_commands.h"     // for HclCommandsGaudi2
#include "platform/gaudi2/hcl_device.h"                // for HclDeviceGaudi2
#include "platform/gaudi2/hcl_graph_sync.h"            // for HclGraphSyncGa...
#include "platform/gaudi2/hcl_address_generator.h"     // for HclAddressGeneratorGaudi2
#include "platform/gen2_arch_common/collective_states.h"
#include "platform/gen2_arch_common/hcl_device.h"         // for HclDeviceGen2Arch
#include "platform/gen2_arch_common/hcl_graph_sync.h"     // for HclGraphSyncGe...
#include "hcl_log_manager.h"                              // for LOG_*
#include "platform/gen2_arch_common/scaleout_provider.h"  // for ScaleoutProvider
#include "hcl_math_utils.h"                               // for div_round_up
#include "hcl_collective_routines.h"
#include "platform/gaudi2/wqe_tracker.h"
#include "platform/gaudi2/hcl_mem_handler.h"
#include "platform/gen2_arch_common/hcl_packets_utils.h"    // for getEdmaStreamCtxtId
#include "platform/gen2_arch_common/server_connectivity.h"  // for Gen2ArchServerConnectivity
#include "platform/gen2_arch_common/server_def.h"           // for Gen2ArchServerDef

HclCollectiveRoutinesGaudi2::HclCollectiveRoutinesGaudi2(HclDeviceGaudi2* device, int streamId, WqeTracker* wqeTracker)
: HclCollectiveRoutinesGen2Arch(device, streamId, wqeTracker),
  m_sendRecvAggr(m_deviceController.getGen2ArchScalManager().getNicsScaleUpEngines(),
                 m_device->getServerConnectivity(),
                 m_commands),
  m_gaudi2Commands((HclCommandsGaudi2&)m_commands)
{
    LOG_HCL_TRACE(HCL, "Initializing collective routines");
    m_addressGenerator    = std::make_unique<HclAddressGeneratorGaudi2>(m_commands);
    m_memHandler          = std::make_unique<HclCollectiveMemHandlerGaudi2>(m_streamId,
                                                                   *m_addressGenerator,
                                                                   m_intermediateBufferManager,
                                                                   m_commands,
                                                                   m_graphSync);
    m_utils               = new hcl::Gaudi2HclScalUtils();
    m_remainderCalculator = new RemainderCalculatorGaudi2();
    initCollectiveRoutinesGen2Arch();
}

HclCollectiveRoutinesGaudi2::~HclCollectiveRoutinesGaudi2()
{
    if (m_utils) delete m_utils;
    if (m_remainderCalculator) delete m_remainderCalculator;
}

void HclCollectiveRoutinesGaudi2::createScaleUpSendRecvOp(hcl::ScalStreamBase& scalStream,
                                                          const SendRecvArray& sendRecvArray,
                                                          int                  selfModuleId,
                                                          HCL_Comm             comm,
                                                          unsigned             collectiveContextIndex,
                                                          uint32_t             soAddress,
                                                          bool                 isSend,
                                                          bool                 isLast,
                                                          bool                 notifyRndvAck,
                                                          bool                 waitForRndvAcks)
{
    m_gaudi2Commands.serializeScaleUpSendRecv(scalStream,
                                              ((HclDeviceGaudi2*)m_device)->getContextManager(),
                                              m_sendRecvAggr,
                                              sendRecvArray,
                                              selfModuleId,
                                              comm,
                                              collectiveContextIndex,
                                              soAddress,
                                              isSend,
                                              isLast,
                                              notifyRndvAck,
                                              waitForRndvAcks);
}

// collectiveContextIndex = isSend * 8 + streamIndex;

void HclCollectiveRoutinesGaudi2::createScaleUpCollectiveOp(hcl::ScalStreamBase& scalStream,
                                                            ScaleUpCollectiveOp& scaleUpCollectiveOp)
{
    ScaleUpCollectiveOpG2 scaleUpOpG2 {scaleUpCollectiveOp, ((HclDeviceGaudi2*)m_device)->getContextManager()};
    scaleUpOpG2.m_currentRank = scaleUpOpG2.m_dynamicComm.getMyRank();
    scaleUpOpG2.m_comm        = scaleUpOpG2.m_dynamicComm;
    scaleUpOpG2.m_numOfRanks  = (scaleUpOpG2.m_isReductionInIMB &&
                                (scaleUpOpG2.m_dataType == hcclBfloat16 || scaleUpOpG2.m_dataType == hcclFloat16) &&
                                scaleUpOpG2.m_isReduction && !scaleUpOpG2.m_isSend)
                                    ? 2
                                    : 0;
    scaleUpOpG2.m_poolId      = !scaleUpOpG2.m_isSend && scaleUpOpG2.m_isReduction
                                    ? DeviceBufferManager::getPoolSizeIndex(SCALEUP_AND_ALL2ALL_POOL)
                                    : 0;
    m_gaudi2Commands.serializeScaleUpCollectiveOp(scalStream, scaleUpOpG2);
}

void HclCollectiveRoutinesGaudi2::createScaleOutCollectiveOp(hcl::ScalStreamBase&  scalStream,
                                                             ScaleOutCollectiveOp& scaleOutCollectiveOp)
{
    ScaleOutCollectiveOpG2 scaleOutOpG2 {scaleOutCollectiveOp, ((HclDeviceGaudi2*)m_device)->getContextManager()};
    m_gaudi2Commands.serializeScaleOutCollectiveOp(scalStream, scaleOutOpG2);
}

unsigned HclCollectiveRoutinesGaudi2::countScaleUpSignalsSendRecv(CommonState&   commonState,
                                                                  const uint32_t numberOfSendBuckets,
                                                                  const uint32_t numberOfRecvBuckets,
                                                                  const uint32_t numberOfSends,
                                                                  const uint32_t numberOfRecvs,
                                                                  const HCL_Comm comm)
{
    const unsigned numScaleupPortsPerConnection =
        getDevice()->getServerConnectivity().getMaxNumScaleUpPortsPerConnection(comm);
    const unsigned boxSize    = getDevice()->getServerDef().getDefaultBoxSize();
    unsigned       numSignals = numScaleupPortsPerConnection * (boxSize - 1);
    if (commonState.m_dynamicComm.getScaleupGroupSize() == 1)
    {
        numSignals = 0;
    }
    else if (numberOfSendBuckets > 0 && numberOfRecvBuckets > 0)
    {
        numSignals *= 2;
    }
    LOG_HCL_TRACE(HCL,
                  "numberOfSendBuckets={}, numberOfRecvBuckets={}, numberOfSends={}, numberOfRecvs={}, numSignals={}",
                  numberOfSendBuckets,
                  numberOfRecvBuckets,
                  numberOfSends,
                  numberOfRecvs,
                  numSignals);
    return numSignals;
}

unsigned HclCollectiveRoutinesGaudi2::countScaleOutSignalsSendRecv(const uint32_t numberOfSends,
                                                                   const uint32_t numberOfRecvs,
                                                                   const HCL_Comm comm)
{
    const unsigned scaleoutSignals = (numberOfSends + numberOfRecvs) * m_scaleoutProvider->getNumOfNicsPerDevice(comm);
    LOG_HCL_TRACE(HCL,
                  "numberOfSends={}, numberOfRecvs={}, scaleoutSignals={}",
                  numberOfSends,
                  numberOfRecvs,
                  scaleoutSignals);
    return scaleoutSignals;
}

void HclCollectiveRoutinesGaudi2::memsetIMBsIfNeeded(SliceState&      sendSliceState,
                                                     SliceState&      recvSliceState,
                                                     unsigned int     sizeInBytes,
                                                     hcclDataType_t   dataType,
                                                     hcl::ScalStream* garbageStream)
{
    for (auto buffer_pool : m_memset_buffers)
    {
        m_memHandler->memsetIMBs(m_device->m_sibContainer,
                                 m_signalsManager,
                                 sendSliceState,
                                 recvSliceState,
                                 sizeInBytes,
                                 m_longSo,
                                 garbageStream->getSchedIdx(),
                                 *garbageStream,
                                 m_streamId,
                                 buffer_pool,
                                 getEdmaStreamCtxtId(sendSliceState.m_apiId, m_streamId),
                                 dataType);
    }
}

uint64_t RemainderCalculatorGaudi2::getBufferClearSize(HCL_CollectiveOp collective,
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
    return originalSize;
}

uint64_t RemainderCalculatorGaudi2::getBoxCount(uint64_t nonRemainderBoxCount,
                                                uint64_t numBoxes,
                                                uint64_t ScaleupGroupSize,
                                                uint64_t boxIndex,
                                                uint64_t scaleUpCount,
                                                uint64_t remainderCount)
{
    return std::min((int)(scaleUpCount * ScaleupGroupSize),
                    (int)std::max(0, (int)(remainderCount - (boxIndex * ScaleupGroupSize * scaleUpCount))));
}

uint64_t RemainderCalculatorGaudi2::getScaleOutCount(uint64_t nonRemainderScaleOutCount,
                                                     uint64_t numBoxes,
                                                     uint64_t boxCount,
                                                     uint64_t boxIndex,
                                                     uint64_t myRankInScaleupGroup,
                                                     uint64_t scaleUpCount,
                                                     uint64_t remainderCount,
                                                     bool     lastRankInScaleupGroup)
{
    return std::min((int)scaleUpCount, std::max(0, (int)(boxCount - (myRankInScaleupGroup * scaleUpCount))));
}

uint64_t RemainderCalculatorGaudi2::getDiv(uint64_t a, uint64_t b)
{
    return div_round_up(a, b);
}

uint64_t RemainderCalculatorGaudi2::getRemainderCount(uint64_t totalCount, uint64_t scaleUpCount, uint64_t commSize)
{
    return totalCount;
}

bool RemainderCalculatorGaudi2::isValidSlicing(uint32_t originalBufferCount,
                                               uint32_t sliceCount,
                                               uint64_t collectiveCount,
                                               uint32_t numSlices,
                                               uint32_t numRanks,
                                               uint32_t minBufferCount)
{
    return sliceCount >= minBufferCount || numSlices == 1;
}

bool RemainderCalculatorGaudi2::isSlicing(uint64_t totalCount,
                                          uint64_t totalCountPerRank,
                                          uint32_t bufferCount,
                                          uint32_t numRanks)
{
    return totalCountPerRank > bufferCount;
}
