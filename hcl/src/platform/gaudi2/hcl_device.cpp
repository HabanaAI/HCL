#include "platform/gaudi2/hcl_device.h"

#include <array>                                                        // for array
#include <cstdint>                                                      // for uint32_t
#include <memory>                                                       // for __share...
#include <vector>                                                       // for vector

#include "hcl_config.h"                                                 // for HclDevi...
#include "hcl_device.h"
#include "hcl_dynamic_communicator.h"                                   // for HclDyna...
#include "hcl_global_conf.h"                                            // for GCFG_BO...
#include "interfaces/hcl_remote_device.h"                               // for HclRemo...
#include "hcl_types.h"                                                  // for HclConf...
#include "hcl_utils.h"                                                  // for LOG_HCL...
#include "infra/scal/gaudi2/scal_manager.h"                             // for Gaudi2S...
#include "interfaces/hcl_unique_sorted_vector.h"                        // for UniqueS...
#include "platform/gaudi2/commands/hcl_commands.h"                      // for HclComm...
#include "platform/gaudi2/context_manager.h"                            // for Context...
#include "platform/gaudi2/hal.h"                                        // for Gaudi2Hal
#include "platform/gen2_arch_common/commands/hcl_commands.h"            // for HclComm...
#include "platform/gen2_arch_common/eq_handler.h"                       // for IEventQ...
#include "platform/gen2_arch_common/port_mapping.h"                     // for Gen2ArchDevicePortMapping
#include "hcl_log_manager.h"                                            // for LOG_ERR
#include "hcl_nic.h"                                                    // for HclNic
#include "platform/gen2_arch_common/intermediate_buffer_container.h"    // for IntermediateBufferContainer
#include "platform/gen2_arch_common/scaleout_provider.h"                // for ScaleoutProvider
#include "platform/gen2_arch_common/hcl_device_controller.h"            //
#include "hcl_log_manager.h"                                            // for LOG_ERR
#include "gaudi2/asic_reg/nic0_qm_arc_aux0_regs.h"                      // for mmNIC0_QM_ARC_AUX0_SCRATCHPAD_7
#include "hccl_communicator.h"
#include "hccl_helpers.h"
#include "hccl_coordinator_client.h"
#include "hccl_internal_defs.h"
#include "hccl_types.h"
#include "ibverbs/hcl_ibverbs.h"

class Gen2ArchDevicePortMapping;

#define IS_RS_QP(stream) ((stream & 1) != 1)

/* This is a test-only constructor, so the nic array in a few lines is allowed... :-\ */
HclDeviceGaudi2::HclDeviceGaudi2(HclDeviceControllerGen2Arch& controller)
: HclDeviceGen2Arch(controller), m_portMapping(getFd())
{
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS2, [&](HCL_Comm comm) { return openQpsHLS(comm); });
    setHal(std::make_shared<hcl::Gaudi2Hal>());
    m_qpManagerScaleUp  = std::make_unique<QPManagerScaleUpGaudi2>(this);
    m_qpManagerScaleOut = std::make_unique<QPManagerScaleOutGaudi2>(this, m_portMapping);
    m_contextManager = new ContextManager({0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21},
                                          m_portMapping,
                                          m_qpManagerScaleUp,
                                          m_qpManagerScaleOut,
                                          *this);
}

HclDeviceGaudi2::HclDeviceGaudi2(HclDeviceControllerGen2Arch& controller, HclDeviceConfig& deviceConfig)
: HclDeviceGen2Arch(controller, deviceConfig), m_portMapping(getFd(), m_portMappingConfig)
{
    m_scalManager.getHBMAddressRange(m_allocationRangeStart, m_allocationRangeEnd);
    setHal(std::make_shared<hcl::Gaudi2Hal>());
    setScaleoutMode(m_portMapping.getNumScaleOutPorts());
    createOfiPlugin();
    m_sibContainer      = new hcl::IntermediateBufferContainer(m_deviceId, m_hal->getMaxStreams());
    m_qpManagerScaleUp  = std::make_unique<QPManagerScaleUpGaudi2>(this);
    m_qpManagerScaleOut = std::make_unique<QPManagerScaleOutGaudi2>(this, m_portMapping);
    m_contextManager    = new ContextManager(m_scalManager.getNicsScaleUpEngines(),
                                          m_portMapping,
                                          m_qpManagerScaleUp,
                                          m_qpManagerScaleOut,
                                          *this);
    m_contextManager->createCollectiveContexts(controller.getGen2ArchCommands());
    registerOpenQpCallback(LOOPBACK, [&](HCL_Comm comm) { return openQpsLoopback(comm); });
    registerOpenQpCallback(HLS2, [&](HCL_Comm comm) { return openQpsHLS(comm); });

    VERIFY(g_ibv.init(this) == hcclSuccess, "ibv initialization failed");

    updateDisabledPorts();
    initNicsMask();
    openWQs();
    m_eqHandler = new IEventQueueHandler();
    m_eqHandler->startThread(this);
    m_scaleoutProvider = ScaleoutProvider::createScaleOutProvider(this);
    setEdmaEngineGroupSizes();
}

