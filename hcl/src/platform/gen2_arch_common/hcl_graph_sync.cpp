#include <utility>                                            // for pair
#include "hcl_utils.h"                                        // for VERIFY
#include "infra/scal/gen2_arch_common/scal_stream.h"          // for ScalStream
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclComm...
#include "platform/gen2_arch_common/hcl_graph_sync.h"
#include "platform/gen2_arch_common/hcl_lbw_write_aggregator.h"

void HclGraphSyncGen2Arch::addSetupMonitors(hcl::ScalStream& scalStream,
                                            unsigned         streamIdx,
                                            unsigned         monBaseIdx,
                                            unsigned         smIdx,
                                            uint64_t         monitorPayloadAddr,
                                            unsigned         fenceBase,
                                            unsigned         fenceIdx)
{
    for (unsigned monIdx = 0; monIdx < MONITORS_PER_FENCE; ++monIdx)
    {
        createSetupMonMessages(scalStream,
                               monitorPayloadAddr,
                               fenceBase + fenceIdx,
                               getRegularMonIdx(fenceIdx, monIdx, streamIdx) + monBaseIdx,
                               getSyncManagerBase(smIdx),
                               false);
    }
}

void HclGraphSyncGen2Arch::createSetupMonMessages(hcl::ScalStream& scalStream,
                                                  uint64_t         address,
                                                  unsigned         fenceIdx,
                                                  unsigned         monitorIdx,
                                                  uint64_t         smBase,
                                                  bool             isLong)
{
    LBWBurstDestData_t destData;
    unsigned           schedIdx = scalStream.getSchedIdx();

    // Setup to payload address (of the dccmQ of scheduler)
    uint32_t destination = getAddrMonPayAddrl(smBase, monitorIdx);
    destData.push_back({destination, (uint32_t)address & 0xffffffff});

    destination = getAddrMonPayAddrh(smBase, monitorIdx);
    destData.push_back({destination, (uint32_t)((address >> 32) & 0xffffffff)});

    uint32_t value = createSchedMonExpFence(fenceIdx);
    destination    = getAddrMonPayData(smBase, monitorIdx);
    destData.push_back({destination, value});

    unsigned soQuarter = monitorIdx % MONITORS_PER_FENCE;
    VERIFY(soQuarter <= 3);

    value = createMonConfig(isLong, soQuarter);

    destination = getAddrMonConfig(smBase, monitorIdx);
    destData.push_back({destination, value});

    m_commands.serializeLbwBurstWriteCommand(scalStream, schedIdx, destData);
}

uint16_t HclGraphSyncGen2Arch::getFifteenBits(uint64_t val, unsigned index)
{
    return (val >> (index * 15)) & 0x7fff;
}

void HclGraphSyncGen2Arch::createArmMonMessages(hcl::ScalStream& scalStream,
                                                uint64_t         soValue,
                                                unsigned         soIdx,
                                                unsigned         monitorIdx,
                                                uint64_t         smBase,
                                                unsigned         fenceIdx,
                                                bool             useEqual)
{
    const unsigned soIdxNoMask = soIdx >> 3;  // LSB are the mask, so unnecessary for long Sos
    const uint8_t  mask        = ~(1 << (soIdx % 8));
    VERIFY(soIdxNoMask <= 0x3ff);
    VERIFY(monitorIdx % 4 == (soIdxNoMask >> 8),
           "regular monitors are set up to the (monitorIdx % 4) quarter of the SM");

    // Arm from last to first, as message to the first indicates that the Arm is complete.
    const uint32_t baseAddrInSm = getOffsetMonArm(monitorIdx);

    uint32_t addr  = smBase + baseAddrInSm;
    uint32_t value = createMonArm(soValue, false, mask, soIdxNoMask, 0, useEqual);

    m_commands.serializeLbwWriteWithFenceDecCommand(scalStream, scalStream.getSchedIdx(), addr, value, fenceIdx);
}

