
#include "gaudi3_nic.h"
#include "interfaces/hcl_idevice.h"
#include "ibverbs/hcl_ibverbs.h"

Gaudi3Nic::Gaudi3Nic(IHclDevice* device, int nic, uint32_t nQPN, uint32_t bp)
: Gen2ArchNic(device,
              nic,
              nQPN,
              device->isScaleOutPort(nic, SCALEOUT_SPOTLIGHT)
                  ? HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND
                  : HL_NIC_USER_COLL_WQ_SEND,  // Hybrid ports can be used as both SU and SO, we are using
                                               // SCALEOUT_SPOTLIGHT in order to allocate the maximum
              device->isScaleOutPort(nic, SCALEOUT_SPOTLIGHT)
                  ? HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV
                  : HL_NIC_USER_COLL_WQ_RECV,  // Hybrid ports can be used as both SU and SO, we are using
                                               // SCALEOUT_SPOTLIGHT in order to allocate the maximum
              bp)
{
    _get_app_params_();
}

void Gaudi3Nic::_get_app_params_()
{
    // since we have two nics sitting on the same nic macro, we need to add an offset to one of them
    // get nic app params so that we know what offest to add.
    hlthunk_nic_user_get_app_params_out app_params;

    // done once for all communicators
    int rc = hlthunk_nic_user_get_app_params(m_device->getFd(), m_nic, &app_params);
    VERIFY(rc == 0, "Application and/or port do not support advanced features. Error: {}", rc);

    nic2QpOffset = app_params.coll_qps_offset;
}

Gaudi3IBVNic::Gaudi3IBVNic(IHclDevice* device, uint32_t nic, uint32_t nQPN, bool scaleOut, uint32_t bp)
: Gen2ArchIBVNic(device, nic, nQPN, bp, scaleOut ? ntScaleOut : ntCollective)
{
};

void Gaudi3IBVNic::init()
{
    Gen2ArchIBVNic::init();
    nic2QpOffset = g_ibv.get_qp_offset(m_nic);
};