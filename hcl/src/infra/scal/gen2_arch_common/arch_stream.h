#pragma once

#include <array>                                        // for array
#include <cstdint>                                      // for uint64_t, uint32_t
#include <cstddef>                                      // for size_t
#include <memory>                                       // for shared_ptr
#include <vector>                                       // for vector
#include "completion_group.h"                           // for CompletionGroup
#include "scal.h"                                       // for scal_comp_group_handle_t
#include "scal_names.h"                                 // for ScalJsonNames, ScalJsonNames::numberOf...
#include "scal_types.h"                                 // for CgInfo, SmInfo
#include "factory_types.h"                              // for CyclicBufferType
#include "infra/scal/gen2_arch_common/stream_layout.h"  // for Gen2ArchStreamLayout

class HclCommandsGen2Arch;
namespace hcl
{
class Gen2ArchScalWrapper;
class ScalStream;
}  // namespace hcl

namespace hcl
{
/**
 * @brief ArchStream is responsible for managing all the microArch streams belong to it.
 *
 */
class ArchStream
{
public:
    ArchStream(unsigned                    archStreamIdx,
               Gen2ArchScalWrapper&        scalWrapper,
               scal_comp_group_handle_t    externalCgHandle,
               scal_comp_group_handle_t    internalCgHandle,
               ScalJsonNames&              scalNames,
               HclCommandsGen2Arch&        commands,
               CyclicBufferType            type,
               const Gen2ArchStreamLayout& streamLayout);

    ArchStream(ArchStream&&)                 = delete;
    ArchStream(const ArchStream&)            = delete;
    ArchStream& operator=(ArchStream&&)      = delete;
    ArchStream& operator=(const ArchStream&) = delete;
    ~ArchStream()                            = default;

    const SmInfo&              getSmInfo();
    const std::vector<CgInfo>& getCgInfo();
    hcl::ScalStream&           getScalStream(unsigned schedIdx, unsigned streamIdx);

    void synchronizeStream(uint64_t targetValue);

    void cgRegisterTimeStemp(uint64_t targetValue, uint64_t timestampHandle, uint32_t timestampsOffset);

    bool streamQuery(uint64_t targetValue);

    bool isACcbHalfFullForDeviceBenchMark();

    void disableCcb(bool disable);
    void dfaLog(hl_logger::LoggerSPtr synDevFailLog);

protected:
    unsigned            m_archStreamIdx;
    std::vector<CgInfo> m_cgInfo;
    SmInfo              m_smInfo;

    /**
     * @brief This data structure holds all logical stream per arch stream:
     *
     *  Data Structure:
     *
     *          schedIdx: 0 -> ("scaleup_receive")
     *                          streamMicro: 0 -> ("scaleup_receive0")
     *                          streamMicro: 1 -> ("scaleup_receive1")
     *                          .
     *                          .
     *                          streamMicro: 31 -> ("scaleup_receive7")
     *          schedIdx: 1 -> ("scaleup_send")
     *                          streamMicro: 0 -> ("scaleup_send0")
     *                          streamMicro: 1 -> ("scaleup_send1")
     *                          .
     *                          .
     *                          streamMicro: 31 -> ("scaleup_send7")
     *          .
     *          .
     *          schedIdx: 3 ->
     *
     */
    std::array<std::array<std::shared_ptr<ScalStream>, ScalJsonNames::numberOfMicroArchsStreamsPerScheduler>,
               (std::size_t)SchedulersIndex::count>
        m_streams;

    Gen2ArchScalWrapper& m_scalWrapper;

    /**
     * @brief Its needed to synchronize stream (wait on cg to get to target value)
     *
     */
    CompletionGroup m_externalCg;

    /**
     * @brief Its needed in context of internal cg for arch stream, we need to know
     *        when "job" that we have sent to the device is done in order to manage
     *        the cyclic buffer, therefor we hold a cg instance
     *
     */
    CompletionGroup m_internalCg;

    ScalJsonNames& m_scalNames;
};
}  // namespace hcl
