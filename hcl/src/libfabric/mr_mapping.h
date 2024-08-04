#pragma once

#include <cstddef>       // for NULL
#include <cstdint>       // for uint64_t, uint32_t
#include <vector>        // for vector
#include "hccl_types.h"  // for hcclResult_t

class ofi_component_t;

/**
 * @brief A singleton mapping between memory regions (MR, a combination of address and size) and theirs FDs and handles
 *
 */
class MRMapping
{
public:
    /**
     * @brief Get an instance of MRMapping
     *
     * @return MRMapping&
     */
    static MRMapping& get_instance()
    {
        static MRMapping mapping;
        return mapping;
    }

    MRMapping(const MRMapping&) = delete;
    MRMapping(MRMapping&&)      = delete;
    MRMapping& operator=(const MRMapping&) = delete;
    MRMapping& operator=(MRMapping&&) = delete;

    struct buffer_mapping_entry
    {
        uint64_t       addr;
        uint64_t       size;
        int            fd;
        struct fid_mr* mr_handle;
    };

    buffer_mapping_entry              curr_entry = {0, 0, 0, NULL};
    std::vector<buffer_mapping_entry> buffer_mapping_vec;

    /**
     * @brief Insert a buffer mapping entry into buffer_mapping_vec
     *
     * @param entry consists of address and size (Optional: FD and handle)
     * @return 0 if successfull
     */
    int update_buffer_mapping(buffer_mapping_entry& entry);

    /**
     * @brief Removes a buffer mapping entry from buffer_mapping_vec
     *
     * @param addr address of mapped buffer
     * @param size size of mapped buffer
     * @return 0 if successfull
     */
    int remove_from_mapping(uint64_t addr, uint64_t size);

    /**
     * @brief Update the handle of a mapped buffer mapping entry
     *
     * @param entry entry including a handle
     * @return 0 if successfull, -1 otherwise
     */
    int update_mr_handle(buffer_mapping_entry& entry);

    /**
     * @brief Search for a handle in mapping
     *
     * @param addr address of the buffer
     * @param size size of the buffer
     * @return handle if found, NULL otherwise
     */
    struct fid_mr* lookup_mr_handle(uint64_t addr, uint64_t size);

    /**
     * @brief Search for a FD in mapping
     *
     * @param addr address of the buffer
     * @param size size of the buffer
     * @return FD if found, 0 otherwise
     */
    int lookup_dma_buf_fd(uint64_t addr, uint64_t size);

    /**
     * @brief Map HBM (currently supported in gaudi-direct mode only) and update the buffer mapping vector
     *
     * @param addr address of the HBM to map
     * @param size size of the HBM to map
     * @param offset offset of addr to map
     * @param flags flags for HCL_BufferMap
     * @param ofiComponent ofi_component_t
     * @return 0 if successful
     */
    int mapDevMem(uint64_t addr, uint64_t size, uint64_t offset, uint32_t flags, ofi_component_t* ofiComponent);

    /**
     * @brief Map host-buffer memory (currently supported in verbs provider only) and update the buffer mapping vector
     *
     * @param addr address of the HBM to map
     * @param size size of the HBM to map
     * @param ofiComponent ofi_component_t
     * @param mr_handle output MR handle of the mapped memory
     * @return 0 if successful
     */
    hcclResult_t mapHostMem(uint64_t addr, uint64_t size, ofi_component_t* ofiComponent, struct fid_mr*& mr_handle);

    /**
     * @brief Map flush related memory regions.
     * @note Flush registrations are not saved in the mapping because they are done using a different domain.
     * @param ofiComponent ofi_component_t
     * @return 0 if successful
     */
    hcclResult_t mapFlushBufMem(ofi_component_t* ofiComponent);

    /**
     * @brief Close all open FDs in the buffer mapping vector
     *
     * @return 0 if successful, -1 otherwise
     */
    int closeFD();

    /**
     * @brief Close all registered MRs in the buffer mapping vector
     *
     * @return 0 if successful, -1 otherwise
     */
    int deregisterMR();

    /**
     * @brief Get DRAM size
     *
     * @return DRAM size
     */
    uint64_t getDramSize();

    /**
     * @brief Get DRAM base address
     *
     * @return DRAM base address
     */
    uint64_t getDramBaseAddr();

    /**
     * @brief Get max DRAM address
     *
     * @return max DRAM address
     */
    uint64_t getMaxDramAddr();

    /**
     * @brief Get the buffer for fabric flush
     *
     * @return void* pointer to flush buffer
     */
    void* getFlushBuf();

    struct fid_mr* getFlushMRLocalHandle();
    struct fid_mr* getFlushMRRemoteHandle();

private:
    uint64_t       m_dram_base = 0;
    uint64_t       m_dram_size = 0;
    int            m_flushBuf;
    struct fid_mr* m_flushMRLocalHandle  = NULL;
    struct fid_mr* m_flushMRRemoteHandle = NULL;
    MRMapping();
    ~MRMapping();
};
