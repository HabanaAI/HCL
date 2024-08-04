#pragma once

#include <array>                                     // for array
#include <cstdint>                                   // for uint32_t, uint64_t
#include <map>                                       // for map
#include <vector>                                    // for map
#include "infra/scal/gen2_arch_common/scal_types.h"  // for CgInfo
#include "platform/gen2_arch_common/signals/types.h"

class HclCommandsGen2Arch;
namespace hcl
{
class ScalStream;
}

static const unsigned SO_VAL_BITS              = 15;
static const unsigned SO_MAX_VAL               = (1 << SO_VAL_BITS) - 1;
static const unsigned FENCE_MONITOR_IDX        = 0;
static const unsigned FENCE_BARRIER_IDX        = 1;
static const unsigned FENCES_PER_STREAM        = 2;
static const unsigned MONITORS_PER_FENCE       = 4;
static const unsigned MONITORS_PER_STREAM      = FENCES_PER_STREAM * MONITORS_PER_FENCE;
static const unsigned LONG_MONITORS_PER_STREAM = 1;
static const unsigned LONG_MONITOR_LENGTH      = 4;
static const unsigned GP_SO_PER_CG_ENTRY       = 2;

enum CgType
{
    eInternal = 0,
    eExternal = 1
};

struct LtuInfo
{
    unsigned SOidx;
    unsigned SOValue;
};

class HclGraphSyncGen2Arch
{
public:
    HclGraphSyncGen2Arch(unsigned syncSmIdx, HclCommandsGen2Arch& commands);
    HclGraphSyncGen2Arch(HclGraphSyncGen2Arch&&)      = delete;
    HclGraphSyncGen2Arch(const HclGraphSyncGen2Arch&) = delete;
    HclGraphSyncGen2Arch& operator=(HclGraphSyncGen2Arch&&) = delete;
    HclGraphSyncGen2Arch& operator=(const HclGraphSyncGen2Arch&) = delete;
    virtual ~HclGraphSyncGen2Arch()                              = default;

    void setCgInfo(hcl::CgInfo& externalCgInfo,
                   hcl::CgInfo& internalCgInfo,
                   unsigned     longtermGpsoPoolSize,
                   unsigned     ltuGpsoPoolSize);

    void addSetupMonitors(hcl::ScalStream& scalStream,
                          unsigned         streamIdx,
                          unsigned         monBaseIdx,
                          unsigned         smIdx,
                          uint64_t         monitorPayloadAddr,
                          unsigned         fenceBase,
                          unsigned         fenceIdx);

    void addSetupLongMonitors(hcl::ScalStream& scalStream,
                              unsigned         dcoreIdx,
                              uint64_t         monitorPayloadAddr,
                              unsigned         monBaseIdx,
                              unsigned         fenceBase,
                              unsigned         fenceIdx);

    unsigned getRegularMonIdx(unsigned fenceIdxInStream, unsigned monIdxInFence, unsigned streamIdx);

    uint32_t getCurrentCgSoAddr(CgType type);

    uint32_t getCurrentGpsoAddr(WaitMethod waitMethod);

    uint32_t getCurrentLtuGpsoAddr(unsigned bufferIdx);

    uint32_t getCurrentLtuGpsoIdx(unsigned bufferIdx);

    uint32_t getCurrentLtuGpsoData(unsigned bufferIdx, unsigned inc = 0);

    uint32_t getCurrentGeneralPurposeSo(WaitMethod waitMethod, int longtermIdx = 0);

    uint32_t getCurrentLongtermSoAddr(unsigned longtermOffset = 0);

    const uint32_t getSoPoolBaseAddr(unsigned poolIdx);

    const unsigned getSoPoolSize(unsigned poolIdx) { return m_pools[poolIdx].size; };

    virtual uint32_t getSoConfigValue(unsigned value, bool isReduction) = 0;

    const hcl::CgInfo& getCgData(bool external) { return external ? m_externalCgInfo : m_internalCgInfo; }

    void addPendingWait(uint32_t longSoIdx, uint64_t longSoVal);

    void incSoIndex(unsigned credits);
    void incLongtermSoIndex(unsigned credits);
    int  getLongtermAmount();

    void addWait(hcl::ScalStream&              scalStream,
                 int                           streamId,
                 unsigned                      dcoreIdx,
                 uint32_t                      monIdx,
                 std::map<uint32_t, uint64_t>& waitedValues,
                 unsigned                      fenceIdx);

