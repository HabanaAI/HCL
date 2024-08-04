#include "hl_ofi_rdm_component.h"

#include "hccl_ofi_wrapper_interface.h"  // for ofi_plugin_interface
#include "hccl_types.h"                  // for hcclLibfabricError, hcclSuccess
#include "hcl_utils.h"                   // for LOG_HCL_ERR, LOG_HCL_DEBUG
#include "libfabric/hl_ofi.h"            // for OFI_UNLIKELY, MAX_EP_ADDR
#include "hcl_log_manager.h"             // for LOG_ERR, LOG_DEBUG
#include "ofi_plugin.h"                  // for ofi_plugin
#include "rdma/fi_domain.h"              // for fi_mr_attr, fid_mr, fi_av_attr
#include "rdma/fi_endpoint.h"            // for fid_ep
#include "rdma/fi_eq.h"                  // for fi_cq_tagged_entry, fi_cq_er...
#include "rdma/fi_errno.h"               // for FI_EAGAIN, FI_EAVAIL

ofi_rdm_component_t::ofi_rdm_component_t(int ofiDeviceId, int hw_module_id, struct fi_info* prov, int cpuid)
: ofi_component_t(ofiDeviceId, hw_module_id, prov, cpuid, FI_CQ_FORMAT_TAGGED),
  m_cqe_tagged_buffers(m_cqe_burst),
  m_tag(hw_module_id << 28),
  m_max_tag(calculate_max_tag(prov))
{
}

uint64_t ofi_rdm_component_t::calculate_max_tag(const struct fi_info* const provider)
{
    int tag_leading_zeros = 0;
    int tag_bits_for_id   = 64;

    // Leading zeros in tag bits are used by provider
    while (!((provider->ep_attr->mem_tag_format << tag_leading_zeros++) & (uint64_t)OFI_HIGHEST_TAG_BIT) &&
           (tag_bits_for_id >= MIN_TAG_BITS_FOR_ID))
    {
        tag_bits_for_id--;
    }
    VERIFY(tag_bits_for_id >= MIN_TAG_BITS_FOR_ID);
    return static_cast<uint64_t>((1ull << (tag_bits_for_id - 1)) - 1);
}

void* ofi_rdm_component_t::get_cq_buf()
{
    return m_cqe_tagged_buffers.data();
}

int ofi_rdm_component_t::next_tag(uint64_t* tag)
{
    int ret = hcclSuccess;

    if (m_tag + 1 >= m_max_tag)
    {
        LOG_HCL_ERR(HCL_OFI, "Can't open more connections for OFI device ID {}", m_ofiDeviceID);
        ret = hcclLibfabricError;
    }
    else
    {
        // Increment m_tag by 1
        *tag = ++m_tag;
    }

    return ret;
}

int ofi_rdm_component_t::listen(uint64_t       tag,
                                void*          handle,
                                listenComm_t** listenComm,
                                unsigned       hostConnIdx,
                                uint16_t       qpSetIndex)
{
    int                           ret                  = hcclUninitialized;
    char                          ep_name[MAX_EP_ADDR] = {0};
    size_t                        namelen              = sizeof(ep_name);
    fi_addr_t                     local_ep_addr;
    std::unique_ptr<listenComm_t> lComm;
    std::vector<uint8_t>          addr;

    const auto [ep, av] = acquire_ep_av(hostConnIdx, EndpointRole::LISTEN, qpSetIndex);

    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_getname(&(ep->get()->fid), (void*)&ep_name, &namelen));
    addr = std::vector<uint8_t>(ep_name, ep_name + namelen);

    memcpy(handle, ep_name, MAX_EP_ADDR);
    memcpy(static_cast<char*>(handle) + MAX_EP_ADDR, &tag, sizeof(tag));
    try
    {
        local_ep_addr = m_av_addr.at(av).at(addr);
    }
    catch (const std::out_of_range&)
    {
        OFI_EXIT_ON_ERROR_VALUE(
            ofi_plugin->w_fi_av_insert(*av, static_cast<void*>(ep_name), 1, &local_ep_addr, 0, nullptr),
            1);
        m_av_addr[av][addr] = local_ep_addr;
    }

    // Build listenComm
    lComm                = std::make_unique<listenComm_t>();
    lComm->tag           = tag;
    lComm->local_ep      = *ep;
    lComm->accepted      = false;
    lComm->dev           = m_ofiDeviceID;
    lComm->local_ep_addr = local_ep_addr;

    LOG_HCL_DEBUG(HCL_OFI,
                  "listenComm initialized; EP: {}, tag: {}",
                  ofi_plugin->w_fi_tostr(&lComm->local_ep_addr, FI_TYPE_ADDR_FORMAT),
                  tag);

    *listenComm = lComm.release();
    ret         = hcclSuccess;
