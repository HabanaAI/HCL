#pragma once

#include <cstddef>       // for NULL
#include <cstdint>       // for uint64_t, uint32_t
#include <map>           // for map
#include <string>        // for string
#include <vector>        // for vector
#include "scal.h"        // for scal_comp_group_handle_t, scal_buffer_handle_t
#include "scal_names.h"  // for SchedulersIndex
#include "scal_types.h"  // for CgInfo, HostFenceInfo, SmInfo
class Gen2ArchScalUtils;

namespace hcl
{
/**
 * @brief
 *
 * ScalWrapper is the lowest layer in the HCL SW stack, and its the only one that holds scal handle.
 * All calls goes to scal SW layer will go through ScalWrapper.
 * ScalWrapper is responsible logically on the memory pools.
 *
 */
class Gen2ArchScalWrapper
{
public:
    struct CgComplex
    {
        CgInfo                   cgInfo;
        scal_comp_group_handle_t cgHandle;
    };

    /**
     * @brief Construct a new Scal Wrapper object
     *
     * @throw ScalErrorException
     */
    Gen2ArchScalWrapper(scal_handle_t deviceHandle, ScalJsonNames& scalNames);
    Gen2ArchScalWrapper(int fd, ScalJsonNames& scalNames);
    Gen2ArchScalWrapper(Gen2ArchScalWrapper&&)                 = delete;
    Gen2ArchScalWrapper(const Gen2ArchScalWrapper&)            = delete;
    Gen2ArchScalWrapper& operator=(Gen2ArchScalWrapper&&)      = delete;
    Gen2ArchScalWrapper& operator=(const Gen2ArchScalWrapper&) = delete;
    virtual ~Gen2ArchScalWrapper()                             = default;

    /**
     * @brief Get the Cg Info object
     *
     * @throw ScalErrorException
     */
    CgComplex getCgInfo(std::string cgName) const;

    /**
     * @brief Get the Sm Info object
     *
     * @throw ScalErrorException
     */
    SmInfo getSmInfo(unsigned archStreamIndex) const;

    InternalHostFenceInfo getHostFenceInfo(unsigned archStreamIndex, unsigned fenceIdx) const;

    bool hostWaitOnFence(scal_host_fence_counter_handle_t fenceHandle, bool askForCredit);

    void signalFromHost(unsigned smIdx, unsigned soIdx, uint32_t value);

    /**
     * @brief A service method for initializing stream.
     *        It will allocate buffer on host share memory, output stream handle & info, buffer handle &info
     *
     * @param streamName [in] stream name as in the configuration json file
     * @param streamHandle [out]
     * @param m_streamInfo [out]
     * @param size [in] buffer size to allocate
     * @param bufferHandle [out]
     * @param bufferInfo [out]
     *
     * @throw ScalErrorException
     */
    void initStream(std::string           streamName,
                    scal_stream_handle_t& streamHandle,
                    scal_stream_info_t&   m_streamInfo,
                    uint64_t              size,
                    scal_buffer_handle_t& bufferHandle,
                    scal_buffer_info_t&   bufferInfo) const;

    void initMemory();

    /**
     * @brief A service method for waiting to target value on completion group.
     *
     * Exception safety: if fail or busy, throw scal exception

     * @param compGrp
     * @param target - Sync object target value
     *
     * @throw ScalErrorException on failure
     * @throw ScalBusyException if timed out
     */
    void waitOnCg(const scal_comp_group_handle_t compGrp, const uint64_t target) const;

    /**
     * @brief A service method for checking if target value on completion group was reached.
     *
     * @param compGrp
     * @param target - Sync object target value
     *
     * @throw ScalErrorException on failure
     */
    bool checkTargetValueOnCg(const scal_comp_group_handle_t compGrp, const uint64_t target) const;

    void completionGroupRegisterTimestamp(const scal_comp_group_handle_t compGrp,
                                          const uint64_t                 longSoValue,
                                          uint64_t                       timestampHandle,
                                          uint32_t                       timestampsOffset);

    /**
     * @brief
     *
     * @throw ScalErrorException on failure
     */
    void sendStream(const scal_stream_handle_t stream, const unsigned pi, const unsigned submissionAlignment);
    void freeBuffer(const scal_buffer_handle_t& bufferHandle);

    // Services methods:

    unsigned         getNumberOfEngines(const char* cluster_name);
    virtual uint64_t getMonitorPayloadAddr(std::string name, unsigned fenceIdx) = 0;

    void getHBMAddressRange(uint64_t& start, uint64_t& end) const;
    void getHBMInfoForExport(uint64_t& vaBase, uint64_t& hbmPoolStart, uint64_t& allocatedPoolSize) const;

    uint64_t getCurrentLongSoValue(scal_comp_group_handle_t cgHandle);

    const std::vector<unsigned> getNicsScaleUpEngines();
    Gen2ArchScalUtils*          m_utils = NULL;

    scal_handle_t getScalHandle() { return m_deviceHandle; }

protected:
    scal_handle_t m_deviceHandle = {0};

private:
    /**
     * @brief This class is responsible on getting the share memory pool. For now we support in HCL only
     *        host memory. The cyclic buffer will not be managed here.
     */
    void getMemoryPoolInfo(scal_pool_handle_t* mpHandle, scal_memory_pool_info* mpInfo, const std::string& name) const;
    /**
     * @brief Get the Memory Pool Info V2 object
     *
     * @param mpHandle scal memory pool handle
     * @param mpInfo scal memory pool info V2 object
     * @param name pool name
     */
    void
    getMemoryPoolInfoV2(scal_pool_handle_t* mpHandle, scal_memory_pool_infoV2* mpInfo, const std::string& name) const;

    /**
     * @brief Since the schedulers in completion group info is not in any particular order in HCL terms,
     *        we need to save maping from completion group handle to the order of the schedulers which is
     *        located in the json names class SchedulersIndex
     */
    void initSchedulersMap();

    scal_monitor_pool_handle_t                    m_monPoolHandle = {0};
    scal_pool_handle_t                            m_mpHandle      = {0};
    scal_memory_pool_info                         m_mpInfo = {0};  // Currently HCL will holds only host memory pool
    scal_monitor_pool_info                        m_monPoolInfo = {0};
    ScalJsonNames&                                m_scalNames;
    std::map<scal_core_handle_t, SchedulersIndex> m_schedulersHandleToCGGIndex;
    std::vector<unsigned>                         m_scaleUpNicEngines;
};

}  // namespace hcl
