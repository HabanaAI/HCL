#pragma once

#include "hcl_types.h"
#include "infiniband/verbs.h"
#include "hcl_ibv_loader.h"

void mac_to_gid(uint64_t mac, ibv_gid& gid);
void ip4addr_to_gid(uint32_t ipv4_addr, ibv_gid& gid);
ibv_mtu to_ibdev_mtu(int mtu);

ibv_gid handle_gid(const std::string& path);
ibv_gid_type_sysfs handle_gid_type(const std::string& path);