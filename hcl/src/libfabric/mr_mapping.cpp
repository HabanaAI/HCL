#include "mr_mapping.h"
#include <unistd.h>  // for close
#include <cstring>   // for strerror
#include "platform/gen2_arch_common/hccl_device.h"
#include "hcl_utils.h"                   // for LOG_HCL_DEBUG, LOG_HCL_ERR
#include "interfaces/hcl_idevice.h"      // for IHclDevice
#include "libfabric/hl_ofi.h"            // for OFI_UNLIKELY
#include "libfabric/hl_ofi_component.h"  // for ofi_component_t
#include "hcl_log_manager.h"             // for LOG*
#include "rdma/fi_domain.h"              // for FI_HMEM_SYNAPSEAI
#include "hlthunk.h"                     // for hlthunk_device_mapped_memory_export_dmabuf_fd

#define ALIGN_SIZE 134217728  // 128MB

int MRMapping::update_buffer_mapping(buffer_mapping_entry& entry)
{
    LOG_HCL_DEBUG(HCL_OFI,
                  "Updating buffer mapping with addr: [0x{:x}] size: [0x{:x}] ({:g}MB).",
                  entry.addr,
                  entry.size,
                  B2MB(entry.size));
    buffer_mapping_vec.push_back(entry);
    return 0;
}

int MRMapping::update_mr_handle(buffer_mapping_entry& entry)
{
    for (auto& mapping_entry : buffer_mapping_vec)
    {
        if (mapping_entry.addr == entry.addr && mapping_entry.size == entry.size && mapping_entry.mr_handle == NULL)
        {
            mapping_entry.mr_handle = entry.mr_handle;
            return 0;
        }
    }
    return -1;
}

int MRMapping::remove_from_mapping(uint64_t addr, uint64_t size)
{
    std::vector<buffer_mapping_entry>::iterator iter;
    for (iter = buffer_mapping_vec.begin(); iter != buffer_mapping_vec.end(); ++iter)
    {
        if ((*iter).addr <= addr && addr + size <= (*iter).addr + (*iter).size)
        {
            buffer_mapping_vec.erase(iter);
            break;
        }
    }
    return 0;
}

int MRMapping::lookup_dma_buf_fd(uint64_t addr, uint64_t size)
{
    // The user can send any type of addr to this method since it is an external API call.
    // We need to validate that the addr requested by the user is:
    // 1) Not higher than the actual HBM max addr.
    // 2) Would not cause overflow.
    if (!(addr + size <= getMaxDramAddr()))
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Requested address [0x{:x}] and size [{:g}MB] for a lookup are invalid (higher than max HBM addr "
                    "[0x{:x}]).",
                    addr,
                    B2MB(size),
                    getMaxDramAddr());
        return -1;
    }
    if (!(addr + size > addr))
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Requested address [0x{:x}] and size [{:g}MB] for a lookup are invalid (overflow).",
                    addr,
                    B2MB(size));
        return -1;
    }

    if (curr_entry.size == size && curr_entry.addr == addr)
    {
        LOG_HCL_DEBUG(HCL_OFI,
                      "Found (current entry) in buffer mapping (FD), addr: [0x{:x}] size: [0x{:x}] ({:g}MB).",
                      addr,
                      size,
                      B2MB(size));
        return curr_entry.fd;
    }
    for (auto& mapping_entry : buffer_mapping_vec)
    {
        if (mapping_entry.addr <= addr && addr + size <= mapping_entry.addr + mapping_entry.size)
        {
            LOG_HCL_DEBUG(HCL_OFI,
                          "Found in buffer mapping (FD), addr: [0x{:x}] size: [0x{:x}] ({:g}MB).",
                          addr,
                          size,
                          B2MB(size));
            return mapping_entry.fd;
        }
    }
    LOG_HCL_DEBUG(HCL_OFI,
                  "Missed in buffer mapping (FD), addr: [0x{:x}] size: [0x{:x}] ({:g}MB).",
                  addr,
                  size,
                  B2MB(size));
    return 0;
}

struct fid_mr* MRMapping::lookup_mr_handle(uint64_t addr, uint64_t size)
{
    for (auto& mapping_entry : buffer_mapping_vec)
    {
        if (mapping_entry.addr <= addr && addr + size <= mapping_entry.addr + mapping_entry.size)
        {
            LOG_HCL_DEBUG(HCL_OFI,
                          "Found in buffer mapping (mr handle), addr: [0x{:x}] size: [0x{:x}] ({:g}MB).",
                          addr,
                          size,
                          B2MB(size));
            return mapping_entry.mr_handle;
        }
    }
    LOG_HCL_DEBUG(HCL_OFI,
                  "Missed in buffer mapping (mr handle), addr: [0x{:x}] size: [0x{:x}] ({:g}MB).",
                  addr,
                  size,
                  B2MB(size));
    return NULL;
}

