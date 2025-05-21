#include "arch_stream.h"

#include <cstddef>                                         // for size_t
#include <map>                                             // for map
#include <string>                                          // for operator+
#include "hcl_utils.h"                                     // for UNUSED
#include "infra/scal/gen2_arch_common/completion_group.h"  // for Completion...
#include "infra/scal/gen2_arch_common/scal_types.h"        // for CgInfo
#include "scal_names.h"                                    // for ScalJsonNames
#include "scal_stream.h"                                   // for ScalStream
#include "infra/scal/gen2_arch_common/cyclic_buffer_manager.h"

class HclCommandsGen2Arch;
namespace hcl
{
class Gen2ArchScalWrapper;
}

using namespace hcl;

ArchStream::ArchStream(unsigned                    archStreamIdx,
                       Gen2ArchScalWrapper&        scalWrapper,
                       scal_comp_group_handle_t    externalCgHandle,
                       scal_comp_group_handle_t    internalCgHandle,
                       ScalJsonNames&              scalNames,
                       HclCommandsGen2Arch&        commands,
                       CyclicBufferType            type,
                       const Gen2ArchStreamLayout& streamLayout)
: m_archStreamIdx(archStreamIdx),
  m_scalWrapper(scalWrapper),
  m_externalCg(scalWrapper, externalCgHandle),
  m_internalCg(scalWrapper, internalCgHandle),
  m_scalNames(scalNames)
{
    // Iterate over each stream in the stream layout
    for (unsigned i = 0; i < streamLayout.getTotalMicroArchStreamCount(); i++)
    {
        HclStreamIndex hclStreamIdx = (HclStreamIndex)i;
        // Get the scheduler index and micro-architecture stream index for the current stream
        size_t schedIdx       = streamLayout.getUarchStreamInfo(hclStreamIdx).schedIndex;
        size_t uarchStreamIdx = streamLayout.getUarchStreamInfo(hclStreamIdx).scalUarchstreamIndex;

        // Get the scheduler name and stream number for the current stream
        std::string schedNameAndStreamNum = streamLayout.getSchedNameAndStreamNum(m_archStreamIdx, hclStreamIdx);
        // Get the scheduler name and micro-architecture stream name for the current stream
        std::string schedAndStreamName = streamLayout.getSchedNameAndStreamName(hclStreamIdx);

        // Determine the appropriate completion group based on the stream index
        CompletionGroup& cg =
            streamLayout.getUarchStreamInfo(hclStreamIdx).cgType == eInternal ? m_internalCg : m_externalCg;

        // Create a new ScalStream object and store it in the m_streams array
        m_streams[schedIdx][uarchStreamIdx] = std::make_shared<ScalStream>(m_scalNames,
                                                                           schedNameAndStreamNum,
                                                                           schedAndStreamName,
                                                                           m_scalWrapper,
                                                                           cg,
                                                                           schedIdx,
                                                                           uarchStreamIdx,
                                                                           archStreamIdx,
                                                                           commands,
                                                                           type);
    }
}

const SmInfo& ArchStream::getSmInfo()
{
    return m_smInfo;
}

const std::vector<CgInfo>& ArchStream::getCgInfo()
{
    return m_cgInfo;
}

hcl::ScalStream& ArchStream::getScalStream(unsigned schedIdx, unsigned streamIdx)
{
    return *m_streams[schedIdx][streamIdx];
}

void ArchStream::synchronizeStream(uint64_t targetValue)
{
    m_externalCg.waitOnValue(targetValue);
}

void ArchStream::cgRegisterTimeStemp(uint64_t targetValue, uint64_t timestampHandle, uint32_t timestampsOffset)
{
    m_externalCg.cgRegisterTimeStemp(targetValue, timestampHandle, timestampsOffset);
}

bool ArchStream::streamQuery(uint64_t targetValue)
{
    return m_externalCg.checkForTargetValue(targetValue);
}

void ArchStream::disableCcb(bool disable)
{
    for (unsigned i = 0; i < (std::size_t)SchedulersIndex::count; i++)
    {
        for (int j = 0; j < ScalJsonNames::numberOfMicroArchsStreamsPerScheduler; j++)
        {
            if (m_streams[i][j])
            {
                m_streams[i][j]->disableCcb(disable);
            }
        }
    }
}

bool ArchStream::isACcbHalfFullForDeviceBenchMark()
{
    return ScalStream::isACcbHalfFullForDeviceBenchMark();
}

void ArchStream::dfaLog(hl_logger::LoggerSPtr synDevFailLog)
{
    for (unsigned i = 0; i < (std::size_t)SchedulersIndex::count; i++)
    {
        for (int j = 0; j < ScalJsonNames::numberOfMicroArchsStreamsPerScheduler; j++)
        {
            if (m_streams[i][j])
            {
                m_streams[i][j]->dfaLog(synDevFailLog);
            }
        }
    }
}