HclDeviceGaudi2::~HclDeviceGaudi2()
{
    m_qpManagerScaleUp.reset();
    m_qpManagerScaleOut.reset();
    delete m_contextManager;
}

hlthunk_device_name HclDeviceGaudi2::getDeviceName()
{
    return HLTHUNK_DEVICE_GAUDI2;
}

uint8_t HclDeviceGaudi2::getPeerNic(HCL_Rank rank, HCL_Comm comm, uint8_t port)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    return configType == LOOPBACK ? port : m_portMapping.getPeerPort(port);
}

unsigned HclDeviceGaudi2::getSenderWqeTableSize()
{
    constexpr int SEND_RECV_WQE_FACTOR = 4;  // sendWqeTable size must be 4 * recvWqeTable size = 4 * cgSize
    return m_cgSize * SEND_RECV_WQE_FACTOR;
}

unsigned HclDeviceGaudi2::getReceiverWqeTableSize()
{
    return m_cgSize;
}

void HclDeviceGaudi2::registerQps(HCL_Comm comm, HCL_Rank remoteRank, const QpsVector& qps, int nic)
{
    uint8_t qpSets = getNumQpSets(isScaleOutPort(nic), comm, remoteRank);

    VERIFY(qps.size() == m_hal->getMaxQPsPerNic() * qpSets,
           "Each connection should hold {} QPs but opened {} QPs for comm {}",
           m_hal->getMaxQPsPerNic() * qpSets,
           qps.size(),
           comm);

    m_contextManager->registerEarc(comm, nic);

    if (m_portMapping.isScaleoutPort(nic))
    {
        m_qpManagerScaleOut->registerQPs(comm, nic, qps, remoteRank, getCommSize(comm), qpSets);
    }
    else
    {
        m_qpManagerScaleUp->registerQPs(comm, nic, qps);
    }
}

bool HclDeviceGaudi2::isSender(unsigned _qpi)
{
    return ((_qpi == QPE_RS_SEND) || (_qpi == QPE_AG_SEND));
}

uint32_t HclDeviceGaudi2::getBackpressureOffset(uint16_t nic)
{
    uint32_t bp_offs = mmNIC0_QM_ARC_AUX0_SCRATCHPAD_7;
    /* specific NIC ARC-AUX base (for even number) */
    bp_offs += (0x80000 * (nic / 2));
    /* specific NIC ARC-AUX base (for odd number) */
    bp_offs += (0x20000 * (nic & 0x1));  // (0x20000 * (nic % 2))
    return bp_offs;
}

uint32_t HclDeviceGaudi2::getQpi(HCL_Comm comm, uint8_t nic, HCL_Rank remoteRank, uint32_t qpn, uint8_t qpSet)
{
    if (isScaleOutPort(nic))
    {
        return m_qpManagerScaleOut->getQPi(comm, nic, qpn, remoteRank);
    }
    else
    {
        return m_qpManagerScaleUp->getQPi(comm, nic, qpn);
    }
}

uint32_t HclDeviceGaudi2::getDestQpi(unsigned qpi)
{
    switch (qpi)
    {
        case QPE_RS_RECV:
            return QPE_RS_SEND;
            break;
        case QPE_AG_RECV:
            return QPE_AG_SEND;
            break;
        case QPE_RS_SEND:
            return QPE_RS_RECV;
            break;
        case QPE_AG_SEND:
            return QPE_AG_RECV;
            break;
    }

    VERIFY(false, "unreachable code, qpi({})", qpi);

    return 0;
}

hcclResult_t HclDeviceGaudi2::openQpsLoopback(HCL_Comm comm)
{
    HclConfigType configType = (HclConfigType)GCFG_BOX_TYPE_ID.value();
    if (configType != LOOPBACK)
    {
        LOG_HCL_ERR(HCL, "Invalid config type ({}), expecting LOOPBACK ({})", configType, LOOPBACK);
        return hcclInvalidArgument;
    }

    LOG_HCL_HEADER(HCL);

    initRemoteNicsLoopback(comm);

    // loop over all the nics, 3 per rank
    for (int rank = 0; rank < getCommSize(comm); rank++)
    {
        for (uint16_t index = 0; index < COMPACT_RANK_INFO_NICS; index++)
        {
            uint32_t port = LOOPBACK_NIC_INDEX_INIT(index, rank);
            if ((!m_hclNic.mask[port])) continue;

            QpsVector qps;

            // allocate max QPs per nic
            for (unsigned i = 0; i < m_hal->getMaxQPsPerNic(); i++)
            {
                qps.push_back(allocateConnection(port, rank, comm, i));
            }
            registerQps(comm, HCL_INVALID_RANK, qps, port);
        }
    }

    return hcclSuccess;
}

hcclResult_t HclDeviceGaudi2::openQpsHlsScaleUp(HCL_Comm comm)
{
    UniqueSortedVector innerRanks;
    getInnerRanks(comm, innerRanks);
    LOG_HCL_INFO(HCL, "Open scale-up QPs");
    return openQps(comm, innerRanks);
}

