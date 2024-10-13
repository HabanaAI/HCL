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

#define DISABLE_AVX_MODE _POWER_PC_

static constexpr synDeviceId SYN_VALID_DEVICE_ID = 0;

#define HCL_INVALID_COMM (HCL_Comm)(-1)
#define HNIC_BUF_SIZE    (128)

// align to size, size should be power of 2
// macro aligned to LKD implementation
#define ALIGN_UP(addr, size) (((addr) + (size) - 1) & ~((size) - 1))

const uint32_t DEFAULT_BOX_SIZE      = 8;
const uint32_t INVALID_SCALEUP_GROUP = (uint32_t)-1;
const int32_t  INVALID_NIC           = -1;

constexpr uint32_t NUM_SCALEUP_PORTS_PER_CONNECTION = 3;
const unsigned     DEFAULT_COMMUNICATORS_SIZE       = 16;  // Currently its acting as MAX comms (SW-123392)

constexpr uint32_t LONG_MON_DWORD_SIZE = 4;

// for HLS3PCIE, should be move to hal_hls3pcie but required here because of GaudiNicsQPS data type
static constexpr unsigned HLS3PCIE_NUM_SCALEUP_PORTS_PER_CONNECTION = 6;

const uint32_t HCL_MAC_BYTE_SIZE = 6;  // size of a MAC address in bytes

// Indicates that request was not created by HCL
const uint64_t HCL_REQUEST_DIGITAL_SIGNATURE = 0xDEADBABA;

const uint32_t HCL_FlagsMask = eHCLWeakOrder | eHCCLAPICall;

const uint64_t DEFAULT_SCALEOUT_PORTS_MASK = 0xc00100;

const uint32_t MAX_SUPPORTED_RANKS = 8192;  // max value of GCFG_HCL_MAX_RANKS

// RankInfo constants and structures
#define HOSTNAME_MAX_LENGTH 256
const int          MAX_QPS_PER_CONNECTION      = 6;
const int          MAX_QPS_SETS_PER_CONNECTION = 4;
const int          MAX_RANK_INFO_NICS          = 24;
constexpr unsigned COMPACT_RANK_INFO_NICS      = 3;
constexpr unsigned MAX_COMPACT_RANK_INFO_NICS =
    std::max(NUM_SCALEUP_PORTS_PER_CONNECTION,
             HLS3PCIE_NUM_SCALEUP_PORTS_PER_CONNECTION);  // support up to 6 scaleup ports
const int HOST_MICRO_ARCH_STREAMS  = 2;
const int MAX_HNIC_CONNECTIONS     = HOST_MICRO_ARCH_STREAMS;
const int MAX_HNIC_CONNECTION_SETS = 16;  // Limited by qpSetIndex size (4 bits)

const int SINGLE_QP_SET_INDEX = 0;
const int SINGLE_QP_SET       = 1;

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
        uint32_t qp[MAX_QPS_SETS_PER_CONNECTION][MAX_QPS_PER_CONNECTION];
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
struct RankInfoHeader
{
    HCL_Rank hcclRank = 0;
    uint32_t boxSize;
    // device info
    uint32_t         hwModuleID;
    int              hostnameLength                = strlen("UNKNOWN");
    char             hostname[HOSTNAME_MAX_LENGTH] = "UNKNOWN";
    sockaddr_storage caddr                         = {0};  // address of coordinator (ip + port)
};

/**
 * @brief holds the data needed for remote ranks
 */
struct RemoteInfo
{
    GaudiNicQPs        gaudiNicQPs;  // QPs on active NICs
    HostNicConnectInfo hostNicConns;
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
typedef std::shared_ptr<IHclNic> spHclNic;

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
using RanksVector = std::vector<HCL_Rank>;
typedef uint32_t HCL_StreamId;

typedef uint32_t HCL_HwModuleId;

// a set of module id numbers that belong one of the nic macros sets
typedef std::unordered_set<HCL_HwModuleId> DevicesSet;

namespace std
{
std::ostream& operator<<(std::ostream& os, const DevicesSet& hwModules);
}

using remote_devices_t       = std::vector<RemoteDeviceConnectionInfo>;
using remote_devices_array_t = std::vector<remote_devices_t>;
using ranks_headers_t        = std::vector<RankInfoHeader>;
