#include "hl_ofi_component.h"
#include <sys/types.h>                   // for ssize_t
#include <sys/uio.h>                     // for iovec
#include <cassert>                       // for assert
#include <cstdlib>                       // for free, calloc
#include <memory>                        // for unique_ptr
#include "hlthunk.h"                     // for hlthunk_device_mapped_memory_export_dmabuf_fd
#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface
#include "hccl_types.h"                  // for hcclLibfabricError, hcclSuccess
#include "hcl_utils.h"                   // for LOG_HCL_ERR, LOG_HCL_DEBUG
#include "libfabric/hl_ofi.h"            // for OFI_UNLIKELY, MAX_EP_ADDR
#include "libfabric/libfabric_common.h"  // for container_of
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG
#include "ofi_plugin.h"                  // for ofi_plugin
#include "rdma/fi_domain.h"              // for fi_mr_attr, fid_mr, fi_av_attr
#include "rdma/fi_endpoint.h"            // for fid_ep
#include "rdma/fi_eq.h"                  // for fi_cq_tagged_entry, fi_cq_er...
#include "rdma/fi_errno.h"               // for FI_EAGAIN, FI_EAVAIL

int ofi_fi_close(fid_t domain)
{
    return ofi_plugin->w_fi_close(domain);
}

#define IF_GDR(value) ((ofi_t::isGaudiDirect()) ? std::optional(value) : (std::nullopt))

ofi_component_t::ofi_component_t(const int                                ofiDeviceID,
                                 [[maybe_unused]] const int               hw_module_id,
                                 struct fi_info* const                    prov,
                                 const int                                cpuid,
                                 [[maybe_unused]] const enum fi_cq_format cq_format)
: m_ofiDeviceID(ofiDeviceID),
  m_cpuid(cpuid),
  m_refcnt(1),
  m_cqe_burst(GCFG_OFI_CQ_BURST_PROC.value()),
  m_eagainMaxRetryDuration(GCFG_HCL_OFI_MAX_RETRY_DURATION.value()),
  m_prov(prov),
  m_fabric(create_fabric(m_prov)),
  m_domain(create_domain(m_prov, m_fabric.get())),
  m_dmabufFD(),
  m_mr(std::nullopt),
  m_fabric_single(create_fabric(prov)),
  m_domain_single(create_domain(prov, m_fabric_single.get())),
  m_mrSingle(std::nullopt),
  m_flush_provider(IF_GDR(m_prov)),
  m_flush_fabric(IF_GDR(create_fabric(*m_flush_provider))),
  m_flush_domain(IF_GDR(create_domain(*m_flush_provider, m_flush_fabric.value().get()))),
  m_flush_cq(IF_GDR(create_cq(m_flush_domain.value().get(), m_cpuid, FI_CQ_FORMAT_TAGGED))),
  m_flush_av(IF_GDR(create_av(m_flush_domain.value().get()))),
  m_flush_ep(IF_GDR(
      create_ep(*m_flush_provider, m_flush_domain.value().get(), m_flush_cq.value().get(), m_flush_av.value().get()))),
  m_flush_addr(IF_GDR(create_address(m_flush_ep.value().get(), m_flush_av.value().get()))),
  m_mrFlushLocal(std::nullopt),
  m_mrFlushRemote(std::nullopt),
  m_flushLocalBuffer(0),
  m_flushRemoteBuffer(0)
{
    LOG_DEBUG(HCL_OFI, "Using provider to create a component: {}", ofi_plugin->w_fi_tostr(m_prov, FI_TYPE_INFO));
}

FiObject<struct fid_fabric*> ofi_component_t::create_fabric(const struct fi_info* const provider)
{
    struct fid_fabric* fabric = nullptr;
    VERIFY(0 == ofi_plugin->w_fi_fabric(provider->fabric_attr, &fabric, nullptr));
    return fabric;
}

FiObject<struct fid_domain*> ofi_component_t::create_domain(struct fi_info* const    provider,
                                                            struct fid_fabric* const fabric)
{
    struct fid_domain* domain = nullptr;
    VERIFY(0 == ofi_plugin->w_fi_domain(fabric, provider, &domain, nullptr));
    return domain;
}

