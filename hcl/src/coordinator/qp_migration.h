#pragma once

#include "hcl_types.h"

struct NicState
{
    HCL_Rank rank  = HCL_INVALID_RANK;
    int32_t  nic   = INVALID_NIC;
    bool     state = false;
};

class IMigrationCallback
{
public:
    virtual void mcNicStateChange(const NicState& nicState) = 0;
};


// pseudo code for the callback
//
//void mcNicStateChange(const NicState& nicState)
//{
//
//  if (nicState.state == down)
//  {
//      create_migration_qps();
//      prepare_send_recv_buffers(send_b, recv_b); // API + send/recv counters + qps
//      hlcp_client->exchangeMigrationData(send_b, recv_b)
//      setup_counters_for_apis(recv_b);
//      setup_rtr_part(recv_b);
//      hlcp_client->commRendezvous();
//      setup_rts_part();
//  }
//
//  if (nicState.state == up)
//  {
//      prepare_send_recv_buffers(send_b, recv_b); // API + send/recv counters
//      ensure_local_port_is_up();
//      hlcp_client->exchangeMigrationData(send_b, recv_b)
//      setup_counters_for_apis(recv_b);
//
//  }
//}