void HclGraphSyncGen2Arch::createArmLongMonMessages(hcl::ScalStream& scalStream,
                                                    uint64_t         soValue,
                                                    unsigned         soIdx,
                                                    unsigned         monitorIdx,
                                                    uint64_t         smBase,
                                                    unsigned         fenceIdx,
                                                    bool             useEqual)
{
    const unsigned soIdxNoMask = soIdx >> 3;  // LSB are the mask, so unnecessary for long Sos
    const uint8_t  mask        = ~(1 << (soIdx % 8));
    VERIFY(soIdxNoMask <= 0x3ff);
    VERIFY((soIdxNoMask >> 8) == 0, "long monitors are set up to the first quarter of the SM");
    // Arm from last to first, as message to the first indicates that the Arm is complete.
    const uint32_t     monArmSize   = getArmMonSize();
    const uint32_t     baseAddrInSm = getOffsetMonArm(monitorIdx);
    LBWBurstDestData_t destData;

    for (int i = LONG_MON_DWORD_SIZE - 1; i >= 0; --i)
    {
        uint32_t addr  = smBase + baseAddrInSm + (i * monArmSize);
        uint32_t value = createMonArm(soValue, true, mask, soIdxNoMask, i, useEqual);

        if (i == 0 ||
            (m_longMonitorStatus.find(addr) == m_longMonitorStatus.end() || m_longMonitorStatus[addr] != value))
        {
            destData.push_back({addr, value});
            if (!m_nullSubmit)
            {
                m_longMonitorStatus[addr] = value;
            }
        }
    }
    m_commands.serializeLbwBurstWriteCommand(scalStream, scalStream.getSchedIdx(), destData);
    m_commands.serializeFenceDecCommand(scalStream, scalStream.getSchedIdx(), fenceIdx);
}

uint32_t HclGraphSyncGen2Arch::getCurrentCgSoAddr(CgType type)
{
    const auto& cgInfo = type == eExternal ? m_externalCgInfo : m_internalCgInfo;
    return getRegSobObj(cgInfo.cgBaseAddr, (m_currentCgSoIndex % cgInfo.size));
}

uint32_t HclGraphSyncGen2Arch::getCurrentGpsoAddr(WaitMethod waitMethod)
{
    const unsigned soIdx  = getCurrentGeneralPurposeSo(waitMethod);
    const uint32_t smBase = getSyncManagerBase(m_smIdx);
    return getRegSobObj(smBase, soIdx);
}

uint32_t HclGraphSyncGen2Arch::getCurrentLtuGpsoAddr(unsigned bufferIdx)
{
    const unsigned soIdx  = m_ltuInfo[bufferIdx].SOidx;
    const uint32_t smBase = getSyncManagerBase(m_smIdx);
    return getRegSobObj(smBase, soIdx);
}

uint32_t HclGraphSyncGen2Arch::getCurrentLtuGpsoIdx(unsigned bufferIdx)
{
    return m_ltuInfo[bufferIdx].SOidx;
}

uint32_t HclGraphSyncGen2Arch::getCurrentLtuGpsoData(unsigned bufferIdx, unsigned inc)
{
    unsigned& curSoVal = m_ltuInfo[bufferIdx].SOValue;
    if (curSoVal + inc > SO_MAX_VAL)
    {
        curSoVal = curSoVal + inc - SO_MAX_VAL;
    }
    else
    {
        curSoVal += inc;
    }
    return curSoVal;
}

uint32_t HclGraphSyncGen2Arch::getCurrentLongtermSoAddr(unsigned longtermIdx)
{
    const pool_s& pool = m_pools[(unsigned)waitMethodToGpsoPool(WaitMethod::GPSO_LONGTERM)];

    VERIFY(longtermIdx < (unsigned)m_currentLongtermAmount,
           "invalid longtermIdx provided: longtermIdx={}, m_currentLongtermAmount={}",
           longtermIdx,
           m_currentLongtermAmount);
    int64_t        longtermBase = m_currentLongtermGpso + longtermIdx + 1 - m_currentLongtermAmount;
    const unsigned soIdx        = (longtermBase % pool.size) + pool.baseIndex;
    const uint32_t smBase       = getSyncManagerBase(m_smIdx);
    return getRegSobObj(smBase, soIdx);
}

const uint32_t HclGraphSyncGen2Arch::getSoPoolBaseAddr(unsigned poolIdx)
{
    const uint32_t smBase = getSyncManagerBase(m_smIdx);
    const unsigned soIdx  = m_pools[poolIdx].baseIndex;
    return getRegSobObj(smBase, soIdx);
}

unsigned HclGraphSyncGen2Arch::getRegularMonIdx(unsigned fenceIdxInStream, unsigned monIdxInFence, unsigned streamIdx)
{
    unsigned fenceIdx = fenceIdxInStream + streamIdx * FENCES_PER_STREAM;
    return monIdxInFence + fenceIdx * MONITORS_PER_FENCE;
}

