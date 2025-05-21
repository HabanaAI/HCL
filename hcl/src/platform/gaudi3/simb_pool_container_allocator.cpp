
#include "platform/gaudi3/simb_pool_container_allocator.h"
#include "hcl_utils.h"
#include "platform/gaudi3/device_simb_pool_manager.h"

SimbPoolContainerAllocatorGaudi3::SimbPoolContainerAllocatorGaudi3(uint64_t numberOfStreams)
: SimbPoolContainerAllocatorGaudiCommon(numberOfStreams) {};

void SimbPoolContainerAllocatorGaudi3::init()
{
    if (GCFG_HCCL_GAUDI_DIRECT.value() && !GCFG_HCL_IMB_SIZE.isSetFromUserConfig())
    {
        m_imbSize = GCFG_HCL_GDR_SLICE_SIZE.value();
        LOG_HCL_INFO(HCL,
                     "Using increased IMB size of {}MB since Gaudi-direct is enabled",
                     B2MB(GCFG_HCL_GDR_SLICE_SIZE.value()));
    }
    else
    {
        m_imbSize = GCFG_HCL_IMB_SIZE.value();
    }

    std::array<std::vector<e_devicePoolID>, MAX_POOL_CONTAINER_IDX> poolTypes = {std::vector<e_devicePoolID> {},
                                                                                 std::vector<e_devicePoolID> {},
                                                                                 std::vector<e_devicePoolID> {},
                                                                                 std::vector<e_devicePoolID> {}};

    if (GCFG_HCL_RS_SO_RECV_CONT_REDUCTION.value())
    {
        poolTypes[SIBO_DOUBLE_SIMB_SIZE].push_back(SCALEOUT_ACC_POOL);
        poolTypes[SIBO_STANDARD_SIMB_SIZE].push_back(SCALEOUT_POOL);
        poolTypes[SIBO_STANDARD_SIMB_SIZE].push_back(SCALEOUT_POOL_1);
    }
    else
    {
        poolTypes[SIBO_DOUBLE_SIMB_SIZE].push_back(SCALEOUT_POOL);
        if (GCFG_HCCL_GAUDI_DIRECT.value())
        {
            poolTypes[NON_SIBO_STANDARD_SIMB_SIZE].push_back(SCALEOUT_GDR_POOL);
        }
    }
    poolTypes[NON_SIBO_STANDARD_SIMB_SIZE].push_back(REDUCE_POOL);
    poolTypes[SIBO_STANDARD_SIMB_SIZE].push_back(SCALEUP_AND_ALL2ALL_POOL);

    if (GCFG_HCCL_PRIM_COLLECTIVE_MASK.value())
    {
        poolTypes[NON_SIBO_DOUBLE_SIMB_SIZE].push_back(PRIMITIVE_POOL);
    }
    generateContainerParams(m_imbSize * 2,
                            poolTypes[SIBO_DOUBLE_SIMB_SIZE],
                            m_poolContainerParams[SIBO_DOUBLE_SIMB_SIZE]);
    generateContainerParams(m_imbSize,
                            poolTypes[SIBO_STANDARD_SIMB_SIZE],
                            m_poolContainerParams[SIBO_STANDARD_SIMB_SIZE]);
    generateContainerParams(m_imbSize * 2,
                            poolTypes[NON_SIBO_DOUBLE_SIMB_SIZE],
                            m_poolContainerParams[NON_SIBO_DOUBLE_SIMB_SIZE]);
    generateContainerParams(m_imbSize,
                            poolTypes[NON_SIBO_STANDARD_SIMB_SIZE],
                            m_poolContainerParams[NON_SIBO_STANDARD_SIMB_SIZE]);

    for (unsigned poolContainerIndex = 0; poolContainerIndex < m_poolContainerParams.size(); poolContainerIndex++)
    {
        LOG_HCL_TRACE(HCL,
                      "sizeOfContainerPerStream={}, sizeOfContainer={}",
                      m_poolContainerParams[poolContainerIndex].sizeOfContainerPerStream,
                      m_poolContainerParams[poolContainerIndex].sizeOfContainer);

        VERIFY(allocateDeviceMemory(m_poolContainerParams[poolContainerIndex].sizeOfContainer,
                                    &m_poolContainerParams[poolContainerIndex].containerBaseAddr),
               "Failed to allocate device memory. Size: {:g}MB",
               B2MB(m_poolContainerParams[poolContainerIndex].sizeOfContainer));
        LOG_HCL_INFO(HCL,
                     "Allocated device memory. Address: 0x{:x}, Size: {:g}MB",
                     m_poolContainerParams[poolContainerIndex].containerBaseAddr,
                     B2MB(m_poolContainerParams[poolContainerIndex].sizeOfContainer));
    }

    auto siboDoubleContainerParams      = m_poolContainerParams[SIBO_DOUBLE_SIMB_SIZE];
    auto siboStandardContainerParams    = m_poolContainerParams[SIBO_STANDARD_SIMB_SIZE];
    auto nonSiboDoubleContainerParams   = m_poolContainerParams[NON_SIBO_DOUBLE_SIMB_SIZE];
    auto nonSiboStandardContainerParams = m_poolContainerParams[NON_SIBO_STANDARD_SIMB_SIZE];

    for (size_t i = 0; i < m_numberOfStreams; i++)
    {
        std::array<SimbPoolContainerParamsPerStream, MAX_POOL_CONTAINER_IDX> m_spcParamsPerStream = {
            SimbPoolContainerParamsPerStream((siboDoubleContainerParams.containerBaseAddr +
                                              (i * siboDoubleContainerParams.sizeOfContainerPerStream)),
                                             siboDoubleContainerParams.simbSize,
                                             siboDoubleContainerParams.simbCountPerStream,
                                             poolTypes[SIBO_DOUBLE_SIMB_SIZE].size()),

            SimbPoolContainerParamsPerStream((siboStandardContainerParams.containerBaseAddr +
                                              (i * siboStandardContainerParams.sizeOfContainerPerStream)),
                                             siboStandardContainerParams.simbSize,
                                             siboStandardContainerParams.simbCountPerStream,
                                             poolTypes[SIBO_STANDARD_SIMB_SIZE].size()),

            SimbPoolContainerParamsPerStream((nonSiboDoubleContainerParams.containerBaseAddr +
                                              (i * nonSiboDoubleContainerParams.sizeOfContainerPerStream)),
                                             nonSiboDoubleContainerParams.simbSize,
                                             nonSiboDoubleContainerParams.simbCountPerStream,
                                             poolTypes[NON_SIBO_DOUBLE_SIMB_SIZE].size()),

            SimbPoolContainerParamsPerStream((nonSiboStandardContainerParams.containerBaseAddr +
                                              (i * nonSiboStandardContainerParams.sizeOfContainerPerStream)),
                                             nonSiboStandardContainerParams.simbSize,
                                             nonSiboStandardContainerParams.simbCountPerStream,
                                             poolTypes[NON_SIBO_STANDARD_SIMB_SIZE].size())};

        m_deviceSimbPoolManagers.push_back(
            std::make_shared<DeviceSimbPoolManagerGaudi3>(m_spcParamsPerStream, getSIBMap(poolTypes)));
    }

    allocateFwIntermediateBuffer();
}