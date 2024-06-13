#include "infra/scal/gaudi2/arch_stream.h"
#include "infra/scal/gaudi2/scal_stream.h"

hcl::Gaudi2ArchStream::Gaudi2ArchStream(unsigned                 streamIdx,
                                        Gen2ArchScalWrapper&     scalWrapper,
                                        scal_comp_group_handle_t externalCgHandle,
                                        scal_comp_group_handle_t internalCgHandle,
                                        ScalJsonNames&           scalNames,
                                        HclCommandsGen2Arch&     commands)
: ArchStream(streamIdx, scalWrapper, externalCgHandle, internalCgHandle, scalNames, commands)
{
    for (size_t schedIdx = 0; schedIdx < m_streams.size(); schedIdx++)
    {
        unsigned numOfStreamsBase = scalNames.numberOfMicroArchStreams[schedIdx] * streamIdx;
        for (size_t j = 0; j < scalNames.numberOfMicroArchStreams[schedIdx]; j++)
        {
            std::string name = std::string(scalNames.schedulersNames.at((SchedulersIndex)schedIdx)) +
                               std::to_string(numOfStreamsBase + j);

            CompletionGroup& cg =
                ((SchedulersIndex)schedIdx == SchedulersIndex::dma && (DMAStreams)j == DMAStreams::garbageCollection)
                    ? m_internalCg
                    : m_externalCg;

            m_streams[schedIdx][j] = std::make_shared<Gaudi2ScalStream>(scalNames,
                                                                        name,
                                                                        m_scalWrapper,
                                                                        cg,
                                                                        schedIdx,
                                                                        j,
                                                                        streamIdx,
                                                                        commands);
        }
    }
}