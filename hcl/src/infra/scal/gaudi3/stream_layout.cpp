
#include "infra/scal/gaudi3/stream_layout.h"
#include "infra/scal/gen2_arch_common/scal_names.h"

using namespace hcl;

Gaudi3StreamLayout::Gaudi3StreamLayout() : Gen2ArchStreamLayout()
{
    initLayout();
    initSchedulerStreamCount();
    verifyRules();
}

// clang-format off
void Gaudi3StreamLayout::initLayout()
{
    ScalJsonNames scalNames;
    //scheduler SU send
    SchedulersIndex schedIdx = SchedulersIndex::sendScaleUp;
    m_streamLayout[HclStreamIndex::SU_SEND_RS]   = {scalNames.schedulersNames[schedIdx], "rs", schedIdx, /*streamIndex*/ 0, eExternal};
    m_streamLayout[HclStreamIndex::SU_SEND_AG]   = {scalNames.schedulersNames[schedIdx], "ag", schedIdx, /*streamIndex*/ 1, eExternal};
    m_streamLayout[HclStreamIndex::SU_SEND_ARB]  = {scalNames.schedulersNames[schedIdx], "arb", schedIdx, /*streamIndex*/ 2, eExternal};

    //scheduler SU recv
    schedIdx = SchedulersIndex::recvScaleUp;
    m_streamLayout[HclStreamIndex::SU_RECV_RS]   = {scalNames.schedulersNames[schedIdx], "rs", schedIdx, /*streamIndex*/ 0, eExternal};
    m_streamLayout[HclStreamIndex::SU_RECV_AG]   = {scalNames.schedulersNames[schedIdx], "ag", schedIdx, /*streamIndex*/ 1, eExternal};
    m_streamLayout[HclStreamIndex::SU_RECV_ARB]  = {scalNames.schedulersNames[schedIdx], "arb", schedIdx, /*streamIndex*/ 2, eExternal};

    //scheduler SO send
    schedIdx = SchedulersIndex::sendScaleOut;
    m_streamLayout[HclStreamIndex::SO_SEND_RS]   = {scalNames.schedulersNames[schedIdx], "rs", schedIdx, /*streamIndex*/ 0, eExternal};
    m_streamLayout[HclStreamIndex::SO_SEND_AG]   = {scalNames.schedulersNames[schedIdx], "ag", schedIdx, /*streamIndex*/ 1, eExternal};
    m_streamLayout[HclStreamIndex::SO_SEND_ARB]  = {scalNames.schedulersNames[schedIdx], "arb", schedIdx, /*streamIndex*/ 2, eExternal};

    //scheduler SO recv
    schedIdx = SchedulersIndex::recvScaleOut;
    m_streamLayout[HclStreamIndex::SO_RECV_RS]   = {scalNames.schedulersNames[schedIdx], "rs", schedIdx, /*streamIndex*/ 0, eExternal};
    m_streamLayout[HclStreamIndex::SO_RECV_AG]   = {scalNames.schedulersNames[schedIdx], "ag", schedIdx, /*streamIndex*/ 1, eExternal};
    m_streamLayout[HclStreamIndex::SO_RECV_ARB]  = {scalNames.schedulersNames[schedIdx], "arb", schedIdx, /*streamIndex*/ 2, eExternal};
    m_streamLayout[HclStreamIndex::GC]           = {scalNames.schedulersNames[schedIdx], "gc", schedIdx, /*streamIndex*/ 3, eInternal};

    //scheduler general purpose
    schedIdx = SchedulersIndex::gp;
    m_streamLayout[HclStreamIndex::REDUCTION]    = {scalNames.schedulersNames[schedIdx], "red", schedIdx, /*streamIndex*/ 0, eExternal};
    m_streamLayout[HclStreamIndex::SO_REDUCTION] = {scalNames.schedulersNames[schedIdx], "sor", schedIdx, /*streamIndex*/ 1, eExternal};
    m_streamLayout[HclStreamIndex::GP_ARB]       = {scalNames.schedulersNames[schedIdx], "arb", schedIdx, /*streamIndex*/ 2, eExternal};
    m_streamLayout[HclStreamIndex::SIGNALING]    = {scalNames.schedulersNames[schedIdx], "sig", schedIdx, /*streamIndex*/ 3, eExternal};
    m_streamLayout[HclStreamIndex::GDR]          = {scalNames.schedulersNames[schedIdx], "gdr", schedIdx, /*streamIndex*/ 4, eExternal};
}