int MRMapping::mapDevMem(uint64_t addr, uint64_t size, uint64_t offset, uint32_t flags, ofi_component_t* ofiComponent)
{
    // If s_hmemMR is set to false, skip registering HBM buffers
    if (!ofi_t::isHmemMR())
    {
        LOG_HCL_DEBUG(HCL_OFI, "Provider does not require registration of HBM buffers");
        return 0;
    }

    int retval = lookup_dma_buf_fd(addr + offset, size);
    if (retval == 0)
    {
        if (offset == 0)
        {
            // Exported device memory addr and size should be aligned to PAGE_SIZE
            uint64_t aligned_base_addr, aligned_end_addr, aligned_size;

            aligned_base_addr = _ALIGN_DOWN(addr, ALIGN_SIZE);
            aligned_end_addr  = _ALIGN_UP(addr + size, ALIGN_SIZE);

            if (aligned_end_addr > getMaxDramAddr())
            {
                LOG_HCL_DEBUG(
                    HCL,
                    "End of aligned addr: 0x{:x} exceeded HBM max addr. End of aligned addr will be decreased to max "
                    "HBM addr: 0x{:x}",
                    aligned_end_addr,
                    getMaxDramAddr());
                aligned_end_addr = getMaxDramAddr();

                // Make sure user did not exceed HBM size.
                VERIFY(aligned_end_addr >= (addr + size),
                       "Requested addr: 0x{:x} and size {:g}MB exceed HBM size.",
                       addr,
                       B2MB(size));
            }

            if (aligned_base_addr < getDramBaseAddr())
            {
                LOG_HCL_DEBUG(
                    HCL_OFI,
                    "Start of aligned addr: 0x{:x} is smaller than HBM base addr. Start of aligned addr will be "
                    "increased to HBM base addr: 0x{:x}",
                    aligned_base_addr,
                    getDramBaseAddr());
                aligned_base_addr = getDramBaseAddr();
            }

            aligned_size = aligned_end_addr - aligned_base_addr;

            LOG_HCL_DEBUG(
                HCL,
                "GaudiDirectCache align: original addr [0x{:x}] original size [0x{:x}] ({:g}MB), new addr [0x{:x}] "
                "new size [0x{:x}].",
                addr,
                size,
                B2MB(size),
                aligned_base_addr,
                aligned_size);

            addr = aligned_base_addr;
            size = aligned_size;
        }

        curr_entry.size = size;
        curr_entry.addr = addr + offset;
        int device_fd   = hccl_device()->getFd();
        if (device_fd != 0)
        {
            LOG_HCL_DEBUG(HCL_OFI, "HCL_GetDeviceFD returned the following device FD: [{}].", device_fd);
        }
        else
        {
            LOG_HCL_DEBUG(HCL_OFI, "HCL_GetDeviceFD returned 0 for device FD");
        }

        int dmabuf_fd = hlthunk_device_mapped_memory_export_dmabuf_fd(device_fd, addr, size, offset, flags);

        if (dmabuf_fd < 0)
        {
            LOG_HCL_ERR(HCL_OFI,
                        "HCL_BufferMap returned invalid FD: [{}] for size [0x{:x}] ({:g}MB), address [0x{:x}], offset "
                        "[0x{:x}], hlthunk_device_mapped_memory_export_dmabuf_fd failed. {}",
                        dmabuf_fd,
                        size,
                        B2MB(size),
                        addr,
                        offset,
                        std::strerror(dmabuf_fd * (-1)));

            curr_entry = {0, 0, 0, NULL};
            return hcclLibfabricError;
        }
        else
        {
            LOG_HCL_DEBUG(
                HCL,
                "HCL_BufferMap returned valid FD: [{}] for size [0x{:x}] ({:g}MB), address [0x{:x}], offset [0x{:x}]"
                "hlthunk_device_mapped_memory_export_dmabuf_fd succeeded.",
                dmabuf_fd,
                size,
                B2MB(size),
                addr,
                offset);
        }

        curr_entry.fd        = dmabuf_fd;
        curr_entry.mr_handle = NULL;

        update_buffer_mapping(curr_entry);

        struct fid_mr* mr_handle = NULL;
        LOG_HCL_DEBUG(HCL_OFI,
                      "calling register_mr with address [0x{:x}], size [0x{:x}], dmabuf_fd={}",
                      curr_entry.addr,
                      size,
                      dmabuf_fd);
        int ret = ofiComponent->register_mr((void*)curr_entry.addr, size, FI_HMEM_SYNAPSEAI, curr_entry.fd, &mr_handle);

        if (OFI_UNLIKELY(ret != 0))
        {
            curr_entry = {0, 0, 0, NULL};
            return hcclLibfabricError;
        }
        VERIFY(mr_handle != NULL, "w_fi_mr_regattr returned mr_handle with value NULL.");
        curr_entry.mr_handle = mr_handle;
        update_mr_handle(curr_entry);
        LOG_HCL_DEBUG(HCL_OFI,
                      "Updated MRMapping entry: size [0x{:x}] ({:g}MB) and address [0x{:x}], mr_handle=0x{:x}, fd={}",
                      curr_entry.size,
                      B2MB(curr_entry.size),
                      curr_entry.addr,
                      (uint64_t)curr_entry.mr_handle,
                      curr_entry.fd);
        curr_entry = {0, 0, 0, NULL};
    }
    else if (retval < 0)
    {
        LOG_HCL_DEBUG(HCL_OFI, "The provided address [{}] and size [{}] cannot be registered.", addr, size);
        return retval;
    }
    return 0;
}