error:
    return ret;
}

int ofi_rdm_component_t::accept(listenComm_t* lComm, ofiComm_t** ofiComm)
{
    int                        ret                         = hcclUninitialized;
    ssize_t                    rc                          = 0;
    char                       remote_ep_addr[MAX_EP_ADDR] = {0};
    fi_addr_t                  remote_ep                   = 0;
    std::unique_ptr<ofiComm_t> oComm;

    assert(lComm->dev == m_ofiDeviceID);

    if (lComm->accepted)
    {
        LOG_HCL_ERR(HCL_OFI, "listenComm object already has an active connection");
        return hcclLibfabricError;
    }

    ofi_req_t req;
    req.state     = OFI_REQ_CREATED;
    req.lComm     = lComm;
    req.ofiDevice = m_ofiDeviceID;

    // Post a buffer for receiving "connection" request
    do
    {
        rc = ofi_plugin->w_fi_trecv(lComm->local_ep,
                                    (void*)&remote_ep_addr,
                                    MAX_EP_ADDR,
                                    nullptr,
                                    FI_ADDR_UNSPEC,
                                    lComm->tag | ~m_max_tag,
                                    0,
                                    &req.ctx);
        if (rc == 0)
        {
            break;
        }
        else if (rc == -FI_EAGAIN)
        {
            // Process completions to have enough resources for posting recv
            OFI_EXIT_ON_ERROR(ofi_progress());
        }
        else
        {
            LOG_HCL_ERR(HCL_OFI,
                        "Unable to post a buffer for receiving connections "
                        "for OFI device ID {}; RC: {}, ERROR: {}",
                        m_ofiDeviceID,
                        rc,
                        ofi_plugin->w_fi_strerror(-rc));
            return hcclLibfabricError;
        }
    } while (true);
    LOG_HCL_DEBUG(HCL_OFI, "Received connection request from peer");

    // Progress OFI until connection is accepted
    while (!lComm->accepted)
    {
        OFI_EXIT_ON_ERROR(ofi_progress());
    }
    LOG_HCL_DEBUG(HCL_OFI, "Connection accepted");

    // Build recvComm
    oComm                 = std::make_unique<ofiComm_t>();
    oComm->tag            = lComm->tag;
    oComm->local_ep       = lComm->local_ep;
    oComm->local_ep_addr  = lComm->local_ep_addr;
    oComm->remote_ep_addr = remote_ep;
    oComm->dev            = m_ofiDeviceID;

    LOG_HCL_DEBUG(HCL_OFI, "ofiComm (accept) initialized for OFI device ID {}; tag {}", oComm->dev, oComm->tag);
    *ofiComm = oComm.release();
    ret      = hcclSuccess;
error:
    return ret;
}

int ofi_rdm_component_t::connect(const void* handle,
                                 ofiComm_t** ofiComm,
                                 void*       localAddr,
                                 unsigned    hostConnIdx,
                                 uint16_t    qpSetIndex)
{
    int                        ret                         = hcclUninitialized;
    ssize_t                    rc                          = 0;
    uint64_t                   tag                         = 0ull;
    char                       remote_ep_addr[MAX_EP_ADDR] = {0};
    char                       local_ep_addr[MAX_EP_ADDR]  = {0};
    size_t                     namelen                     = sizeof(local_ep_addr);
    std::unique_ptr<ofiComm_t> oComm;
    ofi_req_t                  req;
    fi_addr_t                  remote_addr;

    memcpy(&remote_ep_addr, static_cast<const char*>(handle), MAX_EP_ADDR);
    memcpy(&tag, static_cast<const char*>(handle) + MAX_EP_ADDR, sizeof(tag));
    if (tag < 1 || tag > m_max_tag)
    {
        LOG_HCL_ERR(HCL_OFI, "Received an invalid tag {} for OFI device ID {}", tag, m_ofiDeviceID);
        return hcclLibfabricError;
    }

    const auto [ep, av] = acquire_ep_av(hostConnIdx, EndpointRole::LISTEN, qpSetIndex);
    const std::vector<uint8_t> addr(&remote_ep_addr[0], &remote_ep_addr[0] + sizeof(remote_ep_addr));
    try
    {
        remote_addr = m_av_addr.at(av).at(addr);
    }
    catch (const std::out_of_range&)
    {
        // Insert remote address into AV
        OFI_EXIT_ON_ERROR_VALUE(ofi_plugin->w_fi_av_insert(*av, (void*)remote_ep_addr, 1, &remote_addr, 0, nullptr), 1);
        m_av_addr[av][addr] = remote_addr;
    }

    // Build ofiComm_t
    oComm                 = std::make_unique<ofiComm_t>();
    oComm->tag            = tag;
    oComm->local_ep       = *ep;
    oComm->remote_ep_addr = remote_addr;
    oComm->dev            = m_ofiDeviceID;

    LOG_HCL_DEBUG(HCL_OFI, "ofiComm (connect) initialized for OFI device ID {}; tag {}", oComm->dev, oComm->tag);

    // Can optimize this memory allocation by using a pre-allocated
    // buffer and reusing elements from there. For now, going with
    // simple memory allocation. Should remember to free at completion.
    req.ofiComm   = oComm.get();
    req.ofiDevice = oComm->dev;
    req.direction = OFI_SEND;

    // Get local EP address to transfer in the connect message
    OFI_EXIT_ON_ERROR(ofi_plugin->w_fi_getname(&(ep->get()->fid), (void*)&local_ep_addr, &namelen));

    // Send "connect" message to remote EP
    do
    {
        rc = ofi_plugin->w_fi_tsend(oComm->local_ep,
                                    (void*)&local_ep_addr,
                                    MAX_EP_ADDR,
                                    nullptr,
                                    oComm->remote_ep_addr,
                                    oComm->tag | ~m_max_tag,
                                    &req.ctx);
        if (rc == 0)
        {
            break;
        }
        else if (rc == -FI_EAGAIN)
        {
            // Process completions to have enough resources to send connect msg
            OFI_EXIT_ON_ERROR(ofi_progress());
        }
        else
        {
            LOG_HCL_ERR(HCL_OFI,
                        "Unable to send connect message for OFI device ID {}; RC: {}, ERROR: {}",
                        m_ofiDeviceID,
                        rc,
                        ofi_plugin->w_fi_strerror(-rc));
            return hcclLibfabricError;
        }
    } while (true);

    // Ensure the message is sent
    do
    {
        OFI_EXIT_ON_ERROR(ofi_progress());
    } while (req.state != OFI_REQ_COMPLETED);

    LOG_HCL_DEBUG(HCL_OFI, "Connect to remote-EP succeeded");
    *ofiComm = oComm.release();
    ret      = hcclSuccess;

error:
    return ret;
}

