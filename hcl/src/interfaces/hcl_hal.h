#pragma once

#include <cstdint>  // for uint32_t, uint64_t, uint16_t
#include <memory>   // for shared_ptr
#include <set>      // for set

#include "hcl_api_types.h"
#include "hcl_utils.h"
#include "hcl_types.h"  // for HCL_HwModuleId

#define NOT_IMPLEMENTED                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        VERIFY(false, "{} is not implemented for Gaudi2", __func__);                                                   \
        return -1;                                                                                                     \
    } while (false)

namespace hcl
{
class Hal
{
public:
    Hal()                      = default;
    virtual ~Hal()             = default;
    Hal(const Hal&)            = delete;
    Hal& operator=(const Hal&) = delete;

    // getters
    virtual uint64_t getMaxArchStreams() const = 0;
    virtual uint64_t getMaxQPsPerNic() const   = 0;
    virtual uint64_t getMaxNics() const        = 0;

    virtual uint32_t getMaxEDMAs() const = 0;

    virtual uint32_t getMaxQpPerInternalNic() const = 0;
    virtual uint32_t getMaxQpPerExternalNic() const = 0;
};

using HalPtr = std::shared_ptr<Hal>;

}  // namespace hcl
