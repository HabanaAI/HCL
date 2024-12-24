#pragma once

#include "synapse_api_types.h"  // for synDeviceId
#include "hcl_api_types.h"

static constexpr synDeviceId SYN_VALID_DEVICE_ID = 0;

constexpr HCL_Comm HCL_INVALID_COMM = (HCL_Comm)(-1);
constexpr uint32_t HNIC_BUF_SIZE = 128;

constexpr uint32_t DEFAULT_BOX_SIZE      = 8;
constexpr uint32_t INVALID_SCALEUP_GROUP = (uint32_t)-1;
constexpr int32_t  INVALID_NIC           = -1;

constexpr uint32_t NUM_SCALEUP_PORTS_PER_CONNECTION = 3;
constexpr uint32_t DEFAULT_COMMUNICATORS_SIZE       = 16;  // Currently its acting as MAX comms (SW-123392)

constexpr uint32_t LONG_MON_DWORD_SIZE = 4;

constexpr uint64_t DEFAULT_SCALEOUT_PORTS_MASK = 0xc00100;

constexpr uint64_t DEFAULT_PRIM_COLLECTIVE_MASK = 0x0;

constexpr uint32_t MAX_SUPPORTED_RANKS = 8192;  // max value of GCFG_HCL_MAX_RANKS

constexpr uint32_t MAX_QPS_PER_CONNECTION_G2 = 4;
constexpr uint32_t MAX_QPS_PER_CONNECTION_G3 = 6;

// RankInfo constants and structures
constexpr uint32_t QPS_ARRAY_LENGTH            = std::max(MAX_QPS_PER_CONNECTION_G2, MAX_QPS_PER_CONNECTION_G3);
constexpr uint32_t MAX_QPS_SETS_PER_CONNECTION = 4;
constexpr uint32_t MAX_RANK_INFO_NICS          = 24;
constexpr uint32_t COMPACT_RANK_INFO_NICS      = 3;

// should be max(NUM_SCALEUP_PORTS_PER_CONNECTION, HLS3PCIE_NUM_SCALEUP_NICS_PER_DEVICE) but can't include platform
constexpr uint32_t MAX_COMPACT_RANK_INFO_NICS = 6;
constexpr uint32_t HOST_MICRO_ARCH_STREAMS    = 2;
constexpr uint32_t MAX_HNIC_CONNECTIONS       = HOST_MICRO_ARCH_STREAMS;
constexpr uint32_t MAX_HNIC_CONNECTION_SETS   = 16;  // Limited by qpSetIndex size (4 bits)

constexpr int SINGLE_QP_SET_INDEX = 0;
constexpr int SINGLE_QP_SET       = 1;
