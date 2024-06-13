#include "scal_manager.h"

#include <cstdint>                                     // for uint64_t, uint...
#include <map>                                         // for map
#include <ostream>                                     // for operator<<
#include "hcl_utils.h"                                 // for LOG_HCL_DEBUG
#include "infra/scal/gen2_arch_common/arch_stream.h"   // for ArchStream
#include "infra/scal/gen2_arch_common/scal_stream.h"   // for ScalStream
#include "infra/scal/gen2_arch_common/scal_types.h"    // for CgInfo, SmInfo
#include "infra/scal/gen2_arch_common/scal_wrapper.h"  // for Gen2ArchScalWr...
#include "hcl_log_manager.h"                           // for LOG_TRACE, LOG...
#include "scal_exceptions.h"                           // for ScalBusyException
#include "scal.h"

class HclCommandsGen2Arch;
class HclDeviceGen2Arch;

using namespace hcl;

Gen2ArchScalManager::~Gen2ArchScalManager() {}

Gen2ArchScalManager::Gen2ArchScalManager(int fd, HclCommandsGen2Arch& commands) : m_commands(commands) {}

SmInfo Gen2ArchScalManager::getSmInfo(unsigned archStreamIdx)
{
    return m_smInfoArray[archStreamIdx];
}

InternalHostFenceInfo& Gen2ArchScalManager::getHostFenceInfo(unsigned archStreamIdx, unsigned fenceIdx)
{
    return m_hostFenceInfoArray[archStreamIdx][fenceIdx];
}

bool Gen2ArchScalManager::hostWaitOnFence(unsigned archStreamIdx, unsigned fenceIdx, bool askForCredit)
{
    return m_scalWrapper->hostWaitOnFence(m_hostFenceInfoArray[archStreamIdx][fenceIdx].hostFenceCounterHandle,
                                          askForCredit);
}

std::array<CgInfo, (int)SchedulerType::count> Gen2ArchScalManager::getCgInfo(unsigned archStreamIdx)
{
    std::array<CgInfo, (int)SchedulerType::count> cgArray = {{{
        0,
    }}};
    cgArray[(int)SchedulerType::internal] = m_cgInfoArray[archStreamIdx][(int)SchedulerType::internal].cgInfo;
    cgArray[(int)SchedulerType::external] = m_cgInfoArray[archStreamIdx][(int)SchedulerType::external].cgInfo;
    return cgArray;
}

scal_comp_group_handle_t Gen2ArchScalManager::getCgHandle(unsigned archStreamIdx, bool external)
{
    if (external)
    {
        return m_cgInfoArray[archStreamIdx][(int)SchedulerType::external].cgHandle;
    }
    else
    {
        return m_cgInfoArray[archStreamIdx][(int)SchedulerType::internal].cgHandle;
    }
}

void Gen2ArchScalManager::init()
{
    initScalData();
}

void Gen2ArchScalManager::initScalData()
{
    m_scalWrapper->initMemory();

    for (size_t i = 0; i < ScalJsonNames::numberOfArchsStreams; i++)
    {
        m_smInfoArray[i] = m_scalWrapper->getSmInfo(i);
        m_cgInfoArray[i][(int)SchedulerType::internal] =
            m_scalWrapper->getCgInfo(m_scalNames.smNames[i][SyncManagerName::cgInternal]);
        m_cgInfoArray[i][(int)SchedulerType::external] =
            m_scalWrapper->getCgInfo(m_scalNames.smNames[i][SyncManagerName::cgExternal]);

        for (unsigned j = 0; j < HOST_FENCES_NR; ++j)
        {
            m_hostFenceInfoArray[i][j] = m_scalWrapper->getHostFenceInfo(i, j);
        }
    }

    LOG_TRACE(HCL_SCAL, "{}", prettyPrint());
}

size_t Gen2ArchScalManager::getMicroArchStreams(unsigned schedIdx)
{
    return m_scalNames.numberOfMicroArchStreams[schedIdx];
}

hcl::ScalStream& Gen2ArchScalManager::getScalStream(unsigned archStreamIdx, unsigned schedIdx, unsigned streamIdx)
{
    return m_archStreams[archStreamIdx]->getScalStream(schedIdx, streamIdx);
}

bool Gen2ArchScalManager::isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx)
{
    return m_archStreams[archStreamIdx]->isACcbHalfFullForDeviceBenchMark();
}

