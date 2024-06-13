#pragma once

#include "hl_ofi_component.h"

class ofi_rdm_component_t : public ofi_component_t
{
public:
    ofi_rdm_component_t(int ofiDeviceId, int hw_module_id, struct fi_info* prov, int cpuid);
    virtual ~ofi_rdm_component_t();

    int   create_component() override;
    void* get_cq_buf() override;

    int next_tag(uint64_t* tag) override;

    int listen(uint64_t tag, void* handle, listenComm_t** listenComm) override;
    int connect(void* handle, ofiComm_t** ofiComm, void* localAddr) override;
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

private:
    int process_completions(void* cq_buf, uint64_t num_cqes) override;

    std::vector<fi_cq_tagged_entry> m_cqe_tagged_buffers;

    uint64_t m_tag;
    uint64_t m_max_tag;

    struct fid_ep* m_ep;
    struct fid_av* m_av;
    struct fid_ep* m_flush_ep = NULL;
    struct fid_av* m_flush_av = NULL;
};