int ofi_rdm_component_t::process_completions(void* cq_buf, uint64_t num_cqes)
{
    int                        ret      = hcclUninitialized;
    ofi_req_t*                 req      = nullptr;
    uint64_t                   comp_idx = 0, comp_flags = 0;
    uint64_t                   control_bit_mask = ~(m_max_tag);
    struct fi_cq_tagged_entry* cq_entries       = (struct fi_cq_tagged_entry*)cq_buf;
    bool                       firstRecv        = true;

    for (comp_idx = 0; comp_idx < num_cqes; comp_idx++)
    {
        comp_flags = cq_entries[comp_idx].flags;

        req = container_of(cq_entries[comp_idx].op_context, ofi_req_t, ctx);
        if (OFI_UNLIKELY(req == nullptr))
        {
            LOG_HCL_ERR(HCL_OFI, "Invalid request context provided");
            return hcclLibfabricError;
        }

        req->state = OFI_REQ_COMPLETED;
        req->size  = cq_entries[comp_idx].len;

        if (firstRecv && req->direction == OFI_RECV)
        {
            firstRecv = false;
            OFI_EXIT_ON_ERROR(process_first_recv_completion(req));
        }

        // compCallBack should be initialized for operations during runtime
        if (req->compParams.compCallBack)
        {
            req->compParams.compCallBack(&req->compParams);
        }

        // Determine if this is a control message
        if (OFI_UNLIKELY(cq_entries[comp_idx].tag & control_bit_mask))
        {
            if (comp_flags & FI_RECV)
            {
                req->lComm->accepted = true;
            }
        }
    }

    ret = hcclSuccess;
error:
    return ret;
}

int ofi_rdm_component_t::isend(ofiComm_t*             ofiComm,
                               void*                  data,
                               size_t                 size,
                               fid_mr*                mHandle,
                               ofi_req_t**            request,
                               OfiCompCallbackParams& compParams)
{
    int     ret  = hcclUninitialized;
    void*   desc = nullptr;
    ssize_t rc   = -1;

    assert(m_ofiDeviceID == ofiComm->dev);

    std::unique_ptr<ofi_req_t> req = std::make_unique<ofi_req_t>();
    req->ofiComm                   = ofiComm;
    req->ofiDevice                 = ofiComm->dev;
    req->direction                 = OFI_SEND;
    req->compParams                = compParams;

    OFI_EXIT_ON_ERROR(ofi_progress());

    if (nullptr != mHandle)
    {
        desc = ofi_plugin->w_fi_mr_desc((fid_mr*)mHandle);
        if (nullptr == desc)
        {
            LOG_HCL_ERR(
                HCL_OFI,
                "Could not get descriptor for send operation using fi_mr_desc. addr: 0x{:x} size: 0x{:x} ({:g}MB).",
                (uint64_t)data,
                size,
                B2MB(size));
            return hcclLibfabricError;
        }
    }

    // Try sending data to remote EP; return nullptr request if not able to send
    rc = ofi_plugin->w_fi_tsend(ofiComm->local_ep, data, size, desc, ofiComm->remote_ep_addr, ofiComm->tag, &req->ctx);
    if (OFI_UNLIKELY(rc == -FI_EAGAIN))
    {
        *request = nullptr;
        return hcclTryAgainError;
    }
    else if (OFI_UNLIKELY(rc != 0))
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Could not send request for OFI device ID {}; RC: {}, ERROR: {}",
                    ofiComm->dev,
                    rc,
                    ofi_plugin->w_fi_strerror(-rc));
        return hcclLibfabricError;
    }

    ofiComm->num_inflight_sends++;

    *request = req.release();
    ret      = hcclSuccess;
