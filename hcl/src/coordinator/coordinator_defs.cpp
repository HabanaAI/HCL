/******************************************************************************
 * Copyright (C) 2022 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include <mutex>
#include "coordinator_defs.h"
#include "network_utils.h"
#include "hlcp_server.h"
std::mutex coord_create_mtx_;

HcclCoordinatorUPtr IHcclCoordinator::create(bool use_global_comm_ip)
{
    std::lock_guard<std::mutex> lock(coord_create_mtx_);
    int                         hccl_port;
    std::string                 ip;
    if (use_global_comm_ip)
    {
        ip        = get_global_comm_ip();
        hccl_port = get_global_comm_port();
        // check if address is on this host
        VERIFY(ip_is_local(ip), "provided ip({}) is not on local host", ip);
    }
    else
    {
        auto tcp_if = detect_tcp_if();
        ip          = tcp_if.ip_addr;
        hccl_port   = 0;
    }

    sockaddr_t ipaddr(ip, hccl_port);

    VERIFY(ipaddr.str() != "", "invalid global comm id specified. {} {}", ip, hccl_port);

    return HcclCoordinatorUPtr(new hlcp_server_t(ipaddr));
}