FiObject<struct fid_mr*> ofi_component_t::create_mr(struct fid_domain* const domain,
                                                    void* const              data,
                                                    const size_t             size,
                                                    const fi_hmem_iface      fi_hmem_iface,
                                                    const int                dmabuf_fd)
{
    struct fid_mr*      mr      = nullptr;
    uint64_t            flags   = 0;
    struct fi_mr_attr   mr_attr = {{0}, 0};
    struct fi_mr_dmabuf dmabuf  = {0};
    struct iovec        iov     = {0};

    /* for device MR registration. */
    if (dmabuf_fd > 0)
    {
        dmabuf.fd        = dmabuf_fd;
        dmabuf.base_addr = data;
        dmabuf.len       = size;

        mr_attr.dmabuf = &dmabuf;
        flags |= FI_MR_DMABUF;
    }
    /* for host MR registration. */
    else
    {
        iov.iov_base   = data;
        iov.iov_len    = size;
        mr_attr.mr_iov = &iov;
    }

    mr_attr.iov_count = 1;
    mr_attr.access    = FI_SEND | FI_RECV;
    mr_attr.iface     = fi_hmem_iface;

    LOG_DEBUG(HCL_OFI, "MR registration attempt for {}, size {}", data, size);

    VERIFY(0 == ofi_plugin->w_fi_mr_regattr(domain, &mr_attr, flags, &mr), "Failed MR registration");
    LOG_INFO(HCL_OFI,
             "MR registration complete. mHandle={}, key={} address={} size={}MB",
             mr,
             mr->key,
             data,
             B2MB(size));
    return mr;
}

FiObject<struct fid_cq*>
ofi_component_t::create_cq(struct fid_domain* const domain, int cpuid, const enum fi_cq_format format)
{
    struct fi_cq_attr cq_attr = {0};
    cq_attr.format            = format;
    if (cpuid >= 0)
    {
        cq_attr.flags            = FI_AFFINITY;
        cq_attr.signaling_vector = cpuid;
    }
    struct fid_cq* cq = nullptr;
    VERIFY(0 == ofi_plugin->w_fi_cq_open(domain, &cq_attr, &cq, nullptr));
    return cq;
}

FiObject<struct fid_av*> ofi_component_t::create_av(struct fid_domain* const domain)
{
    struct fid_av*    av      = nullptr;
    struct fi_av_attr av_attr = {FI_AV_UNSPEC};
    VERIFY(0 == ofi_plugin->w_fi_av_open(domain, &av_attr, &av, nullptr));
    return av;
}

FiObject<struct fid_ep*> ofi_component_t::create_ep(struct fi_info* const    provider,
                                                    struct fid_domain* const domain,
                                                    struct fid_cq* const     cq,
                                                    struct fid_av* const     av)
{
    struct fid_ep* ep = nullptr;
    VERIFY(0 == ofi_plugin->w_fi_endpoint(domain, provider, &ep, nullptr));
    VERIFY(0 == ofi_plugin->w_fi_ep_bind(ep, &cq->fid, FI_SEND | FI_RECV));
    VERIFY(0 == ofi_plugin->w_fi_ep_bind(ep, &av->fid, 0));
    VERIFY(0 == ofi_plugin->w_fi_enable(ep));
    LOG_DEBUG(HCL_OFI,
              "Created endpoint {} for domain {} bound to cq {} and av {}",
              fmt::ptr(ep),
              fmt::ptr(domain),
              fmt::ptr(cq),
              fmt::ptr(av));
    return ep;
}

fi_addr_t ofi_component_t::create_address(struct fid_ep* const ep, struct fid_av* const av)
{
    fi_addr_t addr                 = 0;
    char      ep_name[MAX_EP_ADDR] = {0};
    size_t    namelen              = sizeof(ep_name);
    VERIFY(0 == ofi_plugin->w_fi_getname(&(ep->fid), (void*)&ep_name, &namelen));
    VERIFY(1 == ofi_plugin->w_fi_av_insert(av, (void*)ep_name, 1, &addr, 0, nullptr));
    return addr;
}