hcclResult_t
MRMapping::mapHostMem(uint64_t addr, uint64_t size, ofi_component_t* ofiComponent, struct fid_mr*& mr_handle)
{
    int res = ofiComponent->register_mr((void*)addr, size, FI_HMEM_SYSTEM, 0, &mr_handle);
    if (res)
    {
        LOG_HCL_ERR(HCL_OFI, "Host MR registration failed");
        return hcclLibfabricError;
    }
    buffer_mapping_entry entry = {addr, size, 0, mr_handle};
    update_buffer_mapping(entry);
    return hcclSuccess;
}

hcclResult_t MRMapping::mapFlushBufMem(ofi_component_t* ofiComponent)
{
    int ret = hcclUninitialized;

    ret = ofiComponent->register_mr((void*)&m_flushBuf, sizeof(int), FI_HMEM_SYSTEM, 0, &m_flushMRLocalHandle, true);
    if (ret)
    {
        LOG_HCL_ERR(HCL_OFI, "Flush local buffer MR registration failed");
        ret = hcclLibfabricError;
        goto error;
    }

    ret = ofiComponent->register_mr((void*)getDramBaseAddr(),
                                    sizeof(int),
                                    FI_HMEM_SYNAPSEAI,
                                    lookup_dma_buf_fd(getDramBaseAddr(), sizeof(int)),
                                    &m_flushMRRemoteHandle,
                                    true);
    if (ret)
    {
        LOG_HCL_ERR(HCL_OFI, "Flush remote buffer MR registration failed");
        ret = hcclLibfabricError;
        goto error;
    }

    return hcclSuccess;

error:
    if (m_flushMRLocalHandle)
    {
        ofi_component_t::deregister_mr(m_flushMRLocalHandle);
        m_flushMRLocalHandle = nullptr;
    }
    if (m_flushMRRemoteHandle)
    {
        ofi_component_t::deregister_mr(m_flushMRRemoteHandle);
        m_flushMRRemoteHandle = nullptr;
    }
    return static_cast<hcclResult_t>(ret);
}

int MRMapping::closeFD()
{
    int status;

    for (auto& mapping_entry : buffer_mapping_vec)
    {
        if (mapping_entry.fd > 0)
        {
            status = close(mapping_entry.fd);
            if (status == 0)
            {
                LOG_HCL_DEBUG(HCL_OFI, "MRMapping: FD [{}] was closed successfully.", mapping_entry.fd);
            }
            else
            {
                LOG_HCL_ERR(HCL_OFI, "MRMapping: FD [{}] could not be closed.", mapping_entry.fd);
                return -1;
            }
        }
    }

    return 0;
}

int MRMapping::deregisterMR()
{
    int status;

    if (m_flushMRLocalHandle)
    {
        VERIFY(0 == ofi_component_t::deregister_mr(m_flushMRLocalHandle));
        m_flushMRLocalHandle = nullptr;
    }
    if (m_flushMRRemoteHandle)
    {
        VERIFY(0 == ofi_component_t::deregister_mr(m_flushMRRemoteHandle));
        m_flushMRRemoteHandle = nullptr;
    }

    for (auto& mapping_entry : buffer_mapping_vec)
    {
        if (mapping_entry.mr_handle)
        {
            status = ofi_component_t::deregister_mr(mapping_entry.mr_handle);
            if (status == 0)
            {
                LOG_HCL_DEBUG(HCL_OFI,
                              "MRMapping: deregistration of mr_handle [{}] went successfully.",
                              (uint64_t)mapping_entry.mr_handle);
            }
            else
            {
                LOG_HCL_ERR(HCL_OFI,
                            "MRMapping: deregistration of mr_handle [{}] failed.",
                            (uint64_t)mapping_entry.mr_handle);
                return -1;
            }
        }
        // avoid double deregistration
        mapping_entry.mr_handle = NULL;
        update_mr_handle(mapping_entry);
    }
    return status;
}

uint64_t MRMapping::getDramSize()
{
    if (m_dram_size == 0)
    {
        m_dram_size = hccl_device()->getDRAMSize();
    }
    return m_dram_size;
}

uint64_t MRMapping::getDramBaseAddr()
{
    if (m_dram_base == 0)
    {
        m_dram_base = hccl_device()->getDRAMBaseAddr();
    }
    return m_dram_base;
}

uint64_t MRMapping::getMaxDramAddr()
{
    return (getDramSize() + getDramBaseAddr());
}

void* MRMapping::getFlushBuf()
{
    return (void*)&m_flushBuf;
}

struct fid_mr* MRMapping::getFlushMRLocalHandle()
{
    return m_flushMRLocalHandle;
}

struct fid_mr* MRMapping::getFlushMRRemoteHandle()
{
    return m_flushMRRemoteHandle;
}

MRMapping::MRMapping() {}

MRMapping::~MRMapping() {}