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

struct NicQPs
{
    uint32_t qp[MAX_QPS_PER_CONNECTION_G3][QPS_ARRAY_LENGTH];
    int8_t   nic;
};

/**
 * @brief gaudi NIC QPs holds QP data for MAX_COMPACT_RANK_INFO_NICS active nics
 *
 * to access by nic use GaudiNicQPs[]
 * to access by index use GaudiNicQPs.qp[]
 */
struct GaudiNicQPs
{
    NicQPs  qp[MAX_COMPACT_RANK_INFO_NICS];
    NicQPs& operator[](uint8_t nic);
};

/**
 * @brief backup gaudi NIC QPs holds QP data of valid (collective) nic's QPs
 * in case migration QPs are assigned on this nic
 *
 * to access by nic use GaudiNicQPs[]
 * to access by index use GaudiNicQPs.qp[]
 */
struct BackupGaudiNicQPs
{
    BackupGaudiNicQPs();
    NicQPs  qp[MAX_COMPACT_RANK_BACKUP_NICS];
    NicQPs& operator[](int8_t nic);
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

struct __attribute__((packed)) FtSyncCountersInfoHeader
{
    HCL_Rank hcclRank           = 0;  // (Client -> Server)
    uint64_t collectivesCounter = 0;  // collectives counter (Client -> Server, Server -> Client)
    uint64_t myCountersVersion =
        0;  // Client -> Server only: Increases every time faultToleranceApiCountersReached is set to true
    bool myCountersReached =
        false;  // Client -> Server only: Sends true in FT when this rank API counters reached their target in FT stage
                // FTwaitMaxCounters. Sends false when coordinator sends a new max counters value
    uint8_t __padding__[3] = {};
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
        uint64_t send = 0;
        uint64_t recv = 0;
    } counters;
};

struct FtSyncCountersRemoteInfo
{
    struct
    {
        uint64_t send = 0;
        uint64_t recv = 0;
    } counters;
};

/**
 * @brief holds device common fields for local and remote rank
 *        (RankInfo and RemoteDeviceConnectionInfo)
 *
 * NIC addresses. ports
 */
struct DeviceInfo
{
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
 * @brief buffer for RankInfo serialization over network
 */
struct FtRanksInfoBuffer
{
    FtSyncCountersInfoHeader localInfo;
    FtSyncCountersRemoteInfo remoteInfo[];  // placeholder for FtSyncCountersRemoteInfo vector
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

struct RemoteDeviceSyncCountersInfo
{
    FtSyncCountersInfoHeader header;
    FtSyncCountersRemoteInfo remoteInfo;  // remote s/r counters to current rank
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
    HL338       = 9,
    HL288       = 10,
    HL3_RACK    = 11,
};

using HclRankAndCommSet = std::set<std::pair<HCL_Comm, HCL_Rank>>;
using CommsSet          = std::set<HCL_Comm>;

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
using box_devices_t          = std::array<int, MAX_MODULES_IDS_PER_SERVER>;
using remote_counters_ranks_t =
    std::vector<RemoteDeviceSyncCountersInfo>;  // Each ranks sends/recvs this data - header + s/r from all ranks
using remote_devices_counters_cache_t = std::vector<remote_counters_ranks_t>;  // cache for all ranks in the comm

namespace std
{
std::ostream& operator<<(std::ostream& os, const std::vector<uint32_t>& uint32Vec);
std::ostream& operator<<(std::ostream& os, const std::unordered_set<uint32_t>& uint32UnorderedSet);
}  // namespace std
HLLOG_DEFINE_OSTREAM_FORMATTER(std::vector<uint32_t>);
HLLOG_DEFINE_OSTREAM_FORMATTER(std::unordered_set<uint32_t>);

using uint128_t = __uint128_t;

enum class NicLkdEventsEnum
{
    NIC_LKD_EVENTS_UP = 0,
    NIC_LKD_EVENTS_DOWN,
    NIC_LKD_EVENTS_SHUTDOWN
};

enum class eIbvNicPhysicalState
//
// Based on internal values in rdma-core/libibmad/iba_types.h
// Possible values
// #define IB_PORT_PHYS_STATE_NO_CHANGE 0
// #define IB_PORT_PHYS_STATE_SLEEP 1
// #define IB_PORT_PHYS_STATE_POLLING 2
// #define IB_PORT_PHYS_STATE_DISABLED 3
// #define IB_PORT_PHYS_STATE_PORTCONFTRAIN 4
// #define IB_PORT_PHYS_STATE_LINKUP 5
// #define IB_PORT_PHYS_STATE_LINKERRRECOVER 6
// #define IB_PORT_PHYS_STATE_PHYTEST 7
{
    Undefined = 0,  // All other states
    Shutdown  = 1,  // NIC shutdown
    LinkUp    = 2   // Link is up
};

using lock_t   = std::mutex;
using locker_t = std::unique_lock<lock_t>;
