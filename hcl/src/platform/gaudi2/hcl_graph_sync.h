#pragma once

#include <stdint.h>                                    // for uint32_t, uint...
#include "platform/gen2_arch_common/hcl_graph_sync.h"  // for HclGraphSyncGe...
class HclCommandsGen2Arch;
namespace hcl
{
class ScalStream;
}

class HclGraphSyncGaudi2 : public HclGraphSyncGen2Arch
{
public:
    HclGraphSyncGaudi2(unsigned smIdx, HclCommandsGen2Arch& commands);
    HclGraphSyncGaudi2(HclGraphSyncGaudi2&&)                 = delete;
    HclGraphSyncGaudi2(const HclGraphSyncGaudi2&)            = delete;
    HclGraphSyncGaudi2& operator=(HclGraphSyncGaudi2&&)      = delete;
    HclGraphSyncGaudi2& operator=(const HclGraphSyncGaudi2&) = delete;
    virtual ~HclGraphSyncGaudi2();
    virtual uint32_t getSoConfigValue(unsigned value, bool isReduction) override;

private:
    virtual uint64_t getSyncManagerBase(unsigned smIdx) override;
    virtual uint32_t getAddrMonPayAddrl(uint64_t smBase, unsigned Idx) override;
    virtual uint32_t getAddrMonPayAddrh(uint64_t smBase, unsigned Idx) override;
    virtual uint32_t getAddrMonPayData(uint64_t smBase, unsigned Idx) override;
    virtual uint32_t getAddrMonConfig(uint64_t smBase, unsigned Idx) override;
    virtual uint32_t getAddrSobObj(uint64_t smBase, unsigned Idx) override;
    virtual uint32_t getRegSobObj(uint64_t smBase, unsigned Idx) override;
    virtual uint32_t getOffsetMonArm(unsigned Idx) override;
    virtual uint32_t createMonConfig(bool isLong, unsigned soQuarter) override;
    virtual uint32_t getArmMonSize() override;
    virtual uint32_t createMonArm(uint64_t       soValue,
                                  bool           longMon,
                                  const uint8_t  mask,
                                  const unsigned soIdxNoMask,
                                  int            i,
                                  bool           useEqual) override;
    virtual uint32_t createSchedMonExpFence(unsigned fenceIdx) override;
    virtual void     createSetupMonMessages(hcl::ScalStream& scalStream,
                                            uint64_t         address,
                                            unsigned         fenceIdx,
                                            unsigned         monitorIdx,
                                            uint64_t         smBase,
                                            bool             isLong) override;
};
