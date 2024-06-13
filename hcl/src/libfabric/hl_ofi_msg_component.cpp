#include "hl_ofi_msg_component.h"

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

ofi_msg_component_t::ofi_msg_component_t(int id, int hw_module_id, struct fi_info* prov, int cpuid)
: ofi_component_t(id, hw_module_id, prov, cpuid)
{
    m_cqe_msg_buffers.resize(m_cqe_burst);
}

int ofi_msg_component_t::create_component()
{
    int ret = hcclSuccess;

    struct fi_cq_attr cq_attr = {0};
    cq_attr.format            = FI_CQ_FORMAT_MSG;
    if (m_cpuid >= 0)
    {
        cq_attr.flags            = FI_AFFINITY;
        cq_attr.signaling_vector = m_cpuid;
        LOG_HCL_INFO(HCL_OFI, "setting CQ's affinity to cpuid {}", m_cpuid);
    }

    LOG_DEBUG(HCL_OFI, "Using provider to create a component: {}", ofi_plugin->w_fi_tostr(m_prov, FI_TYPE_INFO));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_fabric(m_prov->fabric_attr, &m_fabric, NULL));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_domain(m_fabric, m_prov, &m_domain, NULL));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_cq_open(m_domain, &cq_attr, &m_cq, NULL));

    m_refcnt = 1;
    return ret;

error:
    if (m_cq)
    {
        ofi_plugin->w_fi_close((fid_t)m_cq);
        m_cq = NULL;
    }

    if (m_domain)
    {
        ofi_plugin->w_fi_close((fid_t)m_domain);
        m_domain = NULL;
    }

    if (m_fabric)
    {
        ofi_plugin->w_fi_close((fid_t)m_fabric);
        m_fabric = NULL;
    }
    return ret;
}

void* ofi_msg_component_t::get_cq_buf()
{
    return m_cqe_msg_buffers.data();
}

int ofi_msg_component_t::read_eq_event(struct fid_eq* eq, struct fi_eq_cm_entry* entry, uint32_t* event)
{
    struct fi_eq_err_entry err_entry;
    ssize_t                ret;

    ret = ofi_plugin->w_fi_eq_sread(eq, event, entry, sizeof(*entry), -1, 0);
    if (ret != sizeof(*entry))
    {
        LOG_HCL_ERR(HCL_OFI, "fi_eq_sread returned {}, expecting {}", ret, sizeof(*entry));
        if (ret == -FI_EAVAIL)
        {
            ofi_plugin->w_fi_eq_readerr(eq, &err_entry, 0);
            LOG_HCL_ERR(HCL_OFI, "error {} prov_errno {}", err_entry.err, err_entry.prov_errno);
        }
        return (int)ret;
    }

    return 0;
}

/* Wait until the server (accept()) finds that it has a new connection to actually accept */
int ofi_msg_component_t::wait_for_connection(struct fid_eq* eq, struct fi_info** prov)
{
    struct fi_eq_cm_entry entry;
    uint32_t              event;

    int ret = read_eq_event(eq, &entry, &event);
    if (0 != ret)
    {
        return ret;
    }

    *prov = entry.info;
    if (event != FI_CONNREQ)
    {
        LOG_HCL_ERR(HCL_OFI, "unexpected CM event {}", event);
        return -FI_EOTHER;
    }

    return 0;
}

/* Wait until the connection is actually complete and successful */
int ofi_msg_component_t::wait_until_connected(struct fid_ep* ep, struct fid_eq* eq)
{
    struct fi_eq_cm_entry entry;
    uint32_t              event;

    int ret = read_eq_event(eq, &entry, &event);
    if (0 != ret)
    {
        return ret;
    }

    if (event != FI_CONNECTED || entry.fid != &ep->fid)
    {
        LOG_HCL_ERR(HCL_OFI, "unexpected CM event {} fid {} (ep {})", event, entry.fid, ep);
        return -FI_EOTHER;
    }

    return 0;
}

