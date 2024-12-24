#pragma once

#include <cstddef>       // for NULL
#include <cstdint>       // for uint64_t, uint32_t
#include <vector>        // for vector
#include <memory>        // for make_shared
#include <optional>      // for optional
#include <tuple>         // for tuple
#include "hccl_types.h"  // for hcclResult_t

class ofi_component_t;

struct MRParams
{
    std::optional<uint64_t> m_addr;
    std::optional<uint64_t> m_size;
    std::optional<int>      m_fd;
    std::optional<uint64_t> m_offset;
};

/**
 * @brief Register memory region (host or device) and export the memory region handle and file descriptor.
 */
class MemoryRegion
{
public:
    MemoryRegion(const MRParams& params, ofi_component_t* ofiComponent);
    ~MemoryRegion();

    MemoryRegion(const MemoryRegion&)            = delete;
    MemoryRegion(MemoryRegion&&)                 = delete;
    MemoryRegion& operator=(const MemoryRegion&) = delete;
    MemoryRegion& operator=(MemoryRegion&&)      = delete;

    /**
     * @brief Getter for the MR handle

     * @return handle as optional
     */
    struct fid_mr* getMRHandle() const;

    /**
     * @brief Getter for the single qp MR handle

     * @return handle as optional
     */
    struct fid_mr* getSingleQpMrHandle() const;

    /**
     * @brief Getter for the dmabuf FD

     * @return FD as optional
     */
    int getDmabufFd() const;

    /**
     * @brief Get the host buffer for fabric flush
     *
     * @return pointer to flush buffer
     */
    uint64_t getLocalFlushBuf() const;

    /**
     * @brief Get the device buffer for fabric flush
     *
     * @return pointer to flush buffer
     */
    uint64_t getRemoteFlushBuf() const;

    /**
     * @brief Get the host MR handle for fabric flush
     *
     * @return pointer to handle
     */
    struct fid_mr* getFlushMRLocalHandle() const;

    /**
     * @brief Get the device MR handle for fabric flush
     *
     * @return pointer to handle
     */
    struct fid_mr* getFlushMRRemoteHandle() const;

protected:
    /**
     * @brief register HBM (currently supported in gaudi-direct mode only)
     *
     * @param params fd, address, offset and size in the HBM to map
     * @param ofiComponent ofi_component_t
     * @return 0 if successful
     */
    hcclResult_t registerDevMR(const MRParams& params, ofi_component_t* ofiComponent);

    /**
     * @brief register host-buffer memory (currently supported in verbs provider only)
     *
     * @param params address and size of the memory to map
     * @param ofiComponent ofi_component_t
     * @return 0 if successful
     */
    hcclResult_t registerHostMR(const MRParams& params, ofi_component_t* ofiComponent);

    /**
     * @brief register flush related memory regions.
     *
     * @param params flush buffer address to map
     * @param ofiComponent ofi_component_t
     * @return 0 if successful
     */
    hcclResult_t registerFlushBufMR(const MRParams& params, ofi_component_t* ofiComponent);

private:
    /* Used for device or host memory. currently there is no need for both at the same time. */
    struct fid_mr*                m_handle;
    std::optional<struct fid_mr*> m_handle_single;

    /* Used for device memory. */
    std::optional<int> m_dmabufFD;

    /* Buffers for flush mechanism. */
    std::optional<int>      m_flushLocalBuffer;
    std::optional<uint64_t> m_flushRemoteBuffer;

    /* Handles for flush mechanism. */
    std::optional<struct fid_mr*> m_flushLocalHandle;
    std::optional<struct fid_mr*> m_flushRemoteHandle;
};
