#pragma once
#include "platform/gen2_arch_common/hal.h"
#include "gaudi2/asic_reg/pcie_wrap_special_regs.h"  // for mmPCIE_WRAP_IND_ARMISC_INFO
#include "gaudi2/asic_reg/gaudi2_blocks.h"           // for mmPCIE_WRAP_BASE

namespace hcl
{
class Gaudi2Hal : public Gen2ArchHal
{
public:
    Gaudi2Hal()                            = default;
    virtual ~Gaudi2Hal()                   = default;
    Gaudi2Hal(const Gaudi2Hal&)            = delete;
    Gaudi2Hal& operator=(const Gaudi2Hal&) = delete;

    virtual uint32_t getMaxQpPerInternalNic() const override;
    virtual uint32_t getMaxQpPerExternalNic() const override;
    virtual uint32_t getCollectiveContextsCount() const;
    uint64_t         getMaxQPsPerNicNonPeer() const;

private:
    // The number of QPs per NIC is limited because each QP holds a WQE table, and the total number of
    // WQEs per NIC is 420520
    const uint32_t m_maxQpPerInternalNic = 100;
    const uint32_t m_maxQpPerExternalNic = GCFG_MAX_QP_PER_EXTERNAL_NIC.value();

    const uint32_t m_collectiveContextsCount = 16;
    const uint64_t m_maxQPsPerNicNonPeer     = 2;
};

}  // namespace hcl
