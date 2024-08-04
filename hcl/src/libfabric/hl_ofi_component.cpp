#include "hl_ofi_component.h"
#include <sys/types.h>                   // for ssize_t
#include <sys/uio.h>                     // for iovec
#include <cassert>                       // for assert
#include <cstdlib>                       // for free, calloc
#include <memory>                        // for unique_ptr
#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface
#include "hccl_types.h"                  // for hcclLibfabricError, hcclSuccess
#include "hcl_utils.h"                   // for LOG_HCL_ERR, LOG_HCL_DEBUG
#include "libfabric/hl_ofi.h"            // for OFI_UNLIKELY, MAX_EP_ADDR
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG
#include "mr_mapping.h"                  // for MRMapping
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

ofi_component_t::ofi_component_t(const int               ofiDeviceID,
                                 const int               hw_module_id,
                                 struct fi_info* const   prov,
                                 const int               cpuid,
                                 const enum fi_cq_format cq_format)
: m_ofiDeviceID(ofiDeviceID),
  m_cpuid(cpuid),
  m_refcnt(1),
  m_cqe_burst(GCFG_OFI_CQ_BURST_PROC.value()),
  m_prov(prov),
  m_fabric(create_fabric(m_prov)),
  m_domain(create_domain(m_prov, m_fabric.get())),
  m_cq(create_cq(m_domain.get(), cpuid, cq_format)),
  m_flush_provider(IF_GDR(get_flush_provider())),
  m_flush_fabric(IF_GDR(create_fabric(*m_flush_provider))),
  m_flush_domain(IF_GDR(create_domain(*m_flush_provider, m_flush_fabric.value().get()))),
  m_flush_cq(IF_GDR(create_cq(m_flush_domain.value().get(), m_cpuid, FI_CQ_FORMAT_TAGGED))),
  m_flush_av(IF_GDR(create_av(m_flush_domain.value().get()))),
  m_flush_ep(IF_GDR(
      create_ep(*m_flush_provider, m_flush_domain.value().get(), m_flush_cq.value().get(), m_flush_av.value().get()))),
  m_flush_addr(IF_GDR(create_address(m_flush_ep.value().get(), m_flush_av.value().get())))
{
    LOG_DEBUG(HCL_OFI, "Using provider to create a component: {}", ofi_plugin->w_fi_tostr(m_prov, FI_TYPE_INFO));
}

ofi_component_t::~ofi_component_t()
{
    MRMapping::get_instance().deregisterMR();

    if (ofi_t::isGaudiDirect())
    {
        MRMapping::get_instance().closeFD();
    }

    if (m_flush_provider.has_value())
    {
        ofi_plugin->w_fi_freeinfo(m_flush_provider.value());
    }
}