int ofi_component_t::ofi_progress(struct fid_cq* const cq)
{
    ssize_t                rc         = 0;
    int                    ret        = hcclUninitialized;
    struct fi_cq_err_entry err_buffer = {0};

    while (true)
    {
        // Receive completions for the given endpoint
        void* cq_buf = get_cq_buf();
        rc           = ofi_plugin->w_fi_cq_read(cq, cq_buf, m_cqe_burst);
        if (rc > 0)
        {
            OFI_EXIT_ON_ERROR(process_completions(cq_buf, rc));
            break;
        }
        else if (OFI_UNLIKELY(rc == -FI_EAVAIL))
        {
            const ssize_t prev_rc = rc;
            rc                    = ofi_plugin->w_fi_cq_readerr(cq, &err_buffer, 0);
            if (OFI_UNLIKELY(rc < 0))
            {
                LOG_HCL_ERR(HCL,
                            "Unable to read from fi_cq_readerr; RC: {}, ERROR: {}",
                            rc,
                            ofi_plugin->w_fi_cq_strerror(cq, err_buffer.prov_errno, err_buffer.err_data, nullptr, 0));
                return hcclLibfabricError;
            }

            ofi_req_t* req = container_of(err_buffer.op_context, ofi_req_t, ctx);
            req->state     = OFI_REQ_ERROR;
            req->size      = err_buffer.len;
            LOG_HCL_ERR(HCL_OFI,
                        "Error state, w_fi_cq_read RC: {}, ofiDevice: {}, tag: {} ERROR: {}",
                        prev_rc,
                        req->ofiDevice,
                        req->ofiComm->tag,
                        ofi_plugin->w_fi_cq_strerror(cq, err_buffer.prov_errno, err_buffer.err_data, nullptr, 0));
            return hcclLibfabricError;
        }
        else if (rc == -FI_EAGAIN)
        {
            // No completions to process
            break;
        }
        else
        {
            LOG_HCL_ERR(HCL_OFI,
                        "Unable to retrieve completion queue entries; RC: {}, ERROR: {}",
                        rc,
                        ofi_plugin->w_fi_strerror(-rc));
            return hcclLibfabricError;
        }
    }
    ret = hcclSuccess;
error:
    return ret;
}

int ofi_component_t::ofi_flush_progress()
{
    ssize_t                rc         = 0;
    struct fi_cq_err_entry err_buffer = {0};
    ofi_req_t*             req        = nullptr;
    fi_cq_tagged_entry     cqe;

    // Receive flush completion for the given endpoint
    rc = ofi_plugin->w_fi_cq_read(m_flush_cq.value().get(), &cqe, 1);
    if (rc == 1)
    {
        req = container_of(cqe.op_context, ofi_req_t, ctx);
        if (OFI_UNLIKELY(req == nullptr))
        {
            LOG_HCL_ERR(HCL_OFI, "Invalid request context provided");
            return hcclLibfabricError;
        }
        req->state = OFI_REQ_COMPLETED;
        req->size  = cqe.len;
    }
    else if (OFI_UNLIKELY(rc == -FI_EAVAIL))
    {
        const ssize_t prev_rc = rc;
        rc                    = ofi_plugin->w_fi_cq_readerr(m_flush_cq.value().get(), &err_buffer, 0);
        if (OFI_UNLIKELY(rc < 0))
        {
            LOG_HCL_ERR(HCL,
                        "Unable to read from fi_cq_readerr; RC: {}, ERROR: {}",
                        rc,
                        ofi_plugin->w_fi_cq_strerror(m_flush_cq.value().get(),
                                                     err_buffer.prov_errno,
                                                     err_buffer.err_data,
                                                     NULL,
                                                     0));
            return hcclLibfabricError;
        }

        req        = container_of(err_buffer.op_context, ofi_req_t, ctx);
        req->state = OFI_REQ_ERROR;
        req->size  = err_buffer.len;
        LOG_HCL_ERR(HCL_OFI,
                    "Error state, w_fi_cq_read RC: {}, ERROR: {}",
                    prev_rc,
                    ofi_plugin->w_fi_cq_strerror(m_flush_cq.value().get(),
                                                 err_buffer.prov_errno,
                                                 err_buffer.err_data,
                                                 NULL,
                                                 0));
        return hcclLibfabricError;
    }
    else if (rc == -FI_EAGAIN)
    {
        // Do nothing, no completions to process
    }
    else if (rc > 1)
    {
        LOG_HCL_ERR(HCL_OFI, "Retrieved unexpected number of flush completion queue entries; got {}, expected 1", rc);
        return hcclLibfabricError;
    }
    else
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Unable to retrieve completion queue entries; RC: {}, ERROR: {}",
                    rc,
                    ofi_plugin->w_fi_strerror(-rc));
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

