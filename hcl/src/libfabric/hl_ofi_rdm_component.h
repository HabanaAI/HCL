#pragma once

#include "hl_ofi_component.h"

class ofi_rdm_component_t : public ofi_component_t
{
private:
    enum class EndpointRole : uint8_t
    {
        LISTEN = 1,
        RECV   = LISTEN,
        CONNECT,
        SEND = CONNECT
    };

public:
    ofi_rdm_component_t(int ofiDeviceId, int hw_module_id, struct fi_info* prov, int cpuid);
    virtual ~ofi_rdm_component_t() = default;

    void* get_cq_buf() override;

    int next_tag(uint64_t* tag) override;

    int
    listen(uint64_t tag, void* handle, listenComm_t** listenComm, unsigned hostConnIdx, uint16_t qpSetIndex) override;
    int connect(const void* handle,
                ofiComm_t** ofiComm,
                void*       localAddr,
                unsigned    hostConnIdx,
                uint16_t    qpSetIndex) override;
    int accept(listenComm_t* listenComm, ofiComm_t** ofiComm) override;
    int isend(ofiComm_t*             ofiComm,
              void*                  data,
              size_t                 size,
              fid_mr*                mHandle,
              ofi_req_t**            request,
              OfiCompCallbackParams& compParams) override;
    int irecv(ofiComm_t*             ofiComm,
              void*                  data,
              size_t                 size,
              fid_mr*                mHandle,
              ofi_req_t**            request,
              OfiCompCallbackParams& compParams) override;
    int close(ofiComm_t* ofiComm) override;
    int close(listenComm_t* listenComm) override;

    using EpAv = std::tuple<FiObjectPtr<struct fid_ep*>, FiObjectPtr<struct fid_av*>>;

    /**
     * @brief Retrieve an existing endpoint associated with the given parameters or create if needed.
     *
     * @param hostConnIdx By which ustream the endpoint will be used
     * @param role In which role will be the endpoint used - LISTEN/CONNECT corresponding to RECV/SEND.
     * @param qpSetIndex The qp set index of the endpoint
     * @return endpoint and its address vector.
     */
    EpAv acquire_ep_av(unsigned hostConnIdx, EndpointRole role, uint16_t qpSetIndex);

private:
    int             process_completions(void* cq_buf, uint64_t num_cqes) override;
    static uint64_t calculate_max_tag(const struct fi_info* const provider);
    /**
     * @brief Check whether the required parameters and existing parameters utilize different QPs.
     *
     * @return True if QPs are different and false otherwise.
     */
    static bool isDifferentQP(const unsigned int                      requestedHostConnIdx,
                              const ofi_rdm_component_t::EndpointRole requestedRole,
                              const uint16_t                          requestedQpSetIndex,
                              const unsigned int                      existingHostConnIdx,
                              const ofi_rdm_component_t::EndpointRole existingRole,
                              const uint16_t                          existingQpSetIndex);

private:
    std::vector<fi_cq_tagged_entry> m_cqe_tagged_buffers;

    uint64_t       m_tag;
    const uint64_t m_max_tag;

    std::map<std::tuple<unsigned /*hostConnIdx*/, EndpointRole, uint16_t /*qpSetIndex*/>, EpAv> m_eps;
    using Addr = std::vector<uint8_t>;
    std::map<FiObjectPtr<struct fid_av*>, std::map<Addr, fi_addr_t>> m_av_addr;
};
