#include "memory_region.h"

#include <unistd.h>  // for close
#include <cstring>   // for strerror

#include "platform/gen2_arch_common/hccl_device.h"

#include "hcl_log_manager.h"             // for LOG*
#include "hcl_utils.h"                   // for LOG_HCL_DEBUG, LOG_HCL_ERR
#include "hlthunk.h"                     // for hlthunk_device_mapped_memory_export_dmabuf_fd
#include "interfaces/hcl_idevice.h"      // for IHclDevice
#include "libfabric/hl_ofi.h"            // for OFI_UNLIKELY
#include "libfabric/hl_ofi_component.h"  // for ofi_component_ts
#include "rdma/fi_domain.h"              // for FI_HMEM_SYNAPSEAI

MemoryRegion::MemoryRegion(const MRParams& params, ofi_component_t* ofiComponent)
{
    // Can't create MR handle without verbs.
    VERIFY(ofi_t::isVerbs());

    if (ofi_t::isGaudiDirect())
    {
        VERIFY(registerDevMR(params, ofiComponent) == hcclSuccess,
               "device memory mapping for OFI gaudi-direct failed.");
        if (ofi_t::isFabricFlush())
        {
            VERIFY(registerFlushBufMR(params, ofiComponent) == hcclSuccess,
                   "Flush buffer mapping for gaudi-direct failed");
        }
    }
    else
    {
        VERIFY(registerHostMR(params, ofiComponent) == hcclSuccess,
               "host memory mapping for non gaudi-direct hnic use failed.");
    }
}

MemoryRegion::~MemoryRegion()
{
    /* Close dmabuf FD. */
    if (m_dmabufFD)
    {
        int status = close(m_dmabufFD.value());
        if (status == 0)
        {
            LOG_DEBUG(HCL_OFI, "MemoryRegion: FD {} was closed successfully.", m_dmabufFD.value());
        }
        else
        {
            LOG_ERR(HCL_OFI, "MemoryRegion: FD {} could not be closed.", m_dmabufFD.value());
        }
    }

    /* Deregister MR. */
    ofi_component_t::deregister_mr(m_handle);
    if (m_handle_single)
    {
        ofi_component_t::deregister_mr(m_handle_single.value());
    }
    if (m_flushLocalHandle)
    {
        ofi_component_t::deregister_mr(m_flushLocalHandle.value());
    }
    if (m_flushRemoteHandle)
    {
        ofi_component_t::deregister_mr(m_flushRemoteHandle.value());
    }
}

hcclResult_t MemoryRegion::registerDevMR(const MRParams& params, ofi_component_t* ofiComponent)
{
    // If s_hmemMR is set to false, skip registering HBM buffers
    VERIFY(ofi_t::isHmemMR(), "Provider does not require registration of HBM buffers.");

    // Verify parameters
    VERIFY(params.m_fd && params.m_addr && params.m_size && params.m_offset, "registerDevMR Uninitialized MRParams");
    int      fd     = params.m_fd.value();
    uint64_t addr   = params.m_addr.value();
    uint64_t size   = params.m_size.value();
    uint64_t offset = params.m_offset.value();

    int dmabuf_fd = hlthunk_device_mapped_memory_export_dmabuf_fd(fd, addr, size, offset, (O_RDWR | O_CLOEXEC));
    if (dmabuf_fd < 0)
    {
        LOG_HCL_ERR(HCL_OFI,
                    "hlthunk_device_mapped_memory_export_dmabuf_fd returned invalid FD: [{}] for size [0x{:x}] "
                    "({:g}MB), address [0x{:x}], offset [0x{:x}]. {}",
                    dmabuf_fd,
                    size,
                    B2MB(size),
                    addr,
                    offset,
                    std::strerror(dmabuf_fd * (-1)));
        return hcclLibfabricError;
    }
    else
    {
        LOG_HCL_DEBUG(HCL,
                      "hlthunk_device_mapped_memory_export_dmabuf_fd returned valid FD: [{}] for size [0x{:x}] "
                      "({:g}MB), address [0x{:x}], offset [0x{:x}]",
                      dmabuf_fd,
                      size,
                      B2MB(size),
                      addr,
                      offset);
    }

    LOG_HCL_DEBUG(HCL_OFI,
                  "calling register_mr with address [0x{:x}], size [0x{:x}] ({:g}MB), dmabuf_fd={}",
                  (addr + offset),
                  size,
                  B2MB(size),
                  dmabuf_fd);
    int ret = ofiComponent->register_mr((void*)(addr + offset),
                                        size,
                                        FI_HMEM_SYNAPSEAI,
                                        dmabuf_fd,
                                        &m_handle,
                                        DomainType::DATA);

    if (OFI_UNLIKELY(ret != 0))
    {
        return hcclLibfabricError;
    }
    VERIFY(m_handle != NULL, "MR handle not available for addr 0x{:x} size: 0x{:x}", addr, size);

    if (!GCFG_HCL_SINGLE_QP_PER_SET.value())
    {
        struct fid_mr* single_qp_mr = nullptr;
        ret                         = ofiComponent->register_mr((void*)(addr + offset),
                                        size,
                                        FI_HMEM_SYNAPSEAI,
                                        dmabuf_fd,
                                        &single_qp_mr,
                                        DomainType::SINGLE);
        if (OFI_UNLIKELY(ret != 0))
        {
            return hcclLibfabricError;
        }
        VERIFY(single_qp_mr != NULL, "MR handle not available for addr 0x{:x} size: 0x{:x}", addr, size);
        m_handle_single = single_qp_mr;
    }

    m_dmabufFD = dmabuf_fd;

    return hcclSuccess;
}