void HclGraphSyncGen2Arch::incSoIndex(unsigned credits)
{
    VERIFY(credits <= m_internalCgInfo.size, "Too many credits were asked");
    if (m_currentCgSoIndex + credits > m_internalCgInfo.size)
    {
        m_currentCgSoIndex = m_currentCgSoIndex + credits - m_internalCgInfo.size;
    }
    else
    {
        m_currentCgSoIndex += credits;
    }
}

void HclGraphSyncGen2Arch::incLongtermSoIndex(unsigned credits)
{
    m_currentLongtermGpso += credits;
    m_currentLongtermAmount = credits;
    VERIFY(m_currentLongtermAmount <= (m_currentLongtermGpso + 1) && "invalid longtermIdx provided");
}

int HclGraphSyncGen2Arch::getLongtermAmount()
{
    return m_currentLongtermAmount;
}

HclGraphSyncGen2Arch::HclGraphSyncGen2Arch(unsigned smIdx, HclCommandsGen2Arch& commands)
: m_commands(commands), m_smIdx(smIdx)
{
}

void HclGraphSyncGen2Arch::setCgInfo(hcl::CgInfo& externalCgInfo,
                                     hcl::CgInfo& internalCgInfo,
                                     unsigned     longtermGpsoPoolSize,
                                     unsigned     ltuGpsoPoolSize)
{
    m_internalCgInfo = internalCgInfo;
    m_externalCgInfo = externalCgInfo;

    m_currentCgSoIndex      = -1;
    m_currentLongtermGpso   = -1;
    m_currentLongtermAmount = 1;

    VERIFY(m_soSize >= longtermGpsoPoolSize + ltuGpsoPoolSize + m_internalCgInfo.size, "Not enough gpso are available");
    unsigned availableSos = m_soSize - (longtermGpsoPoolSize + ltuGpsoPoolSize);
    unsigned gpsoPoolSize = std::min(availableSos / 2, m_internalCgInfo.size);

    unsigned offset                               = 0;
    unsigned poolSizes[(unsigned)GpsoPool::COUNT] = {longtermGpsoPoolSize, ltuGpsoPoolSize, gpsoPoolSize, gpsoPoolSize};
    for (unsigned i = 0; i < (unsigned)GpsoPool::COUNT; ++i)
    {
        m_pools[i] = {.size = poolSizes[i], .baseIndex = m_syncObjectBase + offset};
        offset += poolSizes[i];
    }

    m_ltuInfo.resize(ltuGpsoPoolSize);
    m_ltuValid.resize(ltuGpsoPoolSize);
    uint32_t ltuBase = m_pools[(unsigned)GpsoPool::GPSO_LTU].baseIndex;
    for (unsigned i = 0; i < m_ltuInfo.size(); ++i)
    {
        m_ltuInfo[i]  = {.SOidx = ltuBase + i, .SOValue = 0};
        m_ltuValid[i] = {false, false};
    }
}

void HclGraphSyncGen2Arch::setSyncData(uint32_t syncObjectBase, unsigned soSize)
{
    VERIFY(GP_SO_PER_CG_ENTRY * m_internalCgInfo.size <= soSize, "No enough general purpose sync objects available");
    m_syncObjectBase = syncObjectBase;
    m_soSize         = soSize;
}

void HclGraphSyncGen2Arch::createSyncStreamsMessages(hcl::ScalStream& scalStream,
                                                     unsigned         monBase,
                                                     unsigned         syncDcoreIdx,
                                                     unsigned         soVal,
                                                     unsigned         soIdx,
                                                     unsigned         fenceIdx,
                                                     bool             useEqual)
{
    const uint32_t smBase    = getSyncManagerBase(syncDcoreIdx);
    const unsigned soTotalNr = 8192;
    unsigned       soQuarter = soIdx / (soTotalNr / 4);

    createArmMonMessages(scalStream,
                         soVal,
                         soIdx,
                         monBase + getRegularMonIdx(0, soQuarter, scalStream.getStreamIndex()),
                         smBase,
                         fenceIdx,
                         true);
}

void HclGraphSyncGen2Arch::createResetSoMessages(
    HclLbwWriteAggregator&                                         aggregator,
    uint32_t                                                       smIdx,
    const std::array<bool, (unsigned)WaitMethod::WAIT_METHOD_MAX>& methodsToClean)
{
    for (unsigned i = 0; i < methodsToClean.size(); i++)
    {
        if (methodsToClean[i])
        {
            WaitMethod waitMethod = (WaitMethod)i;
            int        sosToClean = (isLongTerm(waitMethod)) ? m_currentLongtermAmount : 1;
            for (int so = 0; so < sosToClean; ++so)
            {
                unsigned soIdx = getCurrentGeneralPurposeSo(waitMethod, so);
                LOG_HCL_DEBUG(HCL, "cleaning up method {} sob index {}", waitMethod, soIdx);
                uint32_t destination = getAddrSobObj(getSyncManagerBase(smIdx), soIdx);
                uint32_t data        = this->getSoConfigValue(0, false);
                aggregator.aggregate(destination, data);
            }
        }
    }
}

