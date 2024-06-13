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

ofi_component_t::ofi_component_t(int ofiDeviceID, int hw_module_id, struct fi_info* prov, int cpuid)
: m_ofiDeviceID(ofiDeviceID),
  m_cpuid(cpuid),
  m_refcnt(0),
  m_cqe_burst(GCFG_OFI_CQ_BURST_PROC.value()),
  m_prov(prov),
  m_fabric(nullptr),
  m_domain(nullptr),
  m_cq(nullptr),
  m_flush_cq(nullptr)
{
}

ofi_component_t::~ofi_component_t()
{
    MRMapping::get_instance().deregisterMR();

    if (ofi_t::isGaudiDirect())
    {
        MRMapping::get_instance().closeFD();
    }

    if (m_cq)
    {
        ofi_plugin->w_fi_close((fid_t)m_cq);
        m_cq = nullptr;
    }
    if (m_flush_cq)
    {
        ofi_plugin->w_fi_close((fid_t)m_flush_cq);
        m_flush_cq = nullptr;
    }
    if (m_domain)
    {
        ofi_plugin->w_fi_close((fid_t)m_domain);
        m_domain = nullptr;
    }

    if (m_fabric)
    {
        ofi_plugin->w_fi_close((fid_t)m_fabric);
        m_fabric = nullptr;
    }
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
        rc           = ofi_plugin->w_fi_cq_read(m_cq, cq_buf, m_cqe_burst);
        if (rc > 0)
        {
            OFI_EXIT_ON_ERROR(process_completions(cq_buf, rc));
        }
        else if (OFI_UNLIKELY(rc == -FI_EAVAIL))
        {
            const ssize_t prev_rc = rc;
            rc                    = ofi_plugin->w_fi_cq_readerr(m_cq, &err_buffer, 0);
            if (OFI_UNLIKELY(rc < 0))
            {
                LOG_HCL_ERR(HCL,
                            "Unable to read from fi_cq_readerr; RC: {}, ERROR: {}",
                            rc,
                            ofi_plugin->w_fi_cq_strerror(m_cq, err_buffer.prov_errno, err_buffer.err_data, nullptr, 0));
                return hcclLibfabricError;
            }

            ofi_req_t* req = container_of(err_buffer.op_context, ofi_req_t, ctx);
            req->state     = OFI_REQ_ERROR;
            req->size      = err_buffer.len;
            LOG_HCL_ERR(HCL_OFI,
                        "Error state, w_fi_cq_read RC: {}, ERROR: {}",
                        prev_rc,
                        ofi_plugin->w_fi_cq_strerror(m_cq, err_buffer.prov_errno, err_buffer.err_data, nullptr, 0));
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
    void*                  cqe_ptr = &cqe;

    // Receive flush completion for the given endpoint
    rc = ofi_plugin->w_fi_cq_read(m_flush_cq, cqe_ptr, 1);
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
        rc                    = ofi_plugin->w_fi_cq_readerr(m_flush_cq, &err_buffer, 0);
        if (OFI_UNLIKELY(rc < 0))
        {
            LOG_HCL_ERR(HCL,
                        "Unable to read from fi_cq_readerr; RC: {}, ERROR: {}",
                        rc,
                        ofi_plugin->w_fi_cq_strerror(m_flush_cq, err_buffer.prov_errno, err_buffer.err_data, NULL, 0));
            return hcclLibfabricError;
        }

        req        = container_of(err_buffer.op_context, ofi_req_t, ctx);
        req->state = OFI_REQ_ERROR;
        req->size  = err_buffer.len;
        LOG_HCL_ERR(HCL_OFI,
                    "Error state, w_fi_cq_read RC: {}, ERROR: {}",
                    prev_rc,
                    ofi_plugin->w_fi_cq_strerror(m_flush_cq, err_buffer.prov_errno, err_buffer.err_data, NULL, 0));
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
                                 struct fid_mr** mHandle)
{
    hcclResult_t      ret     = hcclSuccess;
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

    int rc = ofi_plugin->w_fi_mr_regattr(this->get_domain(), &mr_attr, 0, mHandle);

    if (OFI_UNLIKELY(rc != 0))
    {
        LOG_HCL_ERR(HCL_OFI, "Could not register memory region; RC: {}, ERROR: {}", rc, ofi_plugin->w_fi_strerror(-rc));
        ret = hcclLibfabricError;
    }
    else
    {
        LOG_HCL_INFO(HCL_OFI,
                     "MR registration complete. key={} address={} size={}MB",
                     (*mHandle)->key,
                     iov.iov_base,
                     B2MB(iov.iov_len));
    }

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

fid_domain* ofi_component_t::get_domain()
{
    return m_domain;
}

int ofi_component_t::_flush(ofiComm_t* ofiComm, uint64_t data, struct fid_mr* mrHandle, ofi_req_t& request)
{
    int      ret = 0;
    uint64_t key = 0ULL;
    ssize_t  rc  = 0;

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
    key = ofi_plugin->w_fi_mr_key(mrHandle);
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
            ofiComm->flush_ep,                        // Fabric endpoint on which to initiate read operation
            MRMapping::get_instance().getFlushBuf(),  // Local data buffer to read into
            sizeof(int),                              // Length of data to read
            ofi_plugin->w_fi_mr_desc(
                MRMapping::get_instance().getFlushMRHandle()),  // Descriptor associated with the local data buffer
            ofiComm->flush_ep_addr,  // Source address to read from for connectionless transfers
            data,                    // Remote CQ data to transfer with the operation
            key,                     // Protection key associated with the remote memory
            &request.ctx);           // User specified pointer to associate with the operation
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