void Gen2ArchScalManager::disableCcb(int archStreamIdx, bool disable)
{
    m_archStreams[archStreamIdx]->disableCcb(disable);
}

void Gen2ArchScalManager::dfaLog(int archStreamIdx, hl_logger::LoggerSPtr synDevFailLog)
{
    m_archStreams[archStreamIdx]->dfaLog(synDevFailLog);
}

uint64_t Gen2ArchScalManager::getMonitorPayloadAddr(SchedulersIndex schedIdx, unsigned fenceIdx)
{
    return m_scalWrapper->getMonitorPayloadAddr(m_scalNames.schedulersNames[(SchedulersIndex)schedIdx], fenceIdx);
}

void Gen2ArchScalManager::initGlobalContext(HclDeviceGen2Arch* device, uint8_t apiId)
{
    LOG_HCL_ERR(HCL_SCAL, "initGlobalContext has not been implemented on this device");
}

void Gen2ArchScalManager::configQps(HCL_Comm comm, HclDeviceGen2Arch* device)
{
    LOG_HCL_ERR(HCL_SCAL, "configQps has not been implemented on this device");
}

void Gen2ArchScalManager::initSimb(HclDeviceGen2Arch* device, uint8_t apiID)
{
    LOG_HCL_ERR(HCL_SCAL, "initSimb has not been implemented on this device");
}

bool Gen2ArchScalManager::eventQuery(scal_comp_group_handle_t cgHandle, uint64_t targetValue)
{
    LOG_TRACE(HCL_SCAL, "eventQuery on cgHandle 0x{:X} with targetValue {}", (uint64_t)cgHandle, targetValue);
    return m_scalWrapper->checkTargetValueOnCg(cgHandle, targetValue);
}

bool Gen2ArchScalManager::streamQuery(unsigned archStreamIdx, uint64_t targetValue)
{
    LOG_TRACE(HCL_SCAL, "streamQuery on archStreamIdx {} with targetValue {}", archStreamIdx, targetValue);
    return m_archStreams[archStreamIdx]->streamQuery(targetValue);
}

void Gen2ArchScalManager::synchronizeStream(unsigned archStreamIdx, uint64_t targetValue)
{
    LOG_TRACE(HCL_SCAL, "synchronizeStream on archStreamIdx {} with targetValue {}", archStreamIdx, targetValue);
    m_archStreams[archStreamIdx]->synchronizeStream(targetValue);
}

void Gen2ArchScalManager::cgRegisterTimeStemp(unsigned archStreamIdx,
                                              uint64_t targetValue,
                                              uint64_t timestampHandle,
                                              uint32_t timestampsOffset)
{
    LOG_TRACE(HCL_SCAL,
              "cgRegisterTimeStemp on archStreamIdx {} with targetValue {} timestampHandle 0x{:x} timestampsOffset {}",
              archStreamIdx,
              targetValue,
              (uint64_t)timestampHandle,
              timestampsOffset);
    m_archStreams[archStreamIdx]->cgRegisterTimeStemp(targetValue, timestampHandle, timestampsOffset);
}

void Gen2ArchScalManager::eventSynchronize(scal_comp_group_handle_t cgHandle, uint64_t targetValue)
{
    LOG_TRACE(HCL_SCAL, "eventSynchronize on cgHandle 0x{:X} with targetValue {}", (uint64_t)cgHandle, targetValue);
    m_scalWrapper->waitOnCg(cgHandle, targetValue);
}

void Gen2ArchScalManager::getHBMAddressRange(uint64_t& start, uint64_t& end) const
{
    m_scalWrapper->getHBMAddressRange(start, end);
}

void Gen2ArchScalManager::getHBMInfoForExport(uint64_t& vaBase,
                                              uint64_t& hbmPoolStart,
                                              uint64_t& allocatedPoolSize) const
{
    m_scalWrapper->getHBMInfoForExport(vaBase, hbmPoolStart, allocatedPoolSize);
}

