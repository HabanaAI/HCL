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
class Gaudi2ScalWrapper : public Gen2ArchScalWrapper
{
public:
    Gaudi2ScalWrapper(int fd, ScalJsonNames& scalNames);
    Gaudi2ScalWrapper(Gaudi2ScalWrapper&&)                 = delete;
    Gaudi2ScalWrapper(const Gaudi2ScalWrapper&)            = delete;
    Gaudi2ScalWrapper& operator=(Gaudi2ScalWrapper&&)      = delete;
    Gaudi2ScalWrapper& operator=(const Gaudi2ScalWrapper&) = delete;
    virtual ~Gaudi2ScalWrapper();

    uint64_t getMonitorPayloadAddr(std::string name, unsigned fenceIdx) override;
};

}  // namespace hcl
