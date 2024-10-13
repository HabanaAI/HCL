#pragma once

#include <cstdint>  // for uint32_t, uint...
#include "platform/gen2_arch_common/hcl_graph_sync.h"

class HclCommandsGen2Arch;

class HclGraphSyncGaudi3 : public HclGraphSyncGen2Arch
{
public:
    HclGraphSyncGaudi3(unsigned syncSmIdx, HclCommandsGen2Arch& commands);
    HclGraphSyncGaudi3(HclGraphSyncGaudi3&&)                 = delete;
    HclGraphSyncGaudi3(const HclGraphSyncGaudi3&)            = delete;
    HclGraphSyncGaudi3& operator=(HclGraphSyncGaudi3&&)      = delete;
    HclGraphSyncGaudi3& operator=(const HclGraphSyncGaudi3&) = delete;
    virtual ~HclGraphSyncGaudi3()                            = default;
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
    virtual uint32_t createSchedMonExpFence(unsigned fenceIdx) override;
    virtual uint32_t getArmMonSize() override;
    virtual uint32_t createMonArm(uint64_t       soValue,
                                  bool           longMon,
                                  const uint8_t  mask,
                                  const unsigned soIdxNoMask,
                                  int            i,
                                  bool           useEqual) override;
};
