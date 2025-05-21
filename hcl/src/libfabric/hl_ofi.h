#pragma once

#include <pthread.h>      // for pthread_mutex_t
#include <rdma/fabric.h>  // for FI_VERSION
#include <cstddef>        // for size_t
#include <map>            // for map
#include <string>         // for string
#include <vector>         // for vector

#include "hl_ofi_component.h"

#ifdef __GNUC__
#define OFI_LIKELY(x)   __builtin_expect((x), 1)
#define OFI_UNLIKELY(x) __builtin_expect((x), 0)
#else
#define OFI_LIKELY(x)   (x)
#define OFI_UNLIKELY(X) (x)
#endif

// define the minimum version HCL supports
#define HL_OFI_MAJOR_VERSION (1)
#define HL_OFI_MINOR_VERSION (16)
#define ofi_version          FI_VERSION(HL_OFI_MAJOR_VERSION, HL_OFI_MINOR_VERSION)

#define MAX_INFO    (15)
#define MAX_BDF_LEN (25)

/*
 * We have a limit of MAX_HANDLE_SIZE = 64 bytes. Therefore, we can only
 * support an endpoint name of maximum 56 bytes. We are using remaining
 * 8 bytes for tags.
 */
#define MAX_EP_ADDR (56)

/*
 * For each tag, we use MSB as control bit and remaining as identifiers.
 * We look at mem_tag_format for an endpoint to determine if provider
 * is reserving any MSBs.
 */
#define OFI_HIGHEST_TAG_BIT (0x1ul << 63)

/*
 * We are supporting min 2^32 tags per endpoint and reserving 1 bit for
 * marking control sends/recvs.
 */
#define MIN_TAG_BITS_FOR_ID (32 + 1)

/*
 * Twice the size of max inflight requests
 */
#define OFI_MAX_REQUESTS              256
#define MINIMAL_LIBFABRIC_VERSION     (FI_VERSION(1, 20))
#define UNSUPPORTED_LIBFABRIC_VERSION (FI_VERSION(2, 0))

#define REQUIRED_KERNEL_MAJOR 5
#define REQUIRED_KERNEL_MINOR 12

void get_hints(struct fi_info* hints, bool gaudi_direct);

struct PCIE_Device
{
    std::string addr_prefix;
    int         numa_node;
    std::string switch_addr_prefix;
    std::string full_path;
};

class ofi_component_t;
class ofi_t final
{
public:
    ofi_t(int device_fd, int hw_module_id);
    virtual ~ofi_t();

    int    init();
    int    nOFIDevices() const { return m_nOFIDevices; }
    size_t getOFIDevice() const { return m_ofi_device; }
    int    listen(int ofiDevice, void* handle, listenComm_t* listenComm, unsigned hostConnIdx, uint16_t qpSetIndex);
    int    connect(int         ofiDevice,
                   const void* handle,
                   ofiComm_t*  ofiComm,
                   void*       localAddr,
                   unsigned    hostConnIdx,
                   uint16_t    qpSetIndex);
    int    accept(listenComm_t* listenComm, ofiComm_t* ofiComm);
    int    isend(ofiComm_t* const       ofiComm,
                 void* const            data,
                 const size_t           size,
                 ofi_req_t** const      request,
                 OfiCompCallbackParams& compParams);
    int    irecv(ofiComm_t* const       ofiComm,
                 void* const            data,
                 const size_t           size,
                 ofi_req_t** const      request,
                 OfiCompCallbackParams& compParams);
    int    test(ofi_req_t* request, int* done, size_t* size);
    int    close(const ofiComm_t& ofiComm);
    int    close(const listenComm_t& listenComm);
    bool   is_initialized() const { return m_is_initialized; }
    ofi_component_t* getOfiComponent(int ofiDevice);
    void             releaseOfiComponent(int ofiDevice);

    static bool     isMRLocal() { return s_mrLocal; }
    static bool     isGaudiDirect() { return s_gaudiDirect; }
    static bool     isVerbs() { return s_verbs; }
    static bool     isFabricFlush() { return isGaudiDirect() && GCFG_HCL_FABRIC_FLUSH.value(); }
    struct fi_info* get_nic_info(int ofiDevice);

private:
    /**
     * @brief The order prioritizes the providers. Lower value is better.
     */
    enum class CORE_PROVIDER
    {
        VERBS = 1,
        TCP   = 2
    };

    int                                            acquireOfiComponent(int ofiDevice);
    int                                            initOfiComponent(int ofiDevice);
    int                                            get_ofi_provider(bool gaudi_direct);
    std::map<CORE_PROVIDER, std::vector<fi_info*>> map_by_core_provider(struct fi_info* providers);

    /**
     * @brief Signal whether a detected tcp provider should be excluded
     *
     * @param provider provider information
     * @param expected_mem_tag_format expected memory tag format
     * @param unique_interfaces distinct tcp interfaces vector
     * @return true if inspected provider should be eliminated;
     * @return false if inspected provider should be kept
     *
     */
    bool                    exclude_tcp_provider(const fi_info* const provider, const uint64_t expected_mem_tag_format);
    std::optional<fi_info*> get_tcp_provider(const std::vector<fi_info*>& providers);

    /**
     * @brief Signal whether a detected verbs provider should be excluded
     *
     * @param provider provider information
     * @param expected_mem_tag_format expected memory tag format
     * @return true if inspected provider should be eliminated;
     * @return false if inspected provider should be kept
     */
    bool exclude_verbs_provider(const fi_info* const provider, const uint64_t expected_mem_tag_format);
    std::optional<fi_info*> get_verb_provider(const std::vector<fi_info*>& providers);

    /**
     * @brief Check whether Linux kernel has dmabuf support by reading the kernel symbols file,
     * This is necessary since some customers won't use the official kernel version, supporting dmabuf (5.12), but
     * backport the required modifications instead.
     *
     * @return true if dmabuf is supported
     * @return false otherwise
     */
    bool                                checkDMABUFSupport();
    void                                log_provider(const std::vector<struct fi_info*>& providers,
                                                     const struct fi_info*               selectedProvider,
                                                     const std::string&                  description);
    static std::optional<CORE_PROVIDER> get_core_provider(const std::string& provider_name);

private:
    static bool s_mrLocal;
    static bool s_gaudiDirect;
    static bool s_verbs;

    const int                     m_device_fd;
    int                           m_hw_module_id;
    int                           m_nOFIDevices;
    size_t                        m_ofi_device;
    lock_t                        m_ofi_lock;
    bool                          m_is_initialized;
    std::vector<ofi_component_t*> m_components;
    struct fi_info*               m_fi_getinfo_result;
    std::vector<struct fi_info*>  m_providers;
    PCIE_Device                   m_gaudi_pci_dev;
};
