#pragma once

#include <cstdint>           // for uint64_t
#include <cstring>           // for NULL, memset, size_t
#include <vector>            // for vector
#include "rdma/fabric.h"     // for fi_addr_t, fi_context
#include <rdma/fi_domain.h>  // for fi_hmem_iface
#include "platform/gen2_arch_common/host_scheduler.h"

#define OFI_EXIT_ON_ERROR(fn) OFI_EXIT_ON_ERROR_VALUE(fn, 0)
#define OFI_EXIT_ON_ERROR_VALUE(fn, expected_value)                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ret = (fn);                                                                                                    \
        if (OFI_UNLIKELY((expected_value) != ret))                                                                     \
        {                                                                                                              \
            LOG_HCL_ERR(HCL, #fn " returned error; RC: {}, ERROR: {}", ret, ofi_plugin->w_fi_strerror(-ret));          \
            ret = hcclLibfabricError;                                                                                  \
            goto error;                                                                                                \
        }                                                                                                              \
    } while (false)

enum ofi_req_state_t
{
    OFI_REQ_CREATED = 0,
    OFI_REQ_PENDING,
    OFI_REQ_COMPLETED,
    OFI_REQ_ERROR,
};

enum ofi_req_direction_t
{
    OFI_SEND = 1,
    OFI_RECV,
    OFI_FLUSH,
    OFI_INVALID
};

struct listenComm_t
{
    uint64_t tag;  // Invalid on FI_EP_MSG
    int      dev;
    bool     accepted;

    struct fid_ep* local_ep;
    fi_addr_t      local_ep_addr;

    // FI_EP_MSG only attributes
    struct fi_info* m_pep_prov;
    struct fid_pep* m_pep;
    struct fid_eq*  m_eq;
};

struct ofiComm_t
{
    int            dev;
    uint64_t       tag;
    uint64_t       num_inflight_sends;
    uint64_t       num_inflight_recvs;
    fi_addr_t      remote_ep_addr;
    fi_addr_t      local_ep_addr;
    struct fid_ep* local_ep;
    struct fid_ep* flush_ep;
    fi_addr_t      flush_ep_addr;

    // FI_EP_MSG only attributes
    struct fid_eq* m_eq;
};

struct allConnectionComm_t
{
    listenComm_t* listenComm;
    ofiComm_t*    sendComm;
    ofiComm_t*    recvComm;
};

class ofi_req_t
{
public:
    // Associated comm object
    union
    {
        listenComm_t* lComm;
        ofiComm_t*    ofiComm;
    };

    // Associated OFI context
    struct fi_context ctx;

    // Associated component ID
    int ofiDevice;

    // Size of completed request
    size_t size;

    // State of request
    ofi_req_state_t state;

    // Direction of request
    ofi_req_direction_t direction;

    // Completion params
    OfiCompCallbackParams compParams;

    ofi_req_t()
    {
        lComm   = NULL;
        ofiComm = NULL;

        memset(&ctx, 0, sizeof(struct fi_context));

        ofiDevice = -1;
        size      = 0;

        state = OFI_REQ_CREATED;

        direction = OFI_INVALID;

        compParams.compCallBack = nullptr;
    }

    ~ofi_req_t() = default;
};

//
// Structure of an OFI network component
//
// For resource management, refCnt is maintained internally.
// get/put functionality must be called in a pair when an object
// is acquired to use and released.
// Since this can be shared by multiple entities, it must be protected by
// lock.
//
class ofi_component_t
{
public:
    ofi_component_t(int ofiDeviceId, int hw_module_id, struct fi_info* prov, int cpuid);
    virtual ~ofi_component_t();

    int inc_refcnt() { return ++m_refcnt; }
    int dec_refcnt() { return --m_refcnt; }
    int get_refcnt() const { return m_refcnt; }

    virtual int   create_component() = 0;
    virtual void* get_cq_buf()       = 0;
    virtual int   next_tag(uint64_t* tag) { return 0; }

    virtual int listen(uint64_t tag, void* handle, listenComm_t** listenComm) = 0;
    virtual int connect(void* handle, ofiComm_t** ofiComm, void* localAddr)   = 0;
    virtual int accept(listenComm_t* listenComm, ofiComm_t** ofiComm)         = 0;
    virtual int isend(ofiComm_t*             ofiComm,
                      void*                  data,
                      size_t                 size,
                      fid_mr*                mHandle,
                      ofi_req_t**            request,
                      OfiCompCallbackParams& compParams)                      = 0;
    virtual int irecv(ofiComm_t*             ofiComm,
                      void*                  data,
                      size_t                 size,
                      fid_mr*                mHandle,
                      ofi_req_t**            request,
                      OfiCompCallbackParams& compParams)                      = 0;
    virtual int close(ofiComm_t* ofiComm)                                     = 0;
    virtual int close(listenComm_t* listenComm)                               = 0;

    int test(ofi_req_t* req, int* done, size_t* size);
    int _flush(ofiComm_t* ofiComm, uint64_t data, struct fid_mr* mrHandle, ofi_req_t& request);

    int register_mr(void* data, size_t size, fi_hmem_iface fi_hmem_iface, int device_fd, struct fid_mr** mHandle);
    static int deregister_mr(struct fid_mr* mHandle);

    fid_domain* get_domain();

protected:
    int         ofi_progress();
    int         ofi_flush_progress();
    virtual int process_completions(void* cq_buf, uint64_t num_cqes) = 0;

    int      m_ofiDeviceID;
    int      m_cpuid;
    int      m_refcnt;
    uint64_t m_cqe_burst;

    struct fi_info*    m_prov;
    struct fid_fabric* m_fabric;
    struct fid_domain* m_domain;
    struct fid_cq*     m_cq;
    struct fid_cq*     m_flush_cq;
};
