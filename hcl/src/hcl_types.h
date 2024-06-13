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

#include "synapse_types.h"
#include "common/pci_ids.h"
#include "hlthunk.h"
#include "hcl_api_types.h"
#include "interfaces/hcl_unique_sorted_vector.h"

#include "hcl_log_manager.h"
#include "hcl_defs.h"
#include "hcl_bits.h"

#define DISABLE_AVX_MODE                            _POWER_PC_

#define NO_DEVICE_ID ((synDeviceId)-1)
#define HCL_INVALID_COMM (HCL_Comm)(-1)

// align to size, size should be power of 2
// macro aligned to LKD implementation
#define ALIGN_UP(addr, size) (((addr) + (size) -1) & ~((size) -1))

const uint32_t DEFAULT_BOX_SIZE        = 8;
const HCL_Rank INVALID_RANK            = ((HCL_Rank)-1);
const uint64_t INVALID_POD             = INVALID_RANK;
const int32_t  INVALID_NIC             = (int32_t)-1;

const uint32_t NUM_SCALEUP_PORTS_PER_CONNECTION = 3;
const unsigned DEFAULT_COMMUNICATORS_SIZE = 16; // Currently its acting as MAX comms (SW-123392)

const uint32_t HCL_MAC_BYTE_SIZE = 6; // size of a MAC address in bytes

// Indicates that request was not created by HCL
const uint64_t HCL_REQUEST_DIGITAL_SIGNATURE = 0xDEADBABA;

const uint32_t HCL_FlagsMask = eHCLWeakOrder | eHCCLAPICall;

const uint64_t DEFAULT_SCALEOUT_PORTS_MASK = 0xc00100;

const uint32_t MAX_SUPPORTED_RANKS = 8192;  // max value of GCFG_HCL_MAX_RANKS

// RankInfo constants and structures
#define HOSTNAME_MAX_LENGTH 256
const int MAX_QPS_PER_CONNECTION = 6;
const int MAX_QPS_SETS_PER_CONNECTION = 4;
const int MAX_RANK_INFO_NICS     = 24;
const int COMPACT_RANK_INFO_NICS = 3;
const int HOST_MICRO_ARCH_STREAMS     = 2;
const int MAX_HNIC_CONNECTIONS        = HOST_MICRO_ARCH_STREAMS;

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
 * @brief gaudi NIC QPs holds QP data for 3 (COMPACT_RANK_INFO_NICS) active nics
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
    } qp[COMPACT_RANK_INFO_NICS];

    NicQPs& operator[](uint8_t nic);
};

/**
 * @brief HCCL over host connection info
 */
struct HostNicConnOpaque
{
    char buff[128]; /* TODO: replace with CTRL_BUF_SIZE Define in libfabric_common.h */
};

/**
 * @brief HCCL over host connection info
 */
struct HostNicConnectInfo
{
    HostNicConnOpaque server[MAX_HNIC_CONNECTIONS];
};

/**
 * @brief minimal Rank data required for coordinator 1'st handshake
 */
struct RankInfoHeader
{
    int hcclRank = 0;
    int boxSize;
    // device info
    uint32_t hwModuleID;
    int      hostnameLength                = strlen("UNKNOWN");
    char     hostname[HOSTNAME_MAX_LENGTH] = "UNKNOWN";
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
 * @brief initialize RemoteInfo.indexToNic map in loopback mode
 */
#define LOOPBACK_NIC_INDEX_INIT(index, rank) (index + rank * COMPACT_RANK_INFO_NICS)

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

using hcl_handle_list_t = std::list<HCL_Request>;

class IHclNic;
typedef std::shared_ptr<IHclNic>    spHclNic;

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
    HLS3PCIE    = 9
};

// The following enum is used to define dynamic ports scheme configuration per communicator
// The feature is disabled temporarily and only DEFAULT_SPOTLIGHT ports configuration is supported
enum e_spotlighPortsConfigurations
{
    DEFAULT_SPOTLIGHT  = 0,
    SCALEUP_SPOTLIGHT  = 0,
    SCALEOUT_SPOTLIGHT = 0,
    MAX_SPOTLIGHT      = 2
};

using HclRankAndCommSet = std::set<std::pair<HCL_Comm, HCL_Rank>>;

std::ostream& operator<<(std::ostream& os, const HCL_CollectiveOp& op);
using RanksVector = std::vector<HCL_Rank>;
typedef uint32_t HCL_StreamId;

typedef uint32_t HCL_HwModuleId;

std::ostream& operator<<(std::ostream& os, const std::set<HCL_HwModuleId>& hwModules);