int ofi_component_t::test(ofi_req_t* req, int* done, size_t* size)
{
    int        ret;
    ofiComm_t* ofiComm = nullptr;

    // Try to complete requests only if the given request wasn't completed
    if ((req->state != OFI_REQ_COMPLETED && req->state != OFI_REQ_ERROR))
    {
        ret = ofi_progress(req->ofiComm->cq);
        if (OFI_UNLIKELY(ret != 0))
        {
            return hcclLibfabricError;
        }
    }

    // Determine whether request has finished and free if done
    if (OFI_LIKELY(req->state == OFI_REQ_COMPLETED || req->state == OFI_REQ_ERROR))
    {
        if (size) *size = req->size;
        *done   = 1;
        ofiComm = req->ofiComm;
        if (req->direction == OFI_SEND)
        {
            if (OFI_UNLIKELY(ofiComm == nullptr))
            {
                LOG_HCL_ERR(HCL_OFI, "Invalid ofiComm provided for request on OFI device ID {}", req->ofiDevice);
                return hcclLibfabricError;
            }
            if (OFI_UNLIKELY(ofiComm->num_inflight_sends == 0))
            {
                LOG_HCL_ERR(HCL_OFI, "Failed to process OFI send due to 0 inflight requests");
                return hcclLibfabricError;
            }
            ofiComm->num_inflight_sends--;
        }
        else if (req->direction == OFI_RECV)
        {
            if (OFI_UNLIKELY(ofiComm == nullptr))
            {
                LOG_HCL_ERR(HCL_OFI, "Invalid ofiComm provided for request on OFI device ID {}", req->ofiDevice);
                return hcclLibfabricError;
            }
            if (OFI_UNLIKELY(ofiComm->num_inflight_recvs == 0))
            {
                LOG_HCL_ERR(HCL_OFI, "Failed to process OFI recv due to 0 inflight requests");
                return hcclLibfabricError;
            }
            ofiComm->num_inflight_recvs--;
        }
        if (OFI_UNLIKELY(req->state == OFI_REQ_ERROR))
        {
            LOG_HCL_ERR(HCL_OFI, "Request failed with an error");
            delete req;
            return hcclLibfabricError;
        }
        delete req;
    }
    else
        *done = 0;

    return hcclSuccess;
}

int ofi_component_t::_flush(ofiComm_t* ofiComm, ofi_req_t& request)
{
    ssize_t rc = 0;

    // Validate rComm
    if (OFI_UNLIKELY(nullptr == ofiComm))
    {
        LOG_HCL_ERR(HCL_OFI, "Invalid recvComm was provided for PCIe flush");
        return hcclLibfabricError;
    }

    if (OFI_UNLIKELY(!m_mr))
    {
        LOG_HCL_ERR(HCL_OFI, "Missing MR handle");
        return hcclLibfabricError;
    }

    // Extract remote key
    const uint64_t key = ofi_plugin->w_fi_mr_key(m_mrFlushRemote.value());
    if (OFI_UNLIKELY(key == FI_KEY_NOTAVAIL))
    {
        LOG_HCL_ERR(HCL_OFI, "Extraction of remote key for PCIe flush operation failed.");
        return hcclLibfabricError;
    }

    request.state     = OFI_REQ_CREATED;
    request.ofiComm   = ofiComm;
    request.ofiDevice = m_ofiDeviceID;
    request.direction = OFI_FLUSH;
    LOG_HCL_DEBUG(HCL_OFI,
                  "Performing flushing read of {}B from 0x{:x} to 0x{:x}",
                  sizeof(int),
                  m_flushRemoteBuffer,
                  reinterpret_cast<uint64_t>(&m_flushLocalBuffer));
    // RDMA read
    rc = RETRY_ON_EAGAIN(
        ofi_plugin->w_fi_read(
            m_flush_ep.value().get(),                          // Fabric endpoint on which to initiate read operation
            &m_flushLocalBuffer,                               // Local data buffer to read into
            sizeof(int),                                       // Length of data to read
            ofi_plugin->w_fi_mr_desc(m_mrFlushLocal.value()),  // Descriptor associated with the local data buffer
            *m_flush_addr,        // Source address to read from for connectionless transfers
            m_flushRemoteBuffer,  // Remote CQ data to transfer with the operation
            key,                  // Protection key associated with the remote memory
            &request.ctx),        // User specified pointer to associate with the operation
        m_eagainMaxRetryDuration,
        {
            LOG_HCL_WARN(HCL_OFI, "Insufficient resources for fi_read operation, try again. ofiDev: {}.", ofiComm->dev);
            if (OFI_UNLIKELY(ofi_flush_progress() != 0))
            {
                return hcclLibfabricError;
            }
        });

    if (0 != rc)
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Unable to issue read operation for dev {}. RC: {}, ERROR: {}",
                    ofiComm->dev,
                    rc,
                    ofi_plugin->w_fi_strerror(-rc));
        return hcclLibfabricError;
    }

    return hcclSuccess;
}

