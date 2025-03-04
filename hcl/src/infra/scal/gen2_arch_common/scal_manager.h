#pragma once

#include <cstddef>                                            // for NULL, size_t
#include <algorithm>                                          // for max
#include <array>                                              // for array
#include <cstdint>                                            // for uint64_t, uint32_t
#include <memory>                                             // for unique_ptr
#include <string>                                             // for string
#include <utility>                                            // for pair, make_pair
#include <vector>                                             // for vector
#include "infra/scal/gen2_arch_common/scal_names.h"           // for ScalJsonNames
#include "scal.h"                                             // for scal_comp_group_...
#include "scal_types.h"                                       // for SmInfo
#include "scal_wrapper.h"                                     // for Gen2ArchScalWrapper
#include "platform/gen2_arch_common/device_buffer_manager.h"  // for sibAddressAndSize
#include "factory_types.h"                                    // for CyclicBufferType

class HclCommandsGen2Arch;
class HclDeviceGen2Arch;
namespace hcl
{
class ArchStream;
}
namespace hcl
{
class ScalStream;
}
namespace hcl
{
class ScalStreamBase;
}

namespace hcl
{
/**
 * @brief
 *
 * ScalManager is the API entry point to all Scal needs in HCL.
 * Its responsible for all logic needed by HCL and its the only contact to the scal SW layer.
 * It hold all static data: Arch Streams, Internal/External Compilation Groups, Sync Manager Info,
 * Memory pools, MicroArchStreams and its buffers.
 * It also responsible for managing cyclic buffers AKA MicroArchStreams
 */
class Gen2ArchScalManager
{
public:
    Gen2ArchScalManager(int fd, HclCommandsGen2Arch& commands);
    Gen2ArchScalManager(Gen2ArchScalManager&&)                 = delete;
    Gen2ArchScalManager(const Gen2ArchScalManager&)            = delete;
    Gen2ArchScalManager& operator=(Gen2ArchScalManager&&)      = delete;
    Gen2ArchScalManager& operator=(const Gen2ArchScalManager&) = delete;
    virtual ~Gen2ArchScalManager();

    /**
     * @brief Get static data related to the Sync Manager
     *
     * @param archStreamIdx - Arch Stream Index, needs to be < ScalJsonNames::numberOfArchsStreams
     * @return SmInfo
     */
    SmInfo getSmInfo(unsigned archStreamIdx);

    InternalHostFenceInfo& getHostFenceInfo(unsigned archStreamIdx, unsigned fenceIdx);

    bool hostWaitOnFence(unsigned archStreamIdx, unsigned fenceIdx, bool askForCredit = false);

    /**
     * @brief Get the static data about Internal and External Compilation Group Info
     *
     * @param archStreamIdx -  Arch Stream Index, needs to be < ScalJsonNames::numberOfArchsStreams
     * @return std::array<CgInfo, 2> - cgInfo ordered by ScalJsonNames::SchedulerType
     */
    std::array<CgInfo, 2> getCgInfo(unsigned archStreamIdx);

    /**
     * @brief Get the Cg Handle object
     *
     * @param archStreamIdx  Arch Stream Index, needs to be < ScalJsonNames::numberOfArchsStreams
     * @param external External completion queue
     * @return scal_comp_group_handle_t
     */
    scal_comp_group_handle_t getCgHandle(unsigned archStreamIdx, bool external);

    Gen2ArchScalWrapper::CgComplex getCgInfo(const std::string& cgName) const;

    size_t getMicroArchStreams(unsigned schedIdx);

    /**
     * @brief Get a ScalStream instance for submission
     *
     * @param archStreamIdx  - Arch Stream Index, needs to be < ScalJsonNames::numberOfArchsStreams
     * @param schedIdx - The scheduler index inside the ArchStream, ordered by  ScalJsonNames::SchedulersIndex
     * @param streamIdx - The MicroArchStream index in the scheduler, ordered by ScalJsonName.numberOfMicroArchStreams
     */
    hcl::ScalStream& getScalStream(unsigned archStreamIdx, unsigned schedIdx, unsigned streamIdx);