hcclResult_t MemoryRegion::registerHostMR(const MRParams& params, ofi_component_t* ofiComponent)
{
    // Verify parameters
    VERIFY(params.m_addr && params.m_size, "Uninitialized MRParams");
    uint64_t addr = params.m_addr.value();
    uint64_t size = params.m_size.value();

    int res = ofiComponent->register_mr((void*)addr, size, FI_HMEM_SYSTEM, 0, &m_handle, DomainType::DATA);
    if (res)
    {
        LOG_HCL_ERR(HCL_OFI, "Host MR registration failed");
        return hcclLibfabricError;
    }
    VERIFY(m_handle != NULL, "MR handle not available for addr 0x{:x} size: 0x{:x}", addr, size);

    if (!GCFG_HCL_SINGLE_QP_PER_SET.value())
    {
        struct fid_mr* single_qp_mr = nullptr;
        res = ofiComponent->register_mr((void*)addr, size, FI_HMEM_SYSTEM, 0, &single_qp_mr, DomainType::SINGLE);
        if (res)
        {
            LOG_HCL_ERR(HCL_OFI, "Host MR registration failed");
            return hcclLibfabricError;
        }
        VERIFY(single_qp_mr != NULL, "MR handle not available for addr 0x{:x} size: 0x{:x}", addr, size);
        m_handle_single = single_qp_mr;
    }

    return hcclSuccess;
}

hcclResult_t MemoryRegion::registerFlushBufMR(const MRParams& params, ofi_component_t* ofiComponent)
{
    VERIFY(params.m_addr, "Uninitialized MRParams");

    int            ret              = hcclUninitialized;
    struct fid_mr* local_mr_handle  = NULL;
    struct fid_mr* remote_mr_handle = NULL;

    /* Create buffers */
    m_flushLocalBuffer.emplace();
    m_flushRemoteBuffer = params.m_addr.value();

    /* Register local (host) MR */
    ret = ofiComponent->register_mr((void*)&m_flushLocalBuffer.value(),
                                    sizeof(int),
                                    FI_HMEM_SYSTEM,
                                    0,
                                    &local_mr_handle,
                                    DomainType::FLUSH);
    if (ret)
    {
        LOG_HCL_ERR(HCL_OFI, "Flush local buffer MR registration failed");
        return hcclLibfabricError;
    }

    /* Register remote (device) MR */
    ret = ofiComponent->register_mr((void*)m_flushRemoteBuffer.value(),
                                    sizeof(int),
                                    FI_HMEM_SYNAPSEAI,
                                    m_dmabufFD.value(),
                                    &remote_mr_handle,
                                    DomainType::FLUSH);
    if (ret)
    {
        LOG_HCL_ERR(HCL_OFI, "Flush remote buffer MR registration failed");
        return hcclLibfabricError;
    }

    m_flushLocalHandle  = local_mr_handle;
    m_flushRemoteHandle = remote_mr_handle;

    return hcclSuccess;
}

struct fid_mr* MemoryRegion::getMRHandle() const
{
    return m_handle;
}

struct fid_mr* MemoryRegion::getSingleQpMrHandle() const
{
    VERIFY(m_handle_single, "Memory region is not registered for single QP set.");
    return m_handle_single.value();
}

int MemoryRegion::getDmabufFd() const
{
    VERIFY(m_dmabufFD, "Missing dmabuf FD.");
    return m_dmabufFD.value();
}

uint64_t MemoryRegion::getLocalFlushBuf() const
{
    VERIFY(m_flushLocalBuffer, "Local flush buffer is not initialized.");
    return reinterpret_cast<uint64_t>(&m_flushLocalBuffer.value());
}

uint64_t MemoryRegion::getRemoteFlushBuf() const
{
    VERIFY(m_flushRemoteBuffer, "Remote flush buffer is not initialized.");
    return m_flushRemoteBuffer.value();
}

struct fid_mr* MemoryRegion::getFlushMRLocalHandle() const
{
    VERIFY(m_flushLocalHandle, "Local flush handle is not initialized.");
    return m_flushLocalHandle.value();
}

struct fid_mr* MemoryRegion::getFlushMRRemoteHandle() const
{
    VERIFY(m_flushRemoteHandle, "Remote flush handle is not initialized.");
    return m_flushRemoteHandle.value();
}