void Gen2ArchScalManager::waitOnCg(Gen2ArchScalWrapper::CgComplex& cgComplex, const uint64_t target)
{
    scal_completion_group_set_expected_ctr(cgComplex.cgHandle, target);
    for (size_t i = 0; i < 1000; i++)  // TODO: Decide for how many iterations
    {
        try
        {
            m_scalWrapper->waitOnCg(cgComplex.cgHandle, target);
            break;
        }
        catch (const ScalBusyException&)
        {
            LOG_HCL_DEBUG(HCL_SCAL,
                          "waiting on cg 0x{:x} with target: {}, cg index: {}, longSO=0x{:x}, iteration = {}",
                          (uint64_t)cgComplex.cgHandle,
                          1,
                          cgComplex.cgInfo.cgIdx[(int)SchedulersIndex::recvScaleUp],
                          (uint64_t)cgComplex.cgInfo.longSoAddr,
                          i);
        }
    }
}

std::string Gen2ArchScalManager::prettyPrint() const
{
    std::stringstream ss;

    ss << "Completion Groups Info:" << std::endl;

    for (size_t i = 0; i < ScalJsonNames::numberOfArchsStreams; i++)
    {
        ss << "ArchStream number " << i << ":" << std::endl;
        ss << "  Sync Manager Info:" << std::endl;

        ss << "    soBaseIdx=" << m_smInfoArray[i].soBaseIdx << " soSize=" << m_smInfoArray[i].soSize
           << " soSmIndex=" << m_smInfoArray[i].soSmIndex << " monitorBaseIdx=" << m_smInfoArray[i].monitorBaseIdx
           << " monitorSmIndex=" << m_smInfoArray[i].monitorSmIndex << " monitorSize=" << m_smInfoArray[i].monitorSize
           << " longMonitorBaseIdx=" << m_smInfoArray[i].longMonitorBaseIdx
           << " longMonitorSmIndex=" << m_smInfoArray[i].longMonitorSmIndex
           << " longMonitorSize=" << m_smInfoArray[i].longMonitorSize << std::endl;

        ss << "  Internal Completion Groups Info:" << std::endl;

        ss << "    size=" << std::dec << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.size << " CgBaseAddr=0x"
           << std::hex << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgBaseAddr
           << " nrOfIndices=" << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.nrOfIndices
           << " cgIdx[0]=" << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgIdx[0]
           << " cgIdx[1]=" << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgIdx[1]
           << " cgIdx[2]=" << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgIdx[2]
           << " cgIdx[3]=" << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgIdx[3]
           << " cgIdx[4]=" << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgIdx[4]
           << " cgIdxInHost=" << std::dec << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.cgIdxInHost
           << " longSoAddr=0x" << std::hex << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.longSoAddr
           << " longSoIdx=" << std::dec << m_cgInfoArray[i][(int)SchedulerType::internal].cgInfo.longSoIndex
           << std::endl;

        ss << "  External Completion Groups Info:" << std::endl;

        ss << "    size=" << std::dec << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.size << " CgBaseAddr=0x"
           << std::hex << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgBaseAddr
           << " nrOfIndices=" << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.nrOfIndices
           << " cgIdx[0]=" << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgIdx[0]
           << " cgIdx[1]=" << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgIdx[1]
           << " cgIdx[2]=" << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgIdx[2]
           << " cgIdx[3]=" << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgIdx[3]
           << " cgIdx[4]=" << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgIdx[4]
           << " cgIdxInHost=" << std::dec << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.cgIdxInHost
           << " longSoAddr=0x" << std::hex << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.longSoAddr
           << " longSoIdx=" << std::dec << m_cgInfoArray[i][(int)SchedulerType::external].cgInfo.longSoIndex
           << std::endl;
    }

    return ss.str();
}

const std::vector<unsigned> Gen2ArchScalManager::getNicsScaleUpEngines()
{
    return m_scalWrapper->getNicsScaleUpEngines();
}

unsigned Gen2ArchScalManager::getNumberOfEdmaEngines(unsigned groupNum)
{
    static const char* clusterPrefix   = "network_edma_";
    int                maxStringLength = 20;  // Maximum length of the modified string, including the null terminator

    char modifiedString[maxStringLength];
    snprintf(modifiedString, sizeof(modifiedString), "%s%d", clusterPrefix, groupNum);

    return m_scalWrapper->getNumberOfEngines(modifiedString);
}

uint64_t Gen2ArchScalManager::getCurrentLongSoValue(unsigned archStream)
{
    return m_scalWrapper->getCurrentLongSoValue(m_cgInfoArray[archStream][(int)SchedulerType::external].cgHandle);
}
