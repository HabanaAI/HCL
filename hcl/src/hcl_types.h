#pragma once

#include <cstdint>
#include <unistd.h>
#include <vector>
#include <cerrno>
#include <algorithm>
#include <string>
#include <list>
#include <iostream>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <set>
#include <unordered_set>
#include <sys/socket.h>

#include "synapse_api_types.h"  // for synDeviceId
#include "common/pci_ids.h"
#include "hlthunk.h"
#include "hcl_api_types.h"
#include "hcl_log_manager.h"
#include "hcl_defs.h"
#include "hcl_bits.h"

#include "hcl_inc.h"
#include "hcl_consts.h"

/**
 * @brief gaudi NIC MAC address
 */
union MacAddressType
{
    uint8_t  u8[8];
    uint64_t u64 = 0;
};

/**
 * @brief gaudi NIC IP and MAC
 */
struct GaudiNicAddress
{
    uint32_t       ip = 0;
    MacAddressType mac;
};

/**
 * @brief hold the address info of all rank NICs
 */
struct GaudiNicAddressArray
{
    GaudiNicAddress nics[MAX_RANK_INFO_NICS];
};

/**
 * @brief gaudi NIC QPs holds QP data for MAX_COMPACT_RANK_INFO_NICS active nics
 *
 * to access by nic use GaudiNicQPs[]
 * to access by index use GaudiNicQPs.qp[]
 */
struct GaudiNicQPs
{
    struct NicQPs
    {
        uint32_t qp[MAX_QPS_SETS_PER_CONNECTION][QPS_ARRAY_LENGTH];
        uint8_t  nic;
    } qp[MAX_COMPACT_RANK_INFO_NICS];

    NicQPs& operator[](uint8_t nic);
};

/**
 * @brief HCCL over host connection info
 */
struct HostNicConnOpaque
{
    char buff[HNIC_BUF_SIZE];
};

/**
 * @brief HCCL over host connection info
 */
struct HostNicConnectInfo
{
    HostNicConnOpaque server[MAX_HNIC_CONNECTION_SETS][MAX_HNIC_CONNECTIONS];
};

/**
 * @brief minimal Rank data required for coordinator 1'st handshake
 */
#define HOSTNAME_MAX_LENGTH 256
struct __attribute__((packed)) RankInfoHeader
{
    HCL_Rank hcclRank = 0;
    uint32_t boxSize;
    // device info
    uint32_t         hwModuleID;
    int              hostnameLength                = strlen("UNKNOWN");
    char             hostname[HOSTNAME_MAX_LENGTH] = "UNKNOWN";
    sockaddr_storage caddr                         = {0};    // address of coordinator (ip + port)
    uint64_t         apiCounter                    = 0;      // for migration
    bool             L3                            = false;  //  gnic configuration. false - L2(MAC), true - L3(IP)
    uint64_t         failedScaleOutPortsMask       = 0;
};

/**
 * @brief holds the data needed for remote ranks
 */
struct RemoteInfo
{
    GaudiNicQPs        gaudiNicQPs;  // QPs on active NICs
    HostNicConnectInfo hostNicConns;
    struct  // for migration
    {
        uint64_t sendCounter = 0;
        uint64_t recvCounter = 0;
    } sendRecvCounters;
};

/**
 * @brief holds device common fields for local and remote rank
 *        (RankInfo and RemoteDeviceConnectionInfo)
 *
 * COMM, NIC addresses
 */
struct DeviceInfo
{
    HCL_Comm             m_comm = HCL_INVALID_COMM;
    GaudiNicAddressArray gaudiNicAddresses;
};

/**
 * @brief hold local rank information, header and device
 */
struct LocalRankInfo
{
    RankInfoHeader header;
    DeviceInfo     device;
};

/**
 * @brief hold rank information, include connections for all communicator ranks
 */
struct RankInfo
{
    RankInfoHeader          header;
    DeviceInfo              device;
    std::vector<RemoteInfo> remoteInfo = {};
};

/**
 * @brief buffer for RankInfo serialization over network
 */
struct RankInfoBuffer
{
    LocalRankInfo localInfo;
    RemoteInfo    remoteInfo[];  // placeholder for RemoteInfo vector
};

/**

 * @brief hold remote device info, include connection to local rank
 */
struct RemoteDeviceConnectionInfo
{
    RankInfoHeader header;
    DeviceInfo     device;
    RemoteInfo     remoteInfo;  // remote connections to current rank
};

struct portMaskConfig
{
    uint64_t hwPortsMask;    /* 0-based */
    uint64_t hwExtPortsMask; /* 0-based */
};

using hcl_handle_list_t = std::list<HCL_Request>;

class IHclNic;
using spHclNic = std::shared_ptr<IHclNic>;

enum HclConfigType
{
    UNKNOWN     = 0,
    BACK_2_BACK = 1,
    LOOPBACK    = 2,
    RING        = 3,
    OCP1        = 4,
    HLS1        = 5,
    HLS1H       = 6,
    HLS2        = 7,
    HLS3        = 8,
    HL338       = 9
};

using HclRankAndCommSet = std::set<std::pair<HCL_Comm, HCL_Rank>>;

std::ostream& operator<<(std::ostream& os, const HCL_CollectiveOp& op);
HLLOG_DEFINE_OSTREAM_FORMATTER(HCL_CollectiveOp);

using RanksVector = std::vector<HCL_Rank>;

using HCL_StreamId = uint32_t;

using HCL_HwModuleId = uint32_t;

// a set of module id numbers that belong one of the nic macros sets
using DevicesSet = std::unordered_set<HCL_HwModuleId>;

using remote_devices_t       = std::vector<RemoteDeviceConnectionInfo>;
using remote_devices_array_t = std::vector<remote_devices_t>;
using ranks_headers_t        = std::vector<RankInfoHeader>;

namespace std
{
std::ostream& operator<<(std::ostream& os, const std::vector<uint32_t>& uint32Vec);
std::ostream& operator<<(std::ostream& os, const std::unordered_set<uint32_t>& uint32UnorderedSet);
}  // namespace std
HLLOG_DEFINE_OSTREAM_FORMATTER(std::vector<uint32_t>);
HLLOG_DEFINE_OSTREAM_FORMATTER(std::unordered_set<uint32_t>);
