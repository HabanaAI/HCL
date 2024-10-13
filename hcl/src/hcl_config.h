#pragma once

#include <cstdint>  // for uint8_t, uint32_t
#include <vector>   // for vector

#include "interfaces/hcl_unique_sorted_vector.h"  // for UniqueSortedVector

/**
 * @class HclConfig is responsible to parse the HCL JSON configuration file passed to HCL_Init
 */
class HclConfig
{
public:
    HclConfig()                            = default;
    HclConfig(const HclConfig&)            = delete;
    HclConfig& operator=(const HclConfig&) = delete;

    bool init(const HCL_Rank rank, const uint32_t ranksCount);

    uint32_t                        m_commSize = 0;
    std::vector<UniqueSortedVector> m_communicators;  // list of communicators

    int m_jsonIndex = -1;
};
