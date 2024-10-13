#include "platform/gen2_arch_common/nic_passthrough_handler_base.h"

#include <cstdint>  // for uint32_t
#include <cstring>  // for memset, memcpy

#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_*

UnionFind::UnionFind(const size_t size)
{
    m_nodes.reserve(size);
    m_roots.reserve(size);
}

void UnionFind::addNode(const uint32_t value, const uint32_t dupMask)
{
    m_nodes.emplace_back(value, dupMask);
    UnionFindNode& newNode = *m_nodes.rbegin();

    bool merged = false;
    for (UnionFindNode* root : m_roots)
    {
        if (root->m_value == newNode.m_value)
        {
            merged       = true;
            newNode.root = root;

            root->m_dupMask |= newNode.m_dupMask;

            break;
        }
    }

    if (!merged)
    {
        m_roots.push_back(&newNode);
    }
}

std::vector<UnionFindNode> UnionFind::getRoots()
{
    std::vector<UnionFindNode> result;
    for (const UnionFindNode* root : m_roots)
    {
        result.emplace_back(root->m_value, root->m_dupMask);
    }

    return result;
}
