#pragma once

#include "infra/scal/gen2_arch_common/scal_wrapper.h"  // for Gen2ArchScalWr...
#include "scal.h"                                      // for scal_handle_t
namespace hcl
{
class ScalJsonNames;
}

namespace hcl
{
/**
 * @brief
 *
 * ScalWrapper is the lowest layer in the HCL SW stack, and its the only one that holds scal handle.
 * All calls goes to scal SW layer will go through ScalWrapper.
 * ScalWrapper is responsible logically on the memory pools.
 *
 */
class Gaudi3ScalWrapper : public Gen2ArchScalWrapper
{
public:
    Gaudi3ScalWrapper(int fd, ScalJsonNames& scalNames);
    Gaudi3ScalWrapper(Gaudi3ScalWrapper&&)                 = delete;
    Gaudi3ScalWrapper(const Gaudi3ScalWrapper&)            = delete;
    Gaudi3ScalWrapper& operator=(Gaudi3ScalWrapper&&)      = delete;
    Gaudi3ScalWrapper& operator=(const Gaudi3ScalWrapper&) = delete;
    virtual ~Gaudi3ScalWrapper();

    uint64_t getMonitorPayloadAddr(std::string name, unsigned fenceIdx) override;

private:
    uint64_t getArcAcpEng(unsigned smIndex) const;
};

}  // namespace hcl
