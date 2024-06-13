#include "scal_wrapper.h"

#include <algorithm>                                 // for max
#include <array>                                     // for array, array<>::...
#include <cassert>                                   // for assert
#include <cstdint>                                   // for uint64_t, uint32_t
#include <string>                                    // for operator+, to_st...
#include "hcl_utils.h"                               // for LOG_HCL_DEBUG
#include "infra/scal/gen2_arch_common/scal_types.h"  // for CgInfo, SmInfo
#include "hcl_log_manager.h"                         // for LOG_*
#include "scal_exceptions.h"                         // for ScalErrorException
#include "scal_names.h"                              // for SyncManagerName
#include "scal_utils.h"                              // for Gen2ArchScalUtils
#include "infra/hcl_debug_stats.h"                   // for DEBUG_STATS_...

using namespace hcl;

Gen2ArchScalWrapper::Gen2ArchScalWrapper(scal_handle_t deviceHandle, ScalJsonNames& scalNames)
: m_deviceHandle(deviceHandle), m_scalNames(scalNames)
{
}

Gen2ArchScalWrapper::Gen2ArchScalWrapper(int fd, ScalJsonNames& scalNames) : m_scalNames(scalNames)
{
    if (fd == -1) return;
    scal_handle_t a = 0;

    int rc         = scal_get_handle_from_fd(fd, &a);
    m_deviceHandle = a;
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_handle_from_fd with fd: " + std::to_string(fd));
    }
}

void Gen2ArchScalWrapper::getMemoryPoolInfo(scal_pool_handle_t*    mpHandle,
                                            scal_memory_pool_info* mpInfo,
                                            const std::string&     name) const
{
    int rc = scal_get_pool_handle_by_name(m_deviceHandle, name.c_str(), mpHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_pool_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) + " on " + name);
    }

    rc = scal_pool_get_info(*mpHandle, mpInfo);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_pool_get_info with pool handle: " +
                                 std::to_string(uint64_t(mpHandle)));
    }
}

void Gen2ArchScalWrapper::getMemoryPoolInfoV2(scal_pool_handle_t*      mpHandle,
                                              scal_memory_pool_infoV2* mpInfo,
                                              const std::string&       name) const
{
    int rc = scal_get_pool_handle_by_name(m_deviceHandle, name.c_str(), mpHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_pool_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) + " on " + name);
    }

    rc = scal_pool_get_infoV2(*mpHandle, mpInfo);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_pool_get_infoV2 with pool handle: " +
                                 std::to_string(uint64_t(mpHandle)));
    }
}

void Gen2ArchScalWrapper::initSchedulersMap()
{
    scal_core_handle_t schedulerHandle;

    for (int i = 0; i < (int)SchedulersIndex::count; i++)
    {
        scal_get_core_handle_by_name(m_deviceHandle,
                                     m_scalNames.schedulersNames[(SchedulersIndex)i].c_str(),
                                     &schedulerHandle);
        m_schedulersHandleToCGGIndex[schedulerHandle] = (SchedulersIndex)i;
    }
}

void Gen2ArchScalWrapper::initMemory()
{
    getMemoryPoolInfo(&m_mpHandle, &m_mpInfo, "host_shared");
    initSchedulersMap();
}

void Gen2ArchScalWrapper::initStream(std::string           streamName,
                                     scal_stream_handle_t& streamHandle,
                                     scal_stream_info_t&   m_streamInfo,
                                     uint64_t              size,
                                     scal_buffer_handle_t& bufferHandle,
                                     scal_buffer_info_t&   bufferInfo) const
{
    int rc = scal_get_stream_handle_by_name(m_deviceHandle, streamName.c_str(), &streamHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_stream_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) + " on stream: " + streamName);
    }

    rc = scal_allocate_buffer(m_mpHandle, size, &bufferHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_allocate_buffer with pool handle: " +
                                 std::to_string(uint64_t(m_mpHandle)) + " and size:" + std::to_string(size));
    }

    rc = scal_buffer_get_info(bufferHandle, &bufferInfo);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_buffer_get_info with buffer handle: " +
                                 std::to_string(uint64_t(bufferHandle)));
    }

    rc = scal_stream_set_commands_buffer(streamHandle, bufferHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_stream_set_commands_buffer with stream handle: " +
                                 std::to_string(uint64_t(streamHandle)));
    }

    rc = scal_stream_get_info(streamHandle, &m_streamInfo);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_stream_get_info with stream handle: " +
                                 std::to_string(uint64_t(streamHandle)) + " on stream: " + streamName);
    }
}