struct fid_domain* ofi_component_t::getDomainByType(DomainType domainType) const
{
    switch (domainType)
    {
        case DomainType::DATA:
            return m_domain.get();
        case DomainType::SINGLE:
            return m_domain_single.get();
        case DomainType::FLUSH:
            return m_flush_domain->get();
    }
    VERIFY(false, "Invalid domain type");
}

std::string ofi_component_t::getDomainNameByType(DomainType domainType)
{
    switch (domainType)
    {
        case DomainType::DATA:
            return "Data";
        case DomainType::SINGLE:
            return "Single";
        case DomainType::FLUSH:
            return "Flush";
    }
    VERIFY(false, "Invalid domain type");
}

int ofi_component_t::process_first_recv_completion(ofi_req_t* req)
{
    if (!ofi_t::isFabricFlush())
    {
        return hcclSuccess;
    }

    int       ret = hcclUninitialized;
    ofi_req_t flushReq;
    int       done = 0;

    OFI_EXIT_ON_ERROR(_flush(req->ofiComm, flushReq));

    // Ensure that the request completes
    while (!done)
    {
        if ((flushReq.state != OFI_REQ_COMPLETED && flushReq.state != OFI_REQ_ERROR))
        {
            OFI_EXIT_ON_ERROR(ofi_flush_progress());
        }
        if (OFI_LIKELY(flushReq.state == OFI_REQ_COMPLETED || flushReq.state == OFI_REQ_ERROR))
        {
            done = 1;
            if (OFI_UNLIKELY(flushReq.state == OFI_REQ_ERROR))
            {
                return hcclLibfabricError;
            }
        }
        else
        {
            done = 0;
        }
    }

    ret = hcclSuccess;
error:
    return ret;
}

void ofi_component_t::initializeMemoryRegion(MRParams& params)
{
    VERIFY(ofi_t::isVerbs());

    VERIFY(params.m_addr && params.m_size, "Uninitialized MRParams");
    const uint64_t addr   = params.m_addr.value();
    m_flushRemoteBuffer   = addr;
    const uint64_t size   = params.m_size.value();
    const uint64_t offset = params.m_offset.value_or(0);

    if (ofi_t::isGaudiDirect())
    {
        const auto fd = params.m_fd.value();
        const int  dmabuf_fd =
            hlthunk_device_mapped_memory_export_dmabuf_fd(fd, addr, size, offset, (O_RDWR | O_CLOEXEC));
        VERIFY(dmabuf_fd >= 0,
               "hlthunk_device_mapped_memory_export_dmabuf_fd returned invalid FD: [{}] for size [0x{:x}] "
               "({:g}MB), address [0x{:x}], offset [0x{:x}]. {}",
               dmabuf_fd,
               size,
               B2MB(size),
               addr,
               offset,
               std::strerror(dmabuf_fd * (-1)));
        m_dmabufFD = FileDescriptor(dmabuf_fd);

        if (ofi_t::isFabricFlush())
        {
            /* Register local (host) MR */
            m_mrFlushLocal =
                create_mr(m_flush_domain.value(), &m_flushLocalBuffer, sizeof(m_flushLocalBuffer), FI_HMEM_SYSTEM, 0);

            /* Register remote (device) MR */
            m_mrFlushRemote = create_mr(m_flush_domain.value(),
                                        reinterpret_cast<void*>(m_flushRemoteBuffer),
                                        sizeof(m_flushLocalBuffer),
                                        FI_HMEM_SYNAPSEAI,
                                        m_dmabufFD.get());
        }
    }

    const fi_hmem_iface iface = ofi_t::isGaudiDirect() ? FI_HMEM_SYNAPSEAI : FI_HMEM_SYSTEM;
    m_mr                      = create_mr(m_domain.get(), (void*)(addr + offset), size, iface, m_dmabufFD.value_or(0));
    if (!GCFG_HCL_SINGLE_QP_PER_SET.value())
    {
        m_mrSingle = create_mr(m_domain_single, (void*)(addr + offset), size, iface, m_dmabufFD.value_or(0));
    }
}

int ofi_component_t::getDmabufFd()
{
    VERIFY(m_dmabufFD.has_value(), "Missing dmabuf FD.");
    return m_dmabufFD;
}
