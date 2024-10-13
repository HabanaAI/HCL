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

ArchStream::ArchStream(unsigned                 streamIdx,
                       Gen2ArchScalWrapper&     scalWrapper,
                       scal_comp_group_handle_t externalCgHandle,
                       scal_comp_group_handle_t internalCgHandle,
                       ScalJsonNames&           scalNames,
                       HclCommandsGen2Arch&     commands,
                       CyclicBufferType         type)
: m_streamIdx(streamIdx),
  m_scalWrapper(scalWrapper),
  m_externalCg(scalWrapper, externalCgHandle),
  m_internalCg(scalWrapper, internalCgHandle),
  m_scalNames(scalNames)
{
    for (size_t schedIdx = 0; schedIdx < m_streams.size(); schedIdx++)
    {
        unsigned numOfStreamsBase = scalNames.numberOfMicroArchStreams[schedIdx] * streamIdx;
        for (size_t j = 0; j < scalNames.numberOfMicroArchStreams[schedIdx]; j++)
        {
            unsigned    streamNum = numOfStreamsBase + j;
            std::string schedNameAndStreamNum =
                std::string(scalNames.schedulersNames.at((SchedulersIndex)schedIdx)) + std::to_string(streamNum);

            std::string streamName = "";
            if (schedIdx && (NetworkStreams)(streamNum) < NetworkStreams::max)
            {
                streamName = std::string(scalNames.networkStreamNames.at((NetworkStreams)(streamNum)));
            }
            else if (!schedIdx && (DMAStreams)(streamNum) < DMAStreams::max)
            {
                streamName = std::string(scalNames.dmaStreamNames.at((DMAStreams)(streamNum)));
            }
            else
            {
                streamName = std::to_string(streamNum);
            }
            std::string schedAndStreamName =
                std::string(scalNames.schedulersNames.at((SchedulersIndex)schedIdx)) + "-" + streamName;

            CompletionGroup& cg =
                ((SchedulersIndex)schedIdx == SchedulersIndex::dma && (DMAStreams)j == DMAStreams::garbageCollection)
                    ? m_internalCg
                    : m_externalCg;

            m_streams[schedIdx][j] = std::make_shared<ScalStream>(scalNames,
                                                                  schedNameAndStreamNum,
                                                                  schedAndStreamName,
                                                                  m_scalWrapper,
                                                                  cg,
                                                                  schedIdx,
                                                                  j,
                                                                  streamIdx,
                                                                  commands,
                                                                  type);
        }
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