uint32_t HclGraphSyncGen2Arch::getCurrentGeneralPurposeSo(WaitMethod waitMethod, int longtermIdx)
{
    VERIFY(waitMethod == WaitMethod::GPSO_0 || waitMethod == WaitMethod::GPSO_1 || isLongTerm(waitMethod));

    const pool_s& pool         = m_pools[(unsigned)waitMethodToGpsoPool(waitMethod)];
    int64_t       longtermBase = m_currentLongtermGpso + longtermIdx + 1 - m_currentLongtermAmount;
    int64_t       currentIndex = isLongTerm(waitMethod) ? longtermBase : m_currentCgSoIndex;

    return pool.baseIndex + (currentIndex % pool.size);
}

void HclGraphSyncGen2Arch::createSoSignalMessage(hcl::ScalStream& scalStream,
                                                 unsigned         schedIdx,
                                                 unsigned         soIdx,
                                                 unsigned         soVal,
                                                 uint64_t         smBase,
                                                 bool             reduction)
{
    uint32_t destination = getAddrSobObj(smBase, soIdx);
    m_commands.serializeLbwWriteCommand(scalStream, schedIdx, destination, this->getSoConfigValue(soVal, reduction));
}

void HclGraphSyncGen2Arch::addPendingWait(uint32_t longSoIdx, uint64_t longSoVal)
{
    if (m_pendingWaits.find(longSoIdx) == m_pendingWaits.end() || m_pendingWaits[longSoIdx] < longSoVal)
    {
        m_pendingWaits[longSoIdx] = longSoVal;
    }
}

void HclGraphSyncGen2Arch::addWait(hcl::ScalStream&              scalStream,
                                   int                           streamId,
                                   unsigned                      dcoreIdx,
                                   uint32_t                      monIdx,
                                   std::map<uint32_t, uint64_t>& waitedValues,
                                   unsigned                      fenceIdx)
{
    for (auto& waitSo : m_pendingWaits)
    {
        if (waitedValues.find(waitSo.first) == waitedValues.end() || waitedValues[waitSo.first] < waitSo.second)
        {
            createArmLongMonMessages(scalStream,
                                     waitSo.second,
                                     waitSo.first,
                                     monIdx,
                                     getSyncManagerBase(dcoreIdx),
                                     fenceIdx,
                                     false /* waiting for longSo can't use equal to value */);

            LOG_TRACE(HCL_CG,
                      SCAL_PROGRESS_HCL_FMT "addWait: (uArchStream:{})",
                      streamId,
                      waitSo.first,
                      waitSo.second,
                      *scalStream.getSchedAndStreamName());

            if (!m_nullSubmit)
            {
                waitedValues[waitSo.first] = waitSo.second;
            }
        }
    }
}

void HclGraphSyncGen2Arch::addInternalWait(hcl::ScalStream& scalStream,
                                           unsigned         dcoreIdx,
                                           uint32_t         monIdx,
                                           uint64_t         soValue,
                                           unsigned         soIdx,
                                           unsigned         fenceIdx)
{
    createArmLongMonMessages(scalStream, soValue, soIdx, monIdx, getSyncManagerBase(dcoreIdx), fenceIdx, true);
}

void HclGraphSyncGen2Arch::addSetupLongMonitors(hcl::ScalStream& scalStream,
                                                unsigned         smIdx,
                                                uint64_t         monitorPayloadAddr,
                                                unsigned         monBaseIdx,
                                                unsigned         fenceBase,
                                                unsigned         fenceIdx)
{
    createSetupMonMessages(scalStream,
                           monitorPayloadAddr,
                           fenceIdx + fenceBase,
                           (fenceIdx * LONG_MONITOR_LENGTH) + monBaseIdx,
                           getSyncManagerBase(smIdx),
                           true);
}

bool HclGraphSyncGen2Arch::isForceOrder(bool external)
{
    const auto& cgInfo = external ? m_externalCgInfo : m_internalCgInfo;

    return cgInfo.forceOrder;
}

unsigned HclGraphSyncGen2Arch::getSoPoolSize(GpsoPool pool)
{
    return m_pools[(unsigned)pool].size;
}