int ofi_msg_component_t::listen(uint64_t tag, void* handle, listenComm_t** listenComm)
{
    int               ret;
    char              ep_name[MAX_EP_ADDR] = {0};
    size_t            namelen              = sizeof(ep_name);
    listenComm_t*     lComm                = (listenComm_t*)calloc(1, sizeof(listenComm_t));
    struct fi_eq_attr eq_attr;
    eq_attr.wait_obj = FI_WAIT_UNSPEC;

    lComm->m_pep_prov = m_prov;
    LOG_DEBUG(HCL_OFI, "Using provider to for pep: {}", ofi_plugin->w_fi_tostr(lComm->m_pep_prov, FI_TYPE_INFO));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_passive_ep(m_fabric, lComm->m_pep_prov, &lComm->m_pep, NULL));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_eq_open(m_fabric, &eq_attr, &lComm->m_eq, NULL));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_pep_bind(lComm->m_pep, (fid_t)lComm->m_eq, 0));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_listen(lComm->m_pep));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_getname(&(lComm->m_pep->fid), (void*)&ep_name, &namelen));
    memcpy(handle, ep_name, MAX_EP_ADDR);

    lComm->accepted = false;
    lComm->dev      = m_ofiDeviceID;

    LOG_HCL_DEBUG(HCL_OFI,
                  "listenComm initialized; EP: {}, tag: {}",
                  ofi_plugin->w_fi_tostr(&lComm->m_pep_prov->src_addr, FI_TYPE_ADDR_FORMAT),
                  tag);
    *listenComm = lComm;

error:
    return ret;
}

int ofi_msg_component_t::accept(listenComm_t* lComm, ofiComm_t** oComm)
{
    int        ret;
    ofiComm_t* ofiComm = NULL;
    ofi_req_t* req     = NULL;

    assert(lComm->dev == m_ofiDeviceID);

    if (lComm->accepted == true)
    {
        LOG_HCL_ERR(HCL_OFI, "listenComm object already has an active connection");
        return hcclLibfabricError;
    }

    ofiComm = (ofiComm_t*)calloc(1, sizeof(*ofiComm));
    if (ofiComm == NULL)
    {
        LOG_HCL_ERR(HCL_OFI, "Unable to allocate ofiComm object for OFI device ID {}", m_ofiDeviceID);
        return hcclLibfabricError;
    }

    req            = new ofi_req_t();
    req->state     = OFI_REQ_CREATED;
    req->lComm     = lComm;
    req->ofiDevice = m_ofiDeviceID;

    struct fi_eq_attr eq_attr;
    eq_attr.wait_obj = FI_WAIT_UNSPEC;

    OFI_EXIT_ON_ERROR(wait_for_connection(lComm->m_eq, &m_prov));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_endpoint(m_domain, m_prov, &ofiComm->local_ep, NULL));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_eq_open(m_fabric, &eq_attr, &ofiComm->m_eq, NULL));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_ep_bind(ofiComm->local_ep, (fid_t)ofiComm->m_eq, 0));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_ep_bind(ofiComm->local_ep, (fid_t)m_cq, FI_SEND | FI_RECV));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_enable(ofiComm->local_ep));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_accept(ofiComm->local_ep, NULL, 0));
    OFI_EXIT_ON_ERROR(wait_until_connected(ofiComm->local_ep, ofiComm->m_eq));

    ofiComm->dev = m_ofiDeviceID;

    LOG_HCL_DEBUG(HCL_OFI, "ofiComm initialized for OFI device ID {}", ofiComm->dev);
    *oComm = ofiComm;

    ret = hcclSuccess;

error:
    if (req)
    {
        delete req;
    }
    return ret;
}

