#pragma once

#include <cstddef>  // for size_t
#include <cstdint>  // for uint32_t
#include <array>    // for array
#include <vector>   // for vector

#include "hcl_api_types.h"                    // for HCL_Comm
#include "hcl_types.h"                        // for MAX_MODULES_IDS_PER_SERVER
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH

// a vector of dwords commands per device, for all devices
typedef std::array<std::vector<uint32_t>, MAX_MODULES_IDS_PER_SERVER> DwordsBoxesArray;
// A vector of dwords commands per NIC / NIC macro pair
typedef std::array<std::vector<uint32_t>, MAX_NICS_GEN2ARCH> NicsDwordsArray;

struct UnionFindNode
{
    explicit UnionFindNode(const uint32_t value, const uint32_t dupMask)
    : m_value(value), m_dupMask(dupMask), root(nullptr)
    {
    }

    uint32_t m_value;
    uint32_t m_dupMask;

    UnionFindNode* root = nullptr;
};

class UnionFind
{
public:
    explicit UnionFind(const size_t size);
    void                       addNode(const uint32_t value, const uint32_t dupMask);
    std::vector<UnionFindNode> getRoots();

private:
    std::vector<UnionFindNode>  m_nodes;
    std::vector<UnionFindNode*> m_roots;
};

class NicPassthroughHandlerBase
{
public:
    NicPassthroughHandlerBase()          = default;
    virtual ~NicPassthroughHandlerBase() = default;

    virtual void addNicBuffer(const NicsDwordsArray& nicBuffer) = 0;
};
