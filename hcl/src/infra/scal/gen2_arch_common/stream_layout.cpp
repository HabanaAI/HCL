#include "infra/scal/gen2_arch_common/stream_layout.h"
#include "hcl_utils.h"

void Gen2ArchStreamLayout::initSchedulerStreamCount()
{
    for (const auto& entry : m_streamLayout)
    {
        m_schedulerStreamCount[(unsigned)entry.schedIndex]++;
    }
}

void Gen2ArchStreamLayout::verifyRules()
{
    // These stream are not meant to be moved
    VERIFY(m_streamLayout[GP_ARB].schedIndex == hcl::SchedulersIndex::gp, "stream GP_ARB set to wrong scheduler");
    VERIFY(m_streamLayout[SU_SEND_ARB].schedIndex == hcl::SchedulersIndex::sendScaleUp,
           "stream SU_SEND_ARB set to wrong scheduler");
    VERIFY(m_streamLayout[SU_RECV_ARB].schedIndex == hcl::SchedulersIndex::recvScaleUp,
           "stream SU_RECV_ARB set to wrong scheduler");
    VERIFY(m_streamLayout[SO_SEND_ARB].schedIndex == hcl::SchedulersIndex::sendScaleOut,
           "stream SO_SEND_ARB set to wrong scheduler");
    VERIFY(m_streamLayout[SO_RECV_ARB].schedIndex == hcl::SchedulersIndex::recvScaleOut,
           "stream SO_RECV_ARB set to wrong scheduler");

    VERIFY(m_streamLayout[SU_SEND_AG].schedIndex == hcl::SchedulersIndex::sendScaleUp,
           "stream SU_SEND_AG set to wrong scheduler");
    VERIFY(m_streamLayout[SU_RECV_AG].schedIndex == hcl::SchedulersIndex::recvScaleUp,
           "stream SU_RECV_AG set to wrong scheduler");
    VERIFY(m_streamLayout[SO_SEND_AG].schedIndex == hcl::SchedulersIndex::sendScaleOut,
           "stream SO_SEND_AG set to wrong scheduler");
    VERIFY(m_streamLayout[SO_RECV_AG].schedIndex == hcl::SchedulersIndex::recvScaleOut,
           "stream SO_RECV_AG set to wrong scheduler");

    VERIFY(m_streamLayout[SU_SEND_RS].schedIndex == hcl::SchedulersIndex::sendScaleUp,
           "stream SU_SEND_RS set to wrong scheduler");
    VERIFY(m_streamLayout[SU_RECV_RS].schedIndex == hcl::SchedulersIndex::recvScaleUp,
           "stream SU_RECV_RS set to wrong scheduler");
    VERIFY(m_streamLayout[SO_SEND_RS].schedIndex == hcl::SchedulersIndex::sendScaleOut,
           "stream SO_SEND_RS set to wrong scheduler");
    VERIFY(m_streamLayout[SO_RECV_RS].schedIndex == hcl::SchedulersIndex::recvScaleOut,
           "stream SO_RECV_RS set to wrong scheduler");

    // Make sure that all streams have unique indicies
    // Create an array to track seen scalUarchstreamIndex values for each scheduler
    std::array<std::unordered_set<unsigned>, hcl::SchedulersIndex::count> seenIndices;
    // Iterate over each entry in the stream layout
    for (const auto& entry : m_streamLayout)
    {
        // Get the set of indices for the current scheduler
        auto& indices = seenIndices[entry.schedIndex];

        // Try to insert the current scalUarchstreamIndex into the set
        // If insertion fails, it means the index is a duplicate
        VERIFY(indices.insert(entry.scalUarchstreamIndex).second,
               "there are 2 streams with the same index for scheduler " + std::to_string(entry.schedIndex));
    }

    // Verify that m_schedulerStreamCount[i] never exceeds 6
    for (unsigned i = 0; i < m_schedulerStreamCount.size(); ++i)
    {
        VERIFY(m_schedulerStreamCount[i] <= MAX_STREAM_PER_SCHED,
               "Scheduler stream count exceeds 6 for scheduler " + std::to_string(i));
    }
}

const StreamInfo& Gen2ArchStreamLayout::getUarchStreamInfo(HclStreamIndex hclStreamIdx) const
{
    return m_streamLayout[hclStreamIdx];
};

const StreamInfo& Gen2ArchStreamLayout::getScalArbUarchStream(hcl::SchedulersIndex schedIdx) const
{
    static const std::array<HclStreamIndex, hcl::SchedulersIndex::count> arbStreamIndices = {
        GP_ARB,
        SU_SEND_ARB,
        SU_RECV_ARB,
        SO_SEND_ARB,
        SO_RECV_ARB,
    };
    return m_streamLayout[arbStreamIndices[schedIdx]];
}

// clang-format off
const StreamInfo& Gen2ArchStreamLayout::getScalAgUarchStream(hcl::SchedulersIndex schedIdx) const
{
    static const std::array<HclStreamIndex, hcl::SchedulersIndex::count - 1> agStreamIndices = {
        SU_SEND_AG,
        SU_RECV_AG,
        SO_SEND_AG,
        SO_RECV_AG
    };
    return m_streamLayout[agStreamIndices[schedIdx - 1]];
}

const StreamInfo& Gen2ArchStreamLayout::getScalRsUarchStream(hcl::SchedulersIndex schedIdx) const
{
    static const std::array<HclStreamIndex, hcl::SchedulersIndex::count -1> rsStreamIndices = {
        SU_SEND_RS,
        SU_RECV_RS,
        SO_SEND_RS,
        SO_RECV_RS
    };
    return m_streamLayout[rsStreamIndices[schedIdx - 1]];
}
// clang-format on

unsigned Gen2ArchStreamLayout::getSchedulerMicroArchStreamCount(unsigned schedIdx)
{
    return m_schedulerStreamCount[schedIdx];
}

// Function to get the scheduler name and stream number based on the stream index and layout
std::string Gen2ArchStreamLayout::getSchedNameAndStreamNum(unsigned archStreamIdx, HclStreamIndex hclStreamIdx) const
{
    // Retrieve the stream information for the given stream index
    StreamInfo streamInfo = m_streamLayout[hclStreamIdx];

    // Calculate the base value using the scheduler stream count and arch stream
    unsigned base = m_schedulerStreamCount[streamInfo.schedIndex] * archStreamIdx;

    // Return the scheduler name concatenated with the stream number
    return streamInfo.schedName + std::to_string(streamInfo.scalUarchstreamIndex + base);
}

// Function to get the scheduler name and stream name based on the stream index and layout
std::string Gen2ArchStreamLayout::getSchedNameAndStreamName(HclStreamIndex hclStreamIdx) const
{
    // Retrieve the stream information for the given stream index
    StreamInfo streamInfo = m_streamLayout[hclStreamIdx];

    // Return the scheduler name concatenated with the micro-architecture stream name
    return streamInfo.schedName + "-" + streamInfo.uarchStreamName;
}