int ofi_msg_component_t::connect(void* handle, ofiComm_t** oComm, void* localAddr)
{
    int        ret                         = hcclSuccess;
    char       remote_ep_addr[MAX_EP_ADDR] = {0};
    ofiComm_t* ofiComm                     = NULL;
    ofi_req_t* req                         = NULL;

    struct fi_eq_attr eq_attr;
    eq_attr.wait_obj = FI_WAIT_UNSPEC;

    memcpy(&remote_ep_addr, (char*)handle, MAX_EP_ADDR);

    ofiComm = (ofiComm_t*)calloc(1, sizeof(*ofiComm));
    if (OFI_UNLIKELY(ofiComm == NULL))
    {
        LOG_HCL_ERR(HCL_OFI, "Couldn't allocate ofiComm for OFI device ID {}", m_ofiDeviceID);
        return hcclLibfabricError;
    }

    char                str[INET_ADDRSTRLEN] = {0};
    struct sockaddr_in* sin                  = (struct sockaddr_in*)remote_ep_addr;
    VERIFY(NULL != inet_ntop(sin->sin_family, &sin->sin_addr, str, sizeof(str)));

    fi_info* hints      = ofi_plugin->w_fi_dupinfo(m_prov);
    hints->addr_format  = FI_SOCKADDR_IN;
    hints->src_addr     = malloc(MAX_EP_ADDR);
    hints->src_addrlen  = MAX_EP_ADDR;
    hints->dest_addr    = nullptr;
    hints->dest_addrlen = 0;
    hints->handle       = nullptr;

    memcpy(hints->src_addr, localAddr, MAX_EP_ADDR);
    struct sockaddr_in* sinLocal = (struct sockaddr_in*)hints->src_addr;
    sinLocal->sin_port           = htons(0);  // let libfabric decide randomly

    LOG_DEBUG(HCL_OFI,
              "Attempting to connect to addr {}:{} with hints: {}",
              str,
              htons(sin->sin_port),
              ofi_plugin->w_fi_tostr(hints, FI_TYPE_INFO));

    OFI_EXIT_ON_ERROR_VALUE(
        ofi_plugin
            ->w_fi_getinfo(ofi_version, str, std::to_string(htons(sin->sin_port)).c_str(), 0ULL, hints, &m_prov), 0);
    ofi_plugin->w_fi_freeinfo(hints);

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_endpoint(m_domain, m_prov, &ofiComm->local_ep, NULL));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_eq_open(m_fabric, &eq_attr, &ofiComm->m_eq, NULL));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_ep_bind(ofiComm->local_ep, (fid_t)ofiComm->m_eq, 0));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_ep_bind(ofiComm->local_ep, (fid_t)m_cq, FI_SEND | FI_RECV));
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_enable(ofiComm->local_ep));

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_connect(ofiComm->local_ep, remote_ep_addr, NULL, 0));
    OFI_EXIT_ON_ERROR(wait_until_connected(ofiComm->local_ep, ofiComm->m_eq));

    ofiComm->dev = m_ofiDeviceID;

    LOG_HCL_DEBUG(HCL_OFI, "ofiComm initialized for OFI device ID {}", ofiComm->dev);

    // Can optimize this memory allocation by using a pre-allocated
    // buffer and reusing elements from there. For now, going with
    // simple memory allocation. Should remember to free at completion.
    req            = new ofi_req_t;
    req->ofiComm   = ofiComm;
    req->ofiDevice = ofiComm->dev;
    req->direction = OFI_SEND;

    *oComm = ofiComm;

    ret = hcclSuccess;
    goto exit;

error:
    if (ofiComm)
    {
        free(ofiComm);
    }

exit:
    if (req)
    {
        delete req;
    }
    return ret;
}

int ofi_msg_component_t::process_completions(void* cq_buf, uint64_t num_cqes)
{
    ofi_req_t*              req      = NULL;
    uint64_t                comp_idx = 0, comp_flags = 0;
    struct fi_cq_msg_entry* cq_entries = (struct fi_cq_msg_entry*)cq_buf;

    for (comp_idx = 0; comp_idx < num_cqes; comp_idx++)
    {
        comp_flags = cq_entries[comp_idx].flags;

        req = container_of(cq_entries[comp_idx].op_context, ofi_req_t, ctx);
        if (OFI_UNLIKELY(req == NULL))
        {
            LOG_HCL_ERR(HCL_OFI, "Invalid request context provided");
            return hcclLibfabricError;
        }

        req->state = OFI_REQ_COMPLETED;
        req->size  = cq_entries[comp_idx].len;

        // compCallBack should be initialized for operations during runtime
        if (req->compParams.compCallBack)
        {
            req->compParams.compCallBack(&req->compParams);
        }

        if (unlikely(!req->lComm->accepted && req->size > 0 && comp_flags & FI_RECV))
        {
            req->lComm->accepted = true;
        }
    }

    return hcclSuccess;
}

