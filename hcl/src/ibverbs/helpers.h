#pragma once

#include "hcl_types.h"
#include "infiniband/verbs.h"
#include "hcl_ibv_loader.h"

void    mac_to_gid(uint64_t mac, ibv_gid& gid);
void    ip4addr_to_gid(uint32_t ipv4_addr, ibv_gid& gid);
ibv_mtu to_ibdev_mtu(int mtu);

ibv_gid            handle_gid(const std::string& path);
ibv_gid_type_sysfs handle_gid_type(const std::string& path);

#define LOG_IBV(...) LOG_HCL_TRACE(HCL_IBV, ##__VA_ARGS__)
#define INF_IBV(...) LOG_HCL_INFO(HCL_IBV, ##__VA_ARGS__)
#define ERR_IBV(...) LOG_HCL_ERR(HCL_IBV, ##__VA_ARGS__)
#define WRN_IBV(...) LOG_HCL_WARN(HCL_IBV, ##__VA_ARGS__)
#define CRT_IBV(...) LOG_HCL_CRITICAL(HCL_IBV, ##__VA_ARGS__)
