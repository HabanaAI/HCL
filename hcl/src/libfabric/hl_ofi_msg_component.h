#pragma once

#include "hl_ofi_component.h"

class ofi_msg_component_t : public ofi_component_t
{
public:
    ofi_msg_component_t(int ofiDeviceId, int hw_module_id, struct fi_info* prov, int cpuid);
    virtual ~ofi_msg_component_t() = default;

    int   create_component() override;
    void* get_cq_buf() override;

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
    std::vector<fi_cq_msg_entry> m_cqe_msg_buffers;

    int process_completions(void* cq_buf, uint64_t num_cqes) override;

    int read_eq_event(struct fid_eq* eq, struct fi_eq_cm_entry* entry, uint32_t* event);
    int wait_for_connection(struct fid_eq* eq, struct fi_info** prov);
    int wait_until_connected(struct fid_ep* ep, struct fid_eq* eq);
};
