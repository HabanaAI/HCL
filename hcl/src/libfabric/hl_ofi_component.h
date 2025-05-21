#pragma once

#include <cstdint>           // for uint64_t
#include <cstring>           // for NULL, memset, size_t
#include <vector>            // for vector
#include <optional>          // for optional
#include <utility>           // for forward
#include <memory>            // for shared_ptr
#include "infra/fd.h"        // for FileDescriptor
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

#define RETRY_ON_EAGAIN(expr, max_duration, retry_expr)                                                                \
    ({                                                                                                                 \
        const auto     __start_time = std::chrono::steady_clock::now();                                                \
        decltype(expr) __result;                                                                                       \
        do                                                                                                             \
        {                                                                                                              \
            __result = (expr);                                                                                         \
            if (__result != -EAGAIN)                                                                                   \
            {                                                                                                          \
                LOG_HCL_DEBUG(HCL_OFI,                                                                                 \
                              "retry took {}",                                                                         \
                              std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - \
                                                                                    __start_time));                    \
                break;                                                                                                 \
            }                                                                                                          \
            retry_expr;                                                                                                \
        } while ((std::chrono::steady_clock::now() - __start_time) < (max_duration));                                  \
        __result;                                                                                                      \
    })

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
    bool     isInitialized = false;
    uint64_t tag;
    int      dev;
    bool     accepted;

    struct fid_ep* local_ep;
    struct fid_cq* cq;
    void*          mrDesc;
    fi_addr_t      local_ep_addr;
};

struct ofiComm_t
{
    bool           isInitialized = false;
    int            dev;
    uint64_t       tag;
    uint64_t       num_inflight_sends;
    uint64_t       num_inflight_recvs;
    fi_addr_t      remote_ep_addr;
    fi_addr_t      local_ep_addr;
    struct fid_ep* local_ep;
    struct fid_cq* cq;
    void*          mrDesc;
};

struct allConnectionComm_t
{
    listenComm_t listenComm;
    ofiComm_t    sendComm;
    ofiComm_t    recvComm;
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

int ofi_fi_close(fid_t domain);

template<typename T>
class FiObject final
{
public:
    FiObject(const T& fiObject) : m_fiObject(fiObject) {}

    ~FiObject()
    {
        if (m_fiObject)
        {
            ofi_fi_close(&m_fiObject->fid);
        }
    }

    FiObject(const FiObject&) = delete;

    FiObject& operator=(const FiObject&) = delete;

    FiObject(FiObject&& other) { *this = std::forward<FiObject<T>>(other); };

    FiObject& operator=(FiObject&& other)
    {
        m_fiObject       = other.m_fiObject;
        other.m_fiObject = nullptr;
        return *this;
    };

    const T& get() const { return m_fiObject; }
    operator T&() { return m_fiObject; }
    operator T() const { return m_fiObject; }

private:
    T m_fiObject;
};

template<typename T>
using FiObjectPtr = std::shared_ptr<FiObject<T>>;

enum class DomainType : uint8_t
{
    DATA = 0,  // General purpose for data transfer
    SINGLE,    // Single EP domain used for small sizes
    FLUSH      // Flush mechanism
};

struct MRParams
{
    std::optional<uint64_t> m_addr;
    std::optional<uint64_t> m_size;
    std::optional<int>      m_fd;
    std::optional<uint64_t> m_offset;
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
    ofi_component_t(int ofiDeviceId, int hw_module_id, struct fi_info* prov, int cpuid, enum fi_cq_format cq_format);
    virtual ~ofi_component_t() = default;

    int inc_refcnt() { return ++m_refcnt; }
    int dec_refcnt() { return --m_refcnt; }
    int get_refcnt() const { return m_refcnt; }

    virtual void*    get_cq_buf() = 0;
    virtual uint64_t next_tag() { return 0; }

    virtual int
    listen(uint64_t tag, void* handle, listenComm_t* listenComm, unsigned hostConnIdx, uint16_t qpSetIndex) = 0;
    virtual int
    connect(const void* handle, ofiComm_t* ofiComm, void* localAddr, unsigned hostConnIdx, uint16_t qpSetIndex) = 0;
    virtual int accept(listenComm_t* listenComm, ofiComm_t* ofiComm)                                            = 0;
    virtual int
    isend(ofiComm_t* ofiComm, void* data, size_t size, ofi_req_t** request, OfiCompCallbackParams& compParams) = 0;
    virtual int
    irecv(ofiComm_t* ofiComm, void* data, size_t size, ofi_req_t** request, OfiCompCallbackParams& compParams) = 0;

    int test(ofi_req_t* req, int* done, size_t* size);

    void initializeMemoryRegion(MRParams& params);
    int  getDmabufFd();

protected:
    int                ofi_progress(struct fid_cq* cq);
    int                ofi_flush_progress();
    virtual int        process_completions(void* cq_buf, uint64_t num_cqes) = 0;
    int                process_first_recv_completion(ofi_req_t* req);
    int                _flush(ofiComm_t* ofiComm, ofi_req_t& request);
    struct fid_domain* getDomainByType(DomainType domainType) const;
    static std::string getDomainNameByType(DomainType domainType);

protected:
    static FiObject<struct fid_fabric*> create_fabric(const struct fi_info* provider);
    static FiObject<struct fid_domain*> create_domain(struct fi_info* provider, struct fid_fabric* fabric);
    static FiObject<struct fid_mr*>
    create_mr(struct fid_domain* domain, void* data, size_t size, fi_hmem_iface fi_hmem_iface, int dmabuf_fd);
    static FiObject<struct fid_cq*> create_cq(struct fid_domain* domain, int cpuid, enum fi_cq_format format);
    static FiObject<struct fid_av*> create_av(struct fid_domain* domain);
    static FiObject<struct fid_ep*>
              create_ep(struct fi_info* provider, struct fid_domain* domain, struct fid_cq* cq, struct fid_av* av);
    fi_addr_t create_address(struct fid_ep* const ep, struct fid_av* const av);

protected:
    const int      m_ofiDeviceID;
    const int      m_cpuid;
    int            m_refcnt;
    const uint64_t m_cqe_burst;

protected:
    const std::chrono::seconds              m_eagainMaxRetryDuration;
    struct fi_info*                         m_prov;
    const FiObject<struct fid_fabric*>      m_fabric;
    const FiObject<struct fid_domain*>      m_domain;
    FileDescriptor                          m_dmabufFD;
    std::optional<FiObject<struct fid_mr*>> m_mr;

    const FiObject<struct fid_fabric*>      m_fabric_single;
    const FiObject<struct fid_domain*>      m_domain_single;
    std::optional<FiObject<struct fid_mr*>> m_mrSingle;

private:
    std::optional<struct fi_info* const>              m_flush_provider;
    const std::optional<FiObject<struct fid_fabric*>> m_flush_fabric;
    const std::optional<FiObject<struct fid_domain*>> m_flush_domain;
    const std::optional<FiObject<struct fid_cq*>>     m_flush_cq;
    const std::optional<FiObject<struct fid_av*>>     m_flush_av;
    const std::optional<FiObject<struct fid_ep*>>     m_flush_ep;
    const std::optional<fi_addr_t>                    m_flush_addr;
    std::optional<FiObject<struct fid_mr*>>           m_mrFlushLocal;
    std::optional<FiObject<struct fid_mr*>>           m_mrFlushRemote;
    uint32_t                                          m_flushLocalBuffer;
    uint64_t                                          m_flushRemoteBuffer;
};