void Gen2ArchScalWrapper::completionGroupRegisterTimestamp(const scal_comp_group_handle_t compGrp,
                                                           const uint64_t                 longSoValue,
                                                           uint64_t                       timestampHandle,
                                                           uint32_t                       timestampsOffset)
{
    LOG_TRACE(HCL_SCAL,
              "Recording event timestamp longso handle 0x{:x} value {} timestampHandle 0x{:x}, timestampsOffset {}",
              (uint64_t)compGrp,
              longSoValue,
              timestampHandle,
              timestampsOffset);
    const int rc = scal_completion_group_register_timestamp(compGrp, longSoValue, timestampHandle, timestampsOffset);

    if (rc != SCAL_SUCCESS)
    {
        LOG_ERR(HCL_SCAL, "scal_completion_group_register_timestamp failed with {}", rc);
        throw ScalErrorException("Scal call scal_completion_group_register_timestamp returned with error.");
    }
}

bool Gen2ArchScalWrapper::checkTargetValueOnCg(const scal_comp_group_handle_t compGrp, const uint64_t target) const
{
    LOG_HCL_DEBUG(HCL_SCAL, "Check target on CG: {}", target);
    int rc = scal_completion_group_wait(compGrp, target, 0);  // Non blocking
    if (rc == SCAL_SUCCESS)
    {
        LOG_HCL_TRACE(HCL_SCAL, "Target value {} was reached", target);
        return true;
    }
    else if (rc == SCAL_TIMED_OUT)
    {
        LOG_HCL_DEBUG(HCL_SCAL, "Target value {} was not reached", target);
        return false;
    }
    else
    {
        throw ScalErrorException("Failed on scal_completion_group_wait with scal rc=" + std::to_string(rc) +
                                 ", compGrp handle: " + std::to_string(uint64_t(compGrp)) + " and target value " +
                                 std::to_string(target));
    }
}

void Gen2ArchScalWrapper::waitOnCg(const scal_comp_group_handle_t compGrp, const uint64_t target) const
{
    LOG_HCL_DEBUG(HCL_SCAL, "wait On CG for target: {}", target);
    int rc = scal_completion_group_wait(compGrp, target, SCAL_FOREVER);
    if (rc == SCAL_SUCCESS)
    {
        return;
    }
    else if (rc == SCAL_TIMED_OUT)
    {
        scal_completion_group_info_t info;
        rc = scal_completion_group_get_info(compGrp, &info);

        LOG_WARN(HCL_SCAL,
                 "Timeout waiting for target, current reached targetValue in cg is: {}. "
                 "targValue: {}, cg info read rc {}",
                 info.current_value,
                 target,
                 rc);

        throw ScalBusyException("Scal call scal_completion_group_wait returned with busy.");  // after last attempt
    }
    else
    {
        throw ScalErrorException("Failed on scal_completion_group_wait with scal rc=" + std::to_string(rc) +
                                 ", compGrp handle: " + std::to_string(uint64_t(compGrp)) + " and target value " +
                                 std::to_string(target));
    }
}

void Gen2ArchScalWrapper::freeBuffer(const scal_buffer_handle_t& bufferHandle)
{
    int rc = scal_free_buffer(bufferHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_free_buffer with device handle: " +
                                 std::to_string(uint64_t(bufferHandle)));
    }
}
void Gen2ArchScalWrapper::sendStream(const scal_stream_handle_t stream,
                                     const unsigned             pi,
                                     const unsigned             submissionAlignment)
{
    int rc = scal_stream_submit(stream, pi, submissionAlignment);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on sendStream with stream handle: " + std::to_string(uint64_t(stream)) +
                                 " and pi  " + std::to_string(pi) +
                                 " submissionAlignment: " + std::to_string(submissionAlignment));
    }
}

