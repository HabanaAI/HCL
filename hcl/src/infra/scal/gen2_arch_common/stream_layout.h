#pragma once
#include "infra/scal/gen2_arch_common/stream_layout.h"
#include "infra/scal/gen2_arch_common/scal_names.h"
#include "infra/scal/gen2_arch_common/scal_types.h"
#include <string>
#include <array>

enum HclStreamIndex
{
    REDUCTION,     // 0
    SO_REDUCTION,  // 1
    GP_ARB,        // 2
    SIGNALING,     // 3
    GDR,           // 4
    SU_SEND_RS,    // 5
    SU_SEND_AG,    // 6
    SU_SEND_ARB,   // 7
    SU_RECV_RS,    // 8
    SU_RECV_AG,    // 9
    SU_RECV_ARB,   // 10
    SO_SEND_RS,    // 11
    SO_SEND_AG,    // 12
    SO_SEND_ARB,   // 13
    SO_RECV_RS,    // 14
    SO_RECV_AG,    // 15
    SO_RECV_ARB,   // 16
    GC,            // 17
    COUNT          // 18
};

class StreamInfo
{
public:
    std::string schedName;
    std::string uarchStreamName;
    unsigned    schedIndex;
    unsigned    scalUarchstreamIndex;
    CgType      cgType;
};

using StreamLayout         = std::array<StreamInfo, (unsigned)HclStreamIndex::COUNT>;
using SchedulerStreamCount = std::array<size_t, hcl::SchedulersIndex::count>;

class Gen2ArchStreamLayout
{
public:
    Gen2ArchStreamLayout()                                       = default;
    Gen2ArchStreamLayout(Gen2ArchStreamLayout&&)                 = delete;
    Gen2ArchStreamLayout(const Gen2ArchStreamLayout&)            = delete;
    Gen2ArchStreamLayout& operator=(Gen2ArchStreamLayout&&)      = delete;
    Gen2ArchStreamLayout& operator=(const Gen2ArchStreamLayout&) = delete;
    virtual ~Gen2ArchStreamLayout() {};

    const StreamInfo& getUarchStreamInfo(HclStreamIndex hclStreamIdx) const;
    const StreamInfo& getScalArbUarchStream(hcl::SchedulersIndex schedIdx) const;
    const StreamInfo& getScalAgUarchStream(hcl::SchedulersIndex schedIdx) const;
    const StreamInfo& getScalRsUarchStream(hcl::SchedulersIndex schedIdx) const;
    unsigned          getSchedulerMicroArchStreamCount(unsigned schedIdx);

    inline unsigned getTotalMicroArchStreamCount() const { return (unsigned)HclStreamIndex::COUNT; }

    std::string getSchedNameAndStreamNum(unsigned archStreamIdx, HclStreamIndex hclStreamIdx) const;
    std::string getSchedNameAndStreamName(HclStreamIndex hclStreamIdx) const;

protected:
    virtual void         initLayout() = 0;
    void                 initSchedulerStreamCount();
    void                 verifyRules();
    SchedulerStreamCount m_schedulerStreamCount {};
    StreamLayout         m_streamLayout;
};