error:
    return ret;
}

int ofi_rdm_component_t::irecv(ofiComm_t*             ofiComm,
                               void*                  data,
                               size_t                 size,
                               fid_mr*                mHandle,
                               ofi_req_t**            request,
                               OfiCompCallbackParams& compParams)
{
    int     ret  = hcclUninitialized;
    ssize_t rc   = 0;
    void*   desc = nullptr;

    assert(ofiComm->dev == m_ofiDeviceID);

    std::unique_ptr<ofi_req_t> req = std::make_unique<ofi_req_t>();
    req->ofiComm                   = ofiComm;
    req->ofiDevice                 = ofiComm->dev;
    req->direction                 = OFI_RECV;
    req->compParams                = compParams;

    OFI_EXIT_ON_ERROR(ofi_progress());

    if (nullptr != mHandle)
    {
        desc = ofi_plugin->w_fi_mr_desc((fid_mr*)mHandle);
        if (nullptr == desc)
        {
            LOG_HCL_ERR(HCL_OFI,
                        "Could not get descriptor for recv operation using fi_mr_desc. addr: {} size: 0x{:x}. ({:g}MB)",
                        (uint64_t)data,
                        size,
                        B2MB(size));
            return hcclLibfabricError;
        }
    }

    // Try posting buffer to local EP
    rc = ofi_plugin->w_fi_trecv(ofiComm->local_ep, data, size, desc, FI_ADDR_UNSPEC, ofiComm->tag, 0, &req->ctx);
    if (rc == -FI_EAGAIN)
    {
        // return nullptr request
        *request = nullptr;
        return hcclTryAgainError;
    }
    else if (rc != 0)
    {
        LOG_HCL_ERR(HCL_OFI,
                    "Unable to post receive buffer for OFI device ID {}; RC: {}, ERROR: {}",
                    ofiComm->dev,
                    rc,
                    ofi_plugin->w_fi_strerror(-rc));
        return hcclLibfabricError;
    }

    ofiComm->num_inflight_recvs++;

    *request = req.release();
    ret      = hcclSuccess;
error:
    return ret;
}

int ofi_rdm_component_t::close(ofiComm_t* ofiComm)
{
    LOG_HCL_DEBUG(HCL_OFI, "Freeing ofiComm for OFI device ID {}, tag {}", ofiComm->dev, ofiComm->tag);
    delete ofiComm;

    return hcclSuccess;
}

int ofi_rdm_component_t::close(listenComm_t* listenComm)
{
    LOG_HCL_DEBUG(HCL_OFI, "Freeing listenComm for OFI device ID {}, tag {}", listenComm->dev, listenComm->tag);
    delete listenComm;

    return hcclSuccess;
}

ofi_rdm_component_t::EpAv
ofi_rdm_component_t::acquire_ep_av(unsigned hostConnIdx, ofi_rdm_component_t::EndpointRole role, uint16_t qpSetIndex)
{
    for (const auto& [key, ep_av] : m_eps)
    {
        const auto [hostConnIdx_, role_, qpSetIndex_] = key;
        UNUSED(hostConnIdx_);
        UNUSED(role_);
        if (qpSetIndex_ != qpSetIndex)
        {
            continue;
        }
        // Found existing endpoint in the same set
        m_eps[std::make_tuple(hostConnIdx, role, qpSetIndex)] = ep_av;
        return ep_av;
    }
    const auto av = std::make_shared<FiObject<struct fid_av*>>(create_av(m_domain.get()));
    const auto ep =
        std::make_shared<FiObject<struct fid_ep*>>(create_ep(m_prov, m_domain.get(), m_cq.get(), av->get()));
    m_eps[std::make_tuple(hostConnIdx, role, qpSetIndex)] = std::make_tuple(ep, av);
    return m_eps[std::make_tuple(hostConnIdx, role, qpSetIndex)];
}
