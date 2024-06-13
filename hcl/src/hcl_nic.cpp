
#include "hcl_nic.h"

#include <memory>                       // for __shared_ptr_access
#include "hcl_types.h"                  // for sq_wqe, WQE_LINEAR, PKT_SIZE
#include "hcl_utils.h"                  // for VERIFY, LOG_HCL_DEBUG, LOG_H...
#include "hlthunk.h"                    // for hlthunk_nic_wq_arr_set_in
#include "interfaces/hcl_idevice.h"     // for IHclDevice
#include "drm/habanalabs_accel.h"       // for HL_NIC_USER_COLL_WQ_RECV
#include "hcl_log_manager.h"            // for LOG_TRACE, LOG_DEBUG

HclNic::HclNic(IHclDevice* device, int nic, uint32_t nQPN, uint32_t memType, uint32_t sendType, uint32_t recvType)
: IHclNic(device, nic), m_nQPN(nQPN), m_memType(memType), m_sendType(sendType), m_recvType(recvType)
{
}

void HclNic::_wq_arr_set_()
{
    LOG_HCL_TRACE(HCL, "hlthunk_nic_wq_arr_set for nic({}) nQPN({})", m_nic, m_nQPN);

    int                       rc = 0;
    hlthunk_nic_wq_arr_set_in nicIn = {};

    nicIn.port              = m_nic;
    nicIn.addr              = 0;
    nicIn.num_of_wqs        = m_nQPN;
    nicIn.num_of_wq_entries = m_device->getSenderWqeTableSize();
    nicIn.mem_id            = (hl_nic_mem_id)m_memType;

    hlthunk_nic_wq_arr_set_out nicOut;

    nicIn.type = m_sendType;

    rc = hlthunk_nic_wq_arr_set(m_device->getFd(), &nicIn, &nicOut);
    VERIFY(rc == 0, "{} set SEND WQ failed({})", __FUNCTION__, rc);

    // receive WQ
    nicIn.type = m_recvType;

    rc = hlthunk_nic_wq_arr_set(m_device->getFd(), &nicIn, &nicOut);
    VERIFY(rc == 0, "{} set RECV WQ failed({})", __FUNCTION__, rc);
}

void HclNic::init()
{
    if (m_device) _wq_arr_set_();
}

HclNic::~HclNic() noexcept(false)
{
    if (m_sendType != (unsigned)(-1))
    {
        int rc = hlthunk_nic_wq_arr_unset(m_device->getFd(), m_nic, m_sendType);
        VERIFY(rc == 0, "{} nic({}) unset SEND WQ failed({})", __FUNCTION__, m_nic, rc);
    }

    if (m_recvType != (unsigned)(-1))
    {
        int rc = hlthunk_nic_wq_arr_unset(m_device->getFd(), m_nic, m_recvType);
        VERIFY(rc == 0, "{} nic({}) unset RECV WQ failed({})", __FUNCTION__, m_nic, rc);
    }

    LOG_TRACE(HCL, "{} hlthunk_nic_wq_arr_unset", __FUNCTION__);
}