fi_info* ofi_component_t::get_flush_provider()
{
    struct fi_info* const hints = ofi_plugin->w_fi_allocinfo();
    VERIFY(nullptr != hints);

    get_hints(hints, true);

    struct fi_info* fi_getinfo_result = nullptr;
    VERIFY(0 == ofi_plugin->w_fi_getinfo(ofi_version, nullptr, nullptr, 0ULL, hints, &fi_getinfo_result));
    ofi_plugin->w_fi_freeinfo(hints);
    LOG_DEBUG(HCL_OFI, "Found provider for fabric flush: {}", ofi_plugin->w_fi_tostr(fi_getinfo_result, FI_TYPE_INFO));
    return fi_getinfo_result;
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

int ofi_component_t::ofi_progress()
{
    ssize_t                rc         = 0;
    int                    ret        = hcclUninitialized;
    struct fi_cq_err_entry err_buffer = {0};

    while (true)
    {
        // Receive completions for the given endpoint
        void* cq_buf = get_cq_buf();
        rc           = ofi_plugin->w_fi_cq_read(m_cq.get(), cq_buf, m_cqe_burst);
        if (rc > 0)
        {
            OFI_EXIT_ON_ERROR(process_completions(cq_buf, rc));
        }
        else if (OFI_UNLIKELY(rc == -FI_EAVAIL))
        {
            const ssize_t prev_rc = rc;
            rc                    = ofi_plugin->w_fi_cq_readerr(m_cq.get(), &err_buffer, 0);
            if (OFI_UNLIKELY(rc < 0))
            {
                LOG_HCL_ERR(
                    HCL,
                    "Unable to read from fi_cq_readerr; RC: {}, ERROR: {}",
                    rc,
                    ofi_plugin->w_fi_cq_strerror(m_cq.get(), err_buffer.prov_errno, err_buffer.err_data, nullptr, 0));
                return hcclLibfabricError;
            }

            ofi_req_t* req = container_of(err_buffer.op_context, ofi_req_t, ctx);
            req->state     = OFI_REQ_ERROR;
            req->size      = err_buffer.len;
            LOG_HCL_ERR(
                HCL_OFI,
                "Error state, w_fi_cq_read RC: {}, ERROR: {}",
                prev_rc,
                ofi_plugin->w_fi_cq_strerror(m_cq.get(), err_buffer.prov_errno, err_buffer.err_data, nullptr, 0));
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

int ofi_component_t::register_mr(void*           data,
                                 size_t          size,
                                 fi_hmem_iface   fi_hmem_iface,
                                 int             device_fd,
                                 struct fid_mr** mHandle,
                                 bool            isFlush)
{
    int               ret     = hcclUninitialized;
    struct fi_mr_attr mr_attr = {0};
    struct iovec      iov     = {0};

    iov.iov_base = data;
    iov.iov_len  = size;

    mr_attr.mr_iov    = &iov;
    mr_attr.iov_count = 1;
    mr_attr.access    = FI_SEND | FI_RECV;
    mr_attr.iface     = fi_hmem_iface;
    if (device_fd > 0)
    {
        mr_attr.device.synapseai = device_fd;
    }

    LOG_HCL_DEBUG(HCL_OFI, "MR registration attempt for {}, size {}", iov.iov_base, (void*)iov.iov_len);
    const auto domain = isFlush ? m_flush_domain.value().get() : m_domain.get();
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_mr_regattr(domain, &mr_attr, 0, mHandle));

    LOG_HCL_INFO(HCL_OFI,
                 "MR registration{} complete. mHandle={}, key={} address={} size={}MB",
                 isFlush ? " for flush" : "",
                 *mHandle,
                 (*mHandle)->key,
                 iov.iov_base,
                 B2MB(iov.iov_len));

    ret = hcclSuccess;
error:
    return ret;
}

int ofi_component_t::deregister_mr(struct fid_mr* mHandle)
{
    hcclResult_t ret = hcclSuccess;
    int          rc;

    if (OFI_UNLIKELY(mHandle == nullptr))
    {
        return ret;
    }

    LOG_DEBUG(HCL_OFI, "MR deregistration for key {}", mHandle->key);

    rc = ofi_plugin->w_fi_close((fid_t)mHandle);
    if (OFI_UNLIKELY(rc != 0))
    {
        LOG_ERR(HCL_OFI, "Unable to deregister local memory. RC: {}, ERROR: {}", rc, ofi_plugin->w_fi_strerror(-rc));
        ret = hcclSystemError;
    }

    return ret;
}

int ofi_component_t::test(ofi_req_t* req, int* done, size_t* size)
{
    int        ret;
    ofiComm_t* ofiComm = nullptr;

    // Try to complete requests only if the given request wasn't completed
    if ((req->state != OFI_REQ_COMPLETED && req->state != OFI_REQ_ERROR))
    {
        ret = ofi_progress();
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
            delete req;
            return hcclLibfabricError;
        }
        delete req;
    }
    else
        *done = 0;

    return hcclSuccess;
}

int ofi_component_t::_flush(ofiComm_t* ofiComm, uint64_t data, struct fid_mr* mrHandle, ofi_req_t& request)
{
    int     ret = hcclSuccess;
    ssize_t rc  = 0;

    // Validate rComm
    if (OFI_UNLIKELY(nullptr == ofiComm))
    {
        LOG_HCL_ERR(HCL_OFI, "Invalid recvComm was provided for PCIe flush");
        return hcclLibfabricError;
    }

    if (OFI_UNLIKELY(nullptr == mrHandle))
    {
        LOG_HCL_ERR(HCL_OFI, "Missing MR handle");
        return hcclLibfabricError;
    }

    // Extract remote key
    const uint64_t key = ofi_plugin->w_fi_mr_key(mrHandle);
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
                  data,
                  (uint64_t)MRMapping::get_instance().getFlushBuf());
    // RDMA read
    do
    {
        rc = ofi_plugin->w_fi_read(
            m_flush_ep.value().get(),                 // Fabric endpoint on which to initiate read operation
            MRMapping::get_instance().getFlushBuf(),  // Local data buffer to read into
            sizeof(int),                              // Length of data to read
            ofi_plugin->w_fi_mr_desc(
                MRMapping::get_instance().getFlushMRLocalHandle()),  // Descriptor associated with the local data buffer
            *m_flush_addr,  // Source address to read from for connectionless transfers
            data,           // Remote CQ data to transfer with the operation
            key,            // Protection key associated with the remote memory
            &request.ctx);  // User specified pointer to associate with the operation
        if (rc == 0)
        {
            break;
        }
        else if (rc == -FI_EAGAIN)
        {
            LOG_HCL_WARN(HCL_OFI,
                         "Insufficient resources for fi_read operation, try again. RC: {}, ERROR: {}",
                         ofiComm->dev,
                         rc,
                         ofi_plugin->w_fi_strerror(-rc));
            ret = ofi_flush_progress();
            if (OFI_UNLIKELY(ret != 0))
            {
                return hcclLibfabricError;
            }
        }
        else
        {
            LOG_HCL_ERR(HCL_OFI,
                        "Unable to issue read operation for dev {}. RC: {}, ERROR: {}",
                        ofiComm->dev,
                        rc,
                        ofi_plugin->w_fi_strerror(-rc));
            return hcclLibfabricError;
        }
    } while (true);

    return ret;
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

    OFI_EXIT_ON_ERROR(_flush(req->ofiComm,
                             MRMapping::get_instance().getDramBaseAddr(),
                             MRMapping::get_instance().getFlushMRRemoteHandle(),
                             flushReq));

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
