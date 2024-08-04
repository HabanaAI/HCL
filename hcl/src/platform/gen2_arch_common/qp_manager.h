#pragma once

#include "interfaces/hcl_unique_sorted_vector.h"
#include "internal/hcl_api_types.h"

#include <cstdint>

constexpr uint32_t INVALID_QP = 0;
typedef uint32_t   qpn;

class QPManager
{
public:
    QPManager()          = default;
    virtual ~QPManager() = default;

    virtual void closeQPs(HCL_Comm comm, const UniqueSortedVector& ranks) = 0;
    inline bool  isInvalidQPn(uint32_t qpn) { return (qpn == 0 || qpn == INVALID_QP); };
};