    void addInternalWait(hcl::ScalStream& scalStream,
                         unsigned         dcoreIdx,
                         uint32_t         monIdx,
                         uint64_t         soValue,
                         unsigned         soIdx,
                         unsigned         fenceIdx);

    void setSyncData(uint32_t m_syncObjectBase, unsigned m_soSize);

    void createSyncStreamsMessages(hcl::ScalStream& scalStream,
                                   unsigned         monBase,
                                   unsigned         syncDcoreIdx,
                                   unsigned         soVal,
                                   unsigned         soIdx,
                                   unsigned         fenceIdx,
                                   bool             useEqual);

    void createResetSoMessages(hcl::ScalStream&                                               scalStream,
                               unsigned                                                       schedIdx,
                               uint32_t                                                       dcoreIdx,
                               const std::array<bool, (unsigned)WaitMethod::WAIT_METHOD_MAX>& methodsToClean);

    bool isForceOrder(bool external);

    virtual uint64_t getSyncManagerBase(unsigned)                      = 0;
    virtual uint32_t getRegSobObj(uint64_t smBase, unsigned Idx)       = 0;
    unsigned         getSoPoolSize(GpsoPool pool);

    void setNullSubmit(bool nullSubmit) { m_nullSubmit = nullSubmit; }
    inline std::vector<std::pair<bool, bool>>& getLtuData() { return m_ltuValid; }

protected:
    virtual uint32_t getAddrMonPayAddrl(uint64_t smBase, unsigned Idx) = 0;
    virtual uint32_t getAddrMonPayAddrh(uint64_t smBase, unsigned Idx) = 0;
    virtual uint32_t getAddrMonPayData(uint64_t smBase, unsigned Idx)  = 0;
    virtual uint32_t getAddrMonConfig(uint64_t smBase, unsigned Idx)   = 0;
    virtual uint32_t getAddrSobObj(uint64_t smBase, unsigned Idx)      = 0;
    virtual uint32_t getOffsetMonArm(unsigned Idx)                     = 0;
    virtual uint32_t createMonConfig(bool isLong, unsigned soQuarter)  = 0;
    virtual uint32_t getArmMonSize()                                   = 0;
    virtual uint32_t     createMonArm(uint64_t       soValue,
                                      bool           longMon,
                                      const uint8_t  mask,
                                      const unsigned soIdxNoMask,
                                      int            i,
                                      bool           useEqual)                                                    = 0;
    virtual uint32_t     createSchedMonExpFence(unsigned fenceIdx)                                      = 0;
    virtual void         createSetupMonMessages(hcl::ScalStream& scalStream,
                                                uint64_t         address,
                                                unsigned         fenceIdx,
                                                unsigned         monitorIdx,
                                                uint64_t         smBase,
                                                bool             isLong);
    uint16_t             getFifteenBits(uint64_t val, unsigned index);
    HclCommandsGen2Arch& m_commands;

private:
    void createArmMonMessages(hcl::ScalStream& scalStream,
                              uint64_t         soValue,
                              unsigned         soIdx,
                              unsigned         monitorIdx,
                              uint64_t         smBase,
                              bool             longMon,
                              unsigned         fenceIdx,
                              bool             useEqual = false);

    void createSoSignalMessage(hcl::ScalStream& scalStream,
                               unsigned         schedIdx,
                               unsigned         soIdx,
                               unsigned         soVal,
                               uint64_t         smBase,
                               bool             reduction);

    unsigned m_smIdx;

    hcl::CgInfo m_internalCgInfo = {
        0,
    };
    hcl::CgInfo m_externalCgInfo = {
        0,
    };

    int     m_currentCgSoIndex      = -1;
    int64_t m_currentLongtermGpso   = -1;
    int     m_currentLongtermAmount = 1;

    std::map<uint32_t, uint64_t> m_pendingWaits;
    std::map<uint32_t, uint32_t> m_longMonitorStatus;

    uint32_t m_syncObjectBase = (uint32_t)-1;
    unsigned m_soSize         = (unsigned)-1;

    struct pool_s
    {
        unsigned size;       // how many gpso there are in this pool
        uint32_t baseIndex;  // base sob index where this pool starts
    };
    std::array<struct pool_s, (std::size_t)GpsoPool::COUNT> m_pools;

    bool m_nullSubmit = false;

    std::vector<LtuInfo>               m_ltuInfo;
    std::vector<std::pair<bool, bool>> m_ltuValid;
};