Gen2ArchScalWrapper::CgComplex Gen2ArchScalWrapper::getCgInfo(std::string cgName) const
{
    scal_comp_group_handle_t       cgHndl;
    scal_completion_group_infoV2_t cgScalInfo;
    // SW-47057 - for adding zero

    int rc = scal_get_completion_group_handle_by_name(m_deviceHandle, (cgName + "0").c_str(), &cgHndl);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_completion_group_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) + " and cg name  " + cgName);
    }

    rc = scal_completion_group_get_infoV2(cgHndl, &cgScalInfo);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_completion_group_get_infoV2 with cg handle: " +
                                 std::to_string(uint64_t(cgHndl)) + " and cg name  " + cgName);
    }

    CgInfo cgInfo;

    cgInfo.size        = cgScalInfo.sos_num;
    cgInfo.cgBaseAddr  = m_utils->calculateSoAddressFromIdxAndSM(cgScalInfo.sm, cgScalInfo.sos_base);
    cgInfo.cgIdxInHost = cgScalInfo.index_in_scheduler;  // Is that really that?
    cgInfo.longSoAddr  = m_utils->calculateSoAddressFromIdxAndSM(cgScalInfo.long_so_sm, cgScalInfo.long_so_index);
    cgInfo.cgIdx[(int)m_schedulersHandleToCGGIndex.at(cgScalInfo.scheduler_handle)] = cgScalInfo.index_in_scheduler;
    cgInfo.nrOfIndices        = cgScalInfo.num_slave_schedulers + 1;
    cgInfo.longSoIndex        = cgScalInfo.long_so_index;
    cgInfo.longSoDcore        = cgScalInfo.long_so_dcore;
    cgInfo.longSoInitialValue = cgScalInfo.current_value;
    cgInfo.forceOrder         = cgScalInfo.force_order;
    for (size_t i = 0; i < cgScalInfo.num_slave_schedulers; i++)
    {
        cgInfo.cgIdx[(int)m_schedulersHandleToCGGIndex.at(cgScalInfo.slave_schedulers[i])] =
            cgScalInfo.index_in_slave_schedulers[i];
    }

    Gen2ArchScalWrapper::CgComplex cgComplex;

    cgComplex.cgInfo   = cgInfo;
    cgComplex.cgHandle = cgHndl;

    return cgComplex;
}

uint64_t Gen2ArchScalWrapper::getCurrentLongSoValue(scal_comp_group_handle_t cgHandle)
{
    scal_completion_group_info_t cgScalInfo;
    int                          rc = scal_completion_group_get_info(cgHandle, &cgScalInfo);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_completion_group_get_info with cg handle: " +
                                 std::to_string(uint64_t(cgHandle)) + " on DFA");
    }

    return cgScalInfo.current_value;
}