hcclResult_t HclDeviceGaudi2::openQpsHlsScaleOut(HCL_Comm comm, const UniqueSortedVector& outerRanks)
{
    LOG_HCL_INFO(HCL, "Open scale-out QPs, outerRanks={}", outerRanks);
    return openQps(comm, outerRanks);
}

void HclDeviceGaudi2::allocateCommQPs(HCL_Comm comm, uint32_t commSize)
{
    // this is used for null-submit mode only, we allocate QP storage without the actuall QPs
    m_qpManagerScaleOut->allocateCommQPs(comm, commSize);
}

hcclResult_t HclDeviceGaudi2::openQps(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    // in null-submit mode don't open QPs
    if (GCFG_HCL_NULL_SUBMIT.value())
    {
        // we need to allocate storage
        m_qpManagerScaleOut->allocateCommQPs(comm, getCommSize(comm));
        return hcclSuccess;
    }

    for (auto& rank : ranks)
    {
        if (rank == getMyRank(comm)) continue;

        openQpToRemoteRanks(comm, rank);
    }

    return hcclSuccess;
}

void HclDeviceGaudi2::openQpToRemoteRanks(const HCL_Comm comm, const HCL_Rank remoteRank)
{
    LOG_HCL_TRACE(HCL, "remoteRank={}", remoteRank);
    if (m_QpConnectionExistsForRank[comm].count(remoteRank))
    {
        return;
    }

    for (auto nic : getActiveNics(getMyRank(comm), remoteRank, 1, comm))
    {
        QpsVector qps;
        uint8_t   qpSets = getNumQpSets(isScaleOutPort(nic), comm, remoteRank);
        bool      isPeer = !m_portMapping.isScaleoutPort(nic) || getComm(comm).isPeer(remoteRank);
        for (uint8_t qpSet = 0; qpSet < qpSets; qpSet++)
        {
            for (unsigned qpi = 0; qpi < m_hal->getMaxQPsPerNic(); qpi++)
            {
                unsigned qp = (unsigned)INVALID_QP;
                if (isPeer || IS_RS_QP(qpi) || !(GCFG_HCL_REDUCE_NON_PEER_QPS.value()))
                {
                    qp = allocateConnection(nic, remoteRank, comm, qpi, qpSet);
                }
                qps.push_back(qp);
                LOG_HCL_DEBUG(HCL,
                              "nic {} remoteRank {} comm {} qpSet {} qpi {} qp {}",
                              nic,
                              remoteRank,
                              comm,
                              qpSet,
                              qpi,
                              qp);
            }
        }
        LOG_HCL_DEBUG(HCL,"registering qps for nic {}", nic);
        registerQps(comm, remoteRank, qps, nic);
    }
    updateRankHasQp(comm, remoteRank);
}

void HclDeviceGaudi2::updateDisabledPorts()
{
    uint64_t disabledPortsMap = ~(m_portMapping.getEnabledPortsMask());
    m_deviceConfig.updateDisabledPorts(disabledPortsMap);
}

ContextManager& HclDeviceGaudi2::getContextManager()
{
    return *m_contextManager;
}

const Gen2ArchDevicePortMapping& HclDeviceGaudi2::getPortMapping()
{
    return m_portMapping;
}

hcclResult_t HclDeviceGaudi2::updateQps(HCL_Comm comm)
{
    LOG_HCL_HEADER(HCL);

    LOG_HCL_INFO(HCL, "Update scale-up QPs");
    for (auto& rank : getComm(comm).getInnerRanksInclusive())
    {
        updateRankQps(comm, rank);
    }

    LOG_HCL_INFO(HCL, "Update scale-out connections");
    m_scaleoutProvider->verifyConnections(comm);

    return hcclSuccess;
}

void HclDeviceGaudi2::deleteCommConnections(HCL_Comm comm)
{
    LOG_HCL_INFO(HCL, "Close scale-up QPs");
    m_qpManagerScaleUp->closeQPs(comm, getComm(comm).getInnerRanksExclusive());

    LOG_HCL_INFO(HCL, "Close scale-out connections");
    m_scaleoutProvider->closeConnections(comm);
}

void HclDeviceGaudi2::closeScaleoutQPs(HCL_Comm comm, const UniqueSortedVector& ranks)
{
    m_qpManagerScaleOut->closeQPs(comm, ranks);
}

nics_mask_t HclDeviceGaudi2::getAllPorts(int deviceId, unsigned spotlightType)
{
    return m_portMapping.getAllPorts(deviceId);
};

bool HclDeviceGaudi2::isScaleOutPort(uint16_t port, unsigned spotlightType)
{
    return m_portMapping.isScaleoutPort(port);
}

uint64_t HclDeviceGaudi2::getEnabledPortsMask()
{
    return m_portMapping.getEnabledPortsMask();
}

void HclDeviceGaudi2::setEdmaEngineGroupSizes()
{
    edmaEngineGroupSizes[0] = m_scalManager.getNumberOfEdmaEngines(0);
    LOG_HCL_TRACE(HCL, "EDMA group0 has {} engines", edmaEngineGroupSizes[0]);
}