int ofi_msg_component_t::isend(ofiComm_t*             ofiComm,
                               void*                  data,
                               size_t                 size,
                               fid_mr*                mHandle,
                               ofi_req_t**            request,
                               OfiCompCallbackParams& compParams)
{
    int        ret;
    ssize_t    rc   = 0;
    ofi_req_t* req  = NULL;
    void*      desc = NULL;

    assert(m_ofiDeviceID == ofiComm->dev);

    req            = new ofi_req_t;
    req->ofiComm   = ofiComm;
    req->ofiDevice = ofiComm->dev;
    req->direction = OFI_SEND;
    req->compParams = compParams;

    ret = ofi_progress();
    if (OFI_UNLIKELY(ret != 0))
    {
        if (req)
        {
            delete req;
        }
        return hcclLibfabricError;
    }

    if (mHandle != NULL)
    {
        desc = ofi_plugin->w_fi_mr_desc(mHandle);
        if (desc == 0)
        {
            LOG_HCL_ERR(
                HCL_OFI,
                "Could not get descriptor for send operation using fi_mr_desc. addr: 0x{:x} size: 0x{:x} ({:g}MB).",
                (uint64_t)data,
                size,
                B2MB(size));
            if (req)
            {
                delete req;
            }
            return hcclLibfabricError;
        }
    }

    // Try sending data to remote EP; return NULL request if not able to send
    rc = ofi_plugin->w_fi_send(ofiComm->local_ep, data, size, desc, ofiComm->remote_ep_addr, &req->ctx);
    if (OFI_UNLIKELY(rc == -FI_EAGAIN))
    {
        *request = NULL;
        if (req)
        {
            delete req;
        }
        return hcclTryAgainError;
    }
    else if (OFI_UNLIKELY(rc != 0))
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Could not send request for OFI device ID {}; RC: {}, ERROR: {}",
                    ofiComm->dev,
                    rc,
                    ofi_plugin->w_fi_strerror(-rc));
        if (req)
        {
            delete req;
        }
        return hcclLibfabricError;
    }

    ofiComm->num_inflight_sends++;

    *request = req;

    return hcclSuccess;
}

int ofi_msg_component_t::irecv(ofiComm_t*             recvComm,
                               void*                  data,
                               size_t                 size,
                               fid_mr*                mHandle,
                               ofi_req_t**            request,
                               OfiCompCallbackParams& compParams)
{
    int        ret;
    ssize_t    rc      = 0;
    ofi_req_t* req     = NULL;
    ofiComm_t* ofiComm = (ofiComm_t*)recvComm;
    void*      desc    = NULL;

    assert(ofiComm->dev == m_ofiDeviceID);

    req            = new ofi_req_t;
    req->ofiComm   = ofiComm;
    req->ofiDevice = ofiComm->dev;
    req->direction = OFI_RECV;
    req->compParams = compParams;

    ret = ofi_progress();
    if (OFI_UNLIKELY(ret != 0))
    {
        if (req)
        {
            delete req;
        }
        return hcclLibfabricError;
    }

    if (mHandle != NULL)
    {
        desc = ofi_plugin->w_fi_mr_desc(mHandle);
        if (desc == 0)
        {
            LOG_HCL_ERR(HCL_OFI,
                        "Could not get descriptor for recv operation using fi_mr_desc. addr: {} size: 0x{:x}. ({:g}MB)",
                        (uint64_t)data,
                        size,
                        B2MB(size));
            if (req)
            {
                delete req;
            }
            return hcclLibfabricError;
        }
    }

    // Try posting buffer to local EP
    rc = ofi_plugin->w_fi_recv(ofiComm->local_ep, data, size, desc, FI_ADDR_UNSPEC, &req->ctx);
    if (rc == -FI_EAGAIN)
    {
        // return NULL request
        *request = NULL;
        if (req)
        {
            delete req;
        }
        return hcclTryAgainError;
    }
    else if (rc != 0)
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Unable to post receive buffer for OFI device ID {}; RC: {}, ERROR: {}",
                    ofiComm->dev,
                    rc,
                    ofi_plugin->w_fi_strerror(-rc));
        if (req)
        {
            delete req;
        }
        return hcclLibfabricError;
    }

    ofiComm->num_inflight_recvs++;

    *request = req;

    return hcclSuccess;
}

int ofi_msg_component_t::close(ofiComm_t* ofiComm)
{
    LOG_HCL_DEBUG(HCL_OFI, "Freeing ofiComm for OFI device ID {}", ofiComm->dev);

    ofi_plugin->w_fi_close((fid_t)ofiComm->local_ep);
    ofi_plugin->w_fi_close((fid_t)ofiComm->m_eq);

    free(ofiComm);

    return hcclSuccess;
}

int ofi_msg_component_t::close(listenComm_t* listenComm)
{
    LOG_HCL_DEBUG(HCL_OFI, "Freeing listenComm for OFI device ID {}", listenComm->dev);

    ofi_plugin->w_fi_close((fid_t)listenComm->m_pep);
    ofi_plugin->w_fi_close((fid_t)listenComm->m_eq);
    listenComm->m_pep = NULL;
    listenComm->m_eq  = NULL;

    free(listenComm);

    return hcclSuccess;
}