    uint64_t getMonitorPayloadAddr(SchedulersIndex schedIdx, unsigned fenceIdx);

    virtual void initGlobalContext(HclDeviceGen2Arch* device, uint8_t apiId);
    virtual void initSimb(HclDeviceGen2Arch* device, uint8_t apiID);

    /**
     * @brief event synchronize (blocking)
     *
     * @param cgHandle
     * @param targetValue
     */
    void eventSynchronize(scal_comp_group_handle_t cgHandle, uint64_t targetValue);

    void synchronizeStream(unsigned archStreamIdx, uint64_t targetValue);

    void cgRegisterTimeStemp(unsigned archStreamIdx,
                             uint64_t targetValue,
                             uint64_t timestampHandle,
                             uint32_t timestampsOffset);

    bool eventQuery(scal_comp_group_handle_t cgHandle, uint64_t targetValue);

    bool streamQuery(unsigned archStreamIdx, uint64_t targetValue);

    void getHBMAddressRange(uint64_t& start, uint64_t& end) const;
    /**
     * @brief Get relevant information regarding the HBM prior to memory export.
     *
     * @param vaBase Base virtual address of the memory mapped by SCAL.
     * @param hbmPoolStart Base address of the global HBM pool.
     * @param allocatedPoolSize Size of the global HBM pool.
     */
    void getHBMInfoForExport(uint64_t& vaBase, uint64_t& hbmPoolStart, uint64_t& allocatedPoolSize) const;

    const std::vector<unsigned> getNicsScaleUpEngines();

    unsigned getNumberOfEdmaEngines(unsigned groupNum);

    inline void addStaticBufferAddrAndSize(uint64_t addr, uint64_t size, uint64_t poolSize)
    {
        m_staticBufferAddressesAndSizes.push_back(sibAddressAndSize(addr, size, poolSize));
    }

    inline void signalFromHost(unsigned smIdx, unsigned soIdx, uint32_t value)
    {
        m_scalWrapper->signalFromHost(smIdx, soIdx, value);
    }

    uint64_t getCurrentLongSoValue(unsigned archStream);

    scal_handle_t        getScalHandle() { return m_scalWrapper->getScalHandle(); }
    Gen2ArchScalWrapper& getScalWrapper() { return *m_scalWrapper; }

    bool isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx);

    void disableCcb(int archStreamIdx, bool disable);
    void dfaLog(int archStreamIdx, hl_logger::LoggerSPtr synDevFailLog);
    void waitOnCg(Gen2ArchScalWrapper::CgComplex& cgComplex, const uint64_t target);

    virtual uint32_t getCMaxTargetValue() = 0;

private:
    std::string prettyPrint() const;

    std::array<SmInfo, ScalJsonNames::numberOfArchsStreams> m_smInfoArray = {
        {{0}},
    };

    typedef std::array<std::array<InternalHostFenceInfo, HOST_FENCES_NR>, ScalJsonNames::numberOfArchsStreams>
                       FenceHostInfoArray;
    FenceHostInfoArray m_hostFenceInfoArray = {
        {{{{{0}}}}},
    };

protected:
    HclCommandsGen2Arch& m_commands;
    virtual void         init(CyclicBufferType type);
    void                 initScalData(CyclicBufferType type);

    std::unique_ptr<Gen2ArchScalWrapper> m_scalWrapper;
    std::array<std::array<Gen2ArchScalWrapper::CgComplex, (int)SchedulerType::count>,
               ScalJsonNames::numberOfArchsStreams>
        m_cgInfoArray = {
            {{{{{0}}}}},
    };
    std::array<std::unique_ptr<ArchStream>, ScalJsonNames::numberOfArchsStreams> m_archStreams;
    ScalJsonNames                                                                m_scalNames;
    std::vector<sibAddressAndSize>                                               m_staticBufferAddressesAndSizes;
};

}  // namespace hcl