SmInfo Gen2ArchScalWrapper::getSmInfo(unsigned archStreamIndex) const
{
    scal_monitor_pool_handle_t monPoolHandle;
    scal_monitor_pool_info     monPoolInfo;
    scal_monitor_pool_handle_t longMonPoolHandle;
    scal_monitor_pool_info     longMonPoolInfo;
    scal_so_pool_handle_t      soPoolHandle;
    scal_so_pool_info          soPoolInfo;

    int rc = scal_get_so_monitor_handle_by_name(
        m_deviceHandle,
        m_scalNames.smNames[archStreamIndex][SyncManagerName::networkMonitor].c_str(),
        &monPoolHandle);
    assert(rc == 0);
    if (rc != 0)
    {
        throw ScalErrorException(
            "Failed on scal_get_so_monitor_handle_by_name with smName: " +
            m_scalNames.smNames[archStreamIndex][SyncManagerName::networkMonitor]);
    }

    rc = scal_monitor_pool_get_info(monPoolHandle, &monPoolInfo);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_monitor_pool_get_info with pool handle: " +
                                 std::to_string(uint64_t(monPoolHandle)));
    }
    assert(rc == 0);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_completion_group_get_info with pool handle: " +
                                 std::to_string(uint64_t(monPoolHandle)));
    }

    rc = scal_get_so_monitor_handle_by_name(m_deviceHandle,
                                            m_scalNames.smNames[archStreamIndex][SyncManagerName::longMonitor].c_str(),
                                            &longMonPoolHandle);
    assert(rc == 0);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_get_so_monitor_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) +
                                 " and name: " + m_scalNames.smNames[archStreamIndex][SyncManagerName::longMonitor]);
    }
    rc = scal_monitor_pool_get_info(longMonPoolHandle, &longMonPoolInfo);
    assert(rc == 0);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_monitor_pool_get_info with long monitor handle: " +
                                 std::to_string(uint64_t(longMonPoolHandle)));
    }

    rc = scal_get_so_pool_handle_by_name(m_deviceHandle,
                                         m_scalNames.smNames[archStreamIndex][SyncManagerName::so].c_str(),
                                         &soPoolHandle);
    assert(rc == 0);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_get_so_pool_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) +
                                 " and name: " + m_scalNames.smNames[archStreamIndex][SyncManagerName::so]);
    }
    rc = scal_so_pool_get_info(soPoolHandle, &soPoolInfo);
    assert(rc == 0);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_so_pool_get_info with so pool handle: " +
                                 std::to_string(uint64_t(soPoolHandle)));
    }

    SmInfo info;

    info.soBaseIdx    = soPoolInfo.baseIdx;
    info.soSmIndex    = soPoolInfo.smIndex;
    info.soDcoreIndex = soPoolInfo.dcoreIndex;
    info.soSize       = soPoolInfo.size;

    info.monitorBaseIdx  = monPoolInfo.baseIdx;
    info.monitorSmIndex  = monPoolInfo.smIndex;
    info.monitorSize     = monPoolInfo.size;

    info.longMonitorBaseIdx  = longMonPoolInfo.baseIdx;
    info.longMonitorSmIndex  = longMonPoolInfo.smIndex;
    info.longMonitorSize     = longMonPoolInfo.size;  // In term of regular monitors (4 monitors per long monitor)

    return info;
}

InternalHostFenceInfo Gen2ArchScalWrapper::getHostFenceInfo(unsigned archStreamIndex, unsigned fenceIdx) const
{
    std::string                      host_counter_name = m_scalNames.getFenceName(archStreamIndex, fenceIdx);
    scal_host_fence_counter_handle_t hostFenceCounterHandle;
    if (scal_get_host_fence_counter_handle_by_name(m_deviceHandle, host_counter_name.c_str(), &hostFenceCounterHandle))
    {
        throw ScalErrorException("scal_get_host_fence_counter_handle_by_name failed");
    }

    scal_host_fence_counter_info_t hostFenceCounterInfo;
    if (scal_host_fence_counter_get_info(hostFenceCounterHandle, &hostFenceCounterInfo))
    {
        throw ScalErrorException("scal_host_fence_counter_get_info failed");
    }

    InternalHostFenceInfo internalHostFenceInfo;
    internalHostFenceInfo.hostFenceInfo.smDcore  = hostFenceCounterInfo.sm;
    internalHostFenceInfo.hostFenceInfo.smIndex  = hostFenceCounterInfo.so_index;
    internalHostFenceInfo.decrementsPtr          = hostFenceCounterInfo.request_counter;
    internalHostFenceInfo.incrementsPtr          = hostFenceCounterInfo.ctr;
    internalHostFenceInfo.hostFenceCounterHandle = hostFenceCounterHandle;

    return internalHostFenceInfo;
}

bool Gen2ArchScalWrapper::hostWaitOnFence(scal_host_fence_counter_handle_t fenceHandle, bool askForCredit)
{
    int rc = scal_host_fence_counter_wait(fenceHandle, askForCredit ? 1 : 0, 0);
    if (rc == SCAL_SUCCESS)
    {
        return false;
    }
    else if (rc == SCAL_TIMED_OUT)
    {
        return true;
    }
    else
    {
        throw ScalErrorException("Failed on scal_host_fence_counter_wait with scal rc=" + std::to_string(rc));
        return true;
    }
}

void Gen2ArchScalWrapper::signalFromHost(unsigned smIdx, unsigned soIdx, uint32_t value)
{
    HCL_FUNC_INSTRUMENTATION(DEBUG_STATS_ALL);

    scal_sm_info_t smInfo;
    if (scal_get_sm_info(m_deviceHandle, smIdx, &smInfo))
    {
        throw ScalErrorException("scal_get_sm_info failed");
    }
    scal_write_mapped_reg(&smInfo.objs[soIdx], value);
}

unsigned Gen2ArchScalWrapper::getNumberOfEngines(const char* cluster_name)
{
    scal_cluster_handle_t cluster;
    int                   rc = scal_get_cluster_handle_by_name(m_deviceHandle, cluster_name, &cluster);
    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_get_cluster_handle_by_name with name: nic_scaleup");
    }

    scal_cluster_info_t clusterInfo;

    rc = scal_cluster_get_info(cluster, &clusterInfo);

    if (rc != 0)
    {
        throw ScalErrorException("Failed on scal_cluster_get_info with so pool handle: " +
                                 std::to_string(uint64_t(cluster)));
    }

    return clusterInfo.numEngines;
}

void Gen2ArchScalWrapper::getHBMAddressRange(uint64_t& start, uint64_t& end) const
{
    scal_pool_handle_t    mpHandle;
    scal_memory_pool_info mpInfo;

    getMemoryPoolInfo(&mpHandle, &mpInfo, "global_hbm");

    start = mpInfo.device_base_address;
    end   = start + mpInfo.totalSize;
}

void hcl::Gen2ArchScalWrapper::getHBMInfoForExport(uint64_t& vaBase,
                                                   uint64_t& hbmPoolStart,
                                                   uint64_t& allocatedPoolSize) const
{
    scal_pool_handle_t      mpHandle;
    scal_memory_pool_infoV2 mpInfo;

    getMemoryPoolInfoV2(&mpHandle, &mpInfo, "global_hbm");

    vaBase            = mpInfo.device_base_allocated_address;
    hbmPoolStart      = mpInfo.device_base_address;
    allocatedPoolSize = mpInfo.totalSize;
    LOG_DEBUG(HCL,
              "vaBase 0x{:x}, hbmPoolStart 0x{:x}, allocatedPoolSize {:g}MB",
              vaBase,
              hbmPoolStart,
              B2MB(allocatedPoolSize));
}

const std::vector<unsigned> Gen2ArchScalWrapper::getNicsScaleUpEngines()
{
    if (m_scaleUpNicEngines.size() == 0)
    {
        scal_cluster_handle_t cluster;
        int                   rc = scal_get_cluster_handle_by_name(m_deviceHandle, "nic_scaleup", &cluster);
        if (rc != 0)
        {
            throw ScalErrorException("Failed on scal_get_cluster_handle_by_name with name: nic_scaleup");
        }

        scal_cluster_info_t clusterInfo;

        rc = scal_cluster_get_info(cluster, &clusterInfo);

        if (rc != 0)
        {
            throw ScalErrorException("Failed on scal_cluster_get_info with so pool handle: " +
                                     std::to_string(uint64_t(cluster)));
        }

        std::string engineInitName = "NIC_";

        for (unsigned i = 0; i < clusterInfo.numEngines; i++)
        {
            scal_control_core_info_t info;
            scal_control_core_get_info(clusterInfo.engines[i], &info);
            std::string   niceName(info.name);
            std::string   s         = niceName.substr(engineInitName.size(), niceName.length());
            unsigned long nicNumber = std::stoul(s, nullptr, 10);
            m_scaleUpNicEngines.push_back(nicNumber);
        }
    }
    return m_scaleUpNicEngines;
}
