#include "hcl_global_conf.h"

#include "hcl_types.h"             // for BACK_2_BACK, UNKNOWN, DEFAULT_BOX...
#include "synapse_common_types.h"  // for synDeviceType

using hl_gcfg::deviceValue;
using hl_gcfg::DfltBool;
using hl_gcfg::DfltFloat;
using hl_gcfg::DfltInt64;
using hl_gcfg::DfltSize;
using hl_gcfg::DfltString;
using hl_gcfg::DfltUint64;

using hl_gcfg::MakePrivate;
using hl_gcfg::MakePublic;

using GlobalConfObserver = hl_gcfg::GcfgItemObserver;

// clang-format off

GlobalConfBool GCFG_USE_CPU_AFFINITY(
        "USE_CPU_AFFINITY",
        "Use CPU affinity for priority threads",
        false,
        MakePublic);

GlobalConfUint64 GCFG_HCL_MIN_IMB_SIZE_FACTOR(
        "HCL_MIN_IMB_SIZE_FACTOR",
        "minimum size used for slicing - cant have slice count smaller than this - "
        "expressed as a factor of IMB size -> min size = size / factor",
        2,
        MakePrivate
);

GlobalConfUint64 GCFG_HCL_SCALEOUT_BUFFER_FACTOR(
        "HCL_SCALEOUT_BUFFER_FACTOR",
        "The granularity of a buffer from SCALEOUT_POOL (must be > 1)",
        8,
        MakePrivate);

GlobalConfSize GCFG_HCL_IMB_SIZE(
        "HCL_IMB_SIZE",
        "Static intermediate buffer size",
        DfltSize(hl_gcfg::SizeParam("512KB")),
        MakePrivate);

GlobalConfUint64 GCFG_HCL_SCALEUP_SIMB_COUNT(
        "HCL_SCALEUP_SIMB_COUNT",
        "number of static intermediate buffers",
        DfltUint64(20)  << deviceValue(synDeviceGaudi2, (13)),
        MakePrivate);

GlobalConfBool GCFG_HCL_OPT_SLICE_SIZE(
        "HCL_OPT_SLICE_SIZE",
        "Optimize slicing size",
        true,
        MakePrivate);

GlobalConfSize GCFG_HCL_SLICE_SIZE(
        "HCL_SLICE_SIZE",
        "Slicing size ",
        DfltSize(hl_gcfg::SizeParam("512KB")),
        MakePrivate);

GlobalConfSize GCFG_HCL_GDR_SLICE_SIZE(
        "HCL_GDR_SLICE_SIZE",
        "Slicing size in Gaudi-direct case",
        DfltSize(hl_gcfg::SizeParam("1MB")),
        MakePrivate);

GlobalConfSize GCFG_FW_IMB_SIZE(
        "FW_IMB_SIZE",
        "FW static intermediate buffer size for Gen2Arch only (G2 on SRAM, G3 on HBM)",
        DfltSize(hl_gcfg::SizeParam("512KB")) << deviceValue(synDeviceGaudi3, hl_gcfg::SizeParam("1280KB")),
        MakePrivate);

GlobalConfUint64 GCFG_HCL_DEBUG_STATS_LEVEL(
        "HCL_DEBUG_STATS_LEVEL",
        "Debug statistics level",
        0,
        MakePublic);

GlobalConfString GCFG_HCL_DEBUG_STATS_FILE(
        "HCL_DEBUG_STATS_FILE",
        "Debug statistics file name",
        std::string(),
        MakePublic);

GlobalConfString GCFG_HABANA_PROFILE(
        "HABANA_PROFILE",
        "Enable Habana Profiler",
        std::string("0"),
        MakePublic);

GlobalConfInt64 GCFG_REQUESTER_PRIORITY(
    "REQUESTER_PRIORITY",
    "Priority of requester QP packets",
    3,
    MakePrivate);

GlobalConfInt64 GCFG_RESPONDER_PRIORITY(
    "RESPONDER_PRIORITY",
    "Priority of responder QP packets",
    2,
    MakePrivate);

GlobalConfInt64 GCFG_CONGESTION_WINDOW(
    "CONGESTION_WINDOW",
    "Size of QP context congestion window",
    DfltInt64(24) << deviceValue(synDeviceGaudi3,  32),
    MakePrivate);

GlobalConfUint64 GCFG_CONGESTION_CONTROL_ENABLE(
    "CONGESTION_CONTROL_ENABLE",
    "Enable congestion control for BBR/SWIFT",
    DfltUint64(0),
    MakePrivate);

GlobalConfString GCFG_HCL_MAC_INFO_FILE(
    "HCL_MAC_INFO_FILE",
    "Path to MAC Addr Info file",
    std::string("/etc/habanalabs/macAddrInfo.json"),
    MakePublic);

GlobalConfString GCFG_HCL_GAUDINET_CONFIG_FILE(
    "HCL_GAUDINET_CONFIG_FILE",
    "Gaudi NIC subnet info file",
    std::string("/etc/habanalabs/gaudinet.json"),
    MakePublic);

GlobalConfBool GCFG_HCL_ALIVE_ON_FAILURE(
    "HCL_ALIVE_ON_FAILURE",
    "Do not terminate the program when VERIFY condition fails",
    false,
    MakePrivate);

GlobalConfUint64 GCFG_BOX_TYPE_ID(
    "BOX_TYPE_ID",
    "Box type ID",
    UNKNOWN,
    MakePrivate);

static const std::map<std::string, std::string> boxTypeStrToId = {{"BACK_2_BACK", std::to_string(BACK_2_BACK)},
                                                           {"LOOPBACK", std::to_string(LOOPBACK)},
                                                           {"RING", std::to_string(RING)},
                                                           {"HLS1", std::to_string(HLS1)},
                                                           {"OCP1", std::to_string(OCP1)},
                                                           {"HLS1-H", std::to_string(HLS1H)},
                                                           {"HLS2", std::to_string(HLS2)},
                                                           {"HL288", std::to_string(HL288)},
                                                           {"HLS3", std::to_string(HLS3)},
                                                           {"HL338", std::to_string(HL338)},
                                                           {"HL3_RACK", std::to_string(HL3_RACK)},
                                                           {"SWITCH", std::to_string(BACK_2_BACK)},
                                                           {"UNKNOWN", std::to_string(UNKNOWN)}};

GlobalConfObserver boxTypeObserver(&GCFG_BOX_TYPE_ID, boxTypeStrToId);

std::vector<GlobalConfObserver*> boxObservers = {&boxTypeObserver};

GlobalConfString GCFG_BOX_TYPE(
    "BOX_TYPE",
    "Select box type: UNKNOWN, BACK_2_BACK, LOOPBACK, RING, HLS1, OCP1, HLS1-H, HLS2, HL288, HLS3, HL338, HL3_RACK",
    std::string("UNKNOWN"),
    MakePublic,
    boxObservers);

GlobalConfBool GCFG_WEAK_ORDER(
    "HABANA_WEAK_ORDER",
    "When true, enable weak order between networking and compute operations",
    false,
    MakePublic);

GlobalConfBool GCFG_NOTIFY_ON_CCB_HALF_FULL_FOR_DBM(
        "NOTIFY_ON_CCB_HALF_FULL_FOR_DBM",
        "Device bench mark: turn on CCB back pressure signaling",
        false,
        MakePrivate);

GlobalConfBool GCFG_ENABLE_DEPENDENCY_CHECKER(
    "ENABLE_DEPENDENCY_CHECKER",
    "When true, enable dependency check and when false work in strong order",
    true,
    MakePrivate);

GlobalConfUint64 GCFG_LOOPBACK_COMMUNICATOR_SIZE(
        "LOOPBACK_COMMUNICATOR_SIZE",
        "For loopback tests only - determines the communicator size (Min: 2, Max: 8)",
        8,
        MakePublic);

GlobalConfUint64 GCFG_LOOPBACK_SCALEUP_GROUP_SIZE(
        "LOOPBACK_SCALEUP_GROUP_SIZE",
        "For loopback tests only - determines the scaleup size (Min: 1, Max: 8)",
        8,
        MakePublic);

GlobalConfString GCFG_LOOPBACK_DISABLED_NICS(
        "LOOPBACK_DISABLED_NICS",
        "For loopback tests only - determines the NICS that should be disabled. The scale out NICS must always be disabled",
        std::string("8,22,23"),
        MakePublic);

GlobalConfBool GCFG_HCCL_OVER_OFI(
        "HCCL_OVER_OFI",
        "When true, use OFI as communicators network layer",
        false,
        MakePublic);

GlobalConfBool GCFG_HCCL_GAUDI_DIRECT(
        "HCCL_GAUDI_DIRECT",
        {"HCCL_PEER_DIRECT"},
        "When true, use gaudi-direct for cross-ScaleupGroup communication",
        false,
        MakePublic);

GlobalConfBool GCFG_HCL_FABRIC_FLUSH(
        "HCL_FABRIC_FLUSH",
        "When true, perform flush via fi_read",
        true,
        MakePrivate);

GlobalConfBool GCFG_HCL_HNIC_IPV6(
        "HCL_HNIC_IPV6",
        "When true, enforce IPv6 communication",
        false,
        MakePrivate);

GlobalConfBool GCFG_HCL_HNIC_LTU(
        "HCL_HNIC_LTU",
        "When true, use ltu for RS scaleup buffers in hnic flow",
        true,
        MakePrivate);

GlobalConfBool GCFG_HCCL_ASYNC_EXCHANGE(
        "HCCL_ASYNC_EXCHANGE",
        "When true, use async send/recv for exchange between peers",
        true,
        MakePrivate);

GlobalConfString GCFG_HCCL_SOCKET_IFNAME(
        "HCCL_SOCKET_IFNAME",
        "network i/f name, e.g. eth0",
        std::string(),
        MakePublic);

GlobalConfString GCFG_HCCL_COMM_ID(
        "HCCL_COMM_ID",
        "the IP:port string to create the communicator (e.g. 127.0.0.1:5656)",
        std::string(),
        MakePublic);

GlobalConfInt64 GCFG_HCCL_TRIALS(
        "HCCL_TRIALS",
        "number of trials (1 per second) to attempt connect from client on communicator creation",
        120,
        MakePrivate);

GlobalConfBool GCFG_HCL_RS_SO_RECV_CONT_REDUCTION(
        "HCL_RS_SO_RECV_CONT_REDUCTION",
        "When true, enable Scaleout Recv Continuous Reduction flow",
        false,
        MakePrivate);

GlobalConfSize GCFG_HCL_COMPLEX_BCAST_MIN_SIZE(
        "HCL_COMPLEX_BCAST_MIN_SIZE",
        "Threshold to enable new complex broadcast",
        DfltSize(hl_gcfg::SizeParam("256K")),
        MakePrivate);

GlobalConfBool GCFG_HCL_USE_SINGLE_PEER_BROADCAST(
        "HCL_USE_SINGLE_PEER_BROADCAST",
        "Use single peer broadcast implementation. Not supported for Gaudi3",
        DfltBool(false) << deviceValue(synDeviceGaudi2, true),
        MakePrivate);

GlobalConfBool GCFG_HCL_IS_SINGLE_PEER_BROADCAST_ALLOWED(
        "HCL_IS_SINGLE_PEER_BROADCAST_ALLOWED",
        "Is single peer broadcast allowed",
        DfltBool(false) << deviceValue(synDeviceGaudi2, true),
        MakePrivate);

GlobalConfBool GCFG_HCL_LOG_CONTEXT(
        "HCL_LOG_CONTEXT",
        "Indent in context log lines for easier debug",
        true,
        MakePublic);

GlobalConfUint64 GCFG_HCL_LONGTERM_GPSO_COUNT(
        "HCL_LONGTERM_GPSO_COUNT",
        "Amount of longterm GPSO to reserve from the GPSO pool(s)",
        64,
        MakePrivate);

GlobalConfInt64 GCFG_HOST_SCHEDULER_SLEEP_THRESHOLD(
        "HOST_SCHEDULER_SLEEP_THRESHOLD",
        "Number of iterations on empty host streams before going to sleep",
        5000000,
        MakePrivate);

GlobalConfInt64 GCFG_HOST_SCHEDULER_SLEEP_DURATION(
        "HOST_SCHEDULER_SLEEP__DURATION",
        "Max sleep duration in ms for host scheduler",
        100,
        MakePrivate);

GlobalConfInt64 GCFG_HOST_SCHEDULER_THREADS(
        "HOST_SCHEDULER_THREADS",
        "Number of Host Scheduler threads that will process Host Streams",
        1,
        MakePrivate);

GlobalConfInt64 GCFG_HOST_SCHEDULER_STREAM_DEPTH_PROC(
        "HOST_SCHEDULER_STREAM_DEPTH_PROC",
        "Number of consecutive commands to processes per Host Stream",
        1,
        MakePrivate);

GlobalConfSize GCFG_MTU_SIZE(
        "MTU_SIZE",
        "MTU used by Gaudi NICs",
        hl_gcfg::SizeParam("8K"),
        MakePrivate);

// Must be aligned with synapse GCFG_SRAM_SIZE_RESERVED_FOR_HCL
// In Gaudi3 HBM is used for reduction, so SRAM size is set to 0
GlobalConfSize GCFG_HCL_SRAM_SIZE_RESERVED_FOR_HCL(
    "HCL_SRAM_SIZE",
    "The size of SRAM reserved for HCL reduction use",
    DfltSize(hl_gcfg::SizeParam("512KB")) << deviceValue(synDeviceGaudi3, hl_gcfg::SizeParam("0")),
    MakePrivate);

GlobalConfString GCFG_HCL_RDMA_DEFAULT_PATH(
        "HCL_RDMA_DEFAULT_PATH",
        "Path to libhbl.so",
        std::string("/opt/habanalabs/rdma-core/src/build/lib"),
        MakePrivate);

GlobalConfBool GCFG_HCL_IBV_GID_SYSFS(
        "HCL_IBV_GID_SYSFS",
        "use sysfs for GID data (instead of verbs API)",
        true,
        MakePrivate);

GlobalConfBool GCFG_HCL_USE_NIC_COMPRESSION(
        "HCL_USE_NIC_COMPRESSION",
        "use NIC compression",
        true,
        MakePrivate);

GlobalConfBool GCFG_HCL_FAIL_ON_CHECK_SIGNALS(
        "HCL_FAIL_ON_CHECK_SIGNALS",
        "At end of Gen2 device release, check if signals registers are clean, fail if any is not",
        false,
        MakePublic);

GlobalConfBool GCFG_HCL_ALLOW_GRAPH_CACHING(
        "HCL_ALLOW_GRAPH_CACHING",
        "Graph caching optimization is activated",
        true,
        MakePrivate);

GlobalConfBool GCFG_HCL_NULL_SUBMIT(
    "HCL_NULL_SUBMIT",
    "Null submit for HCL (gaudi2/3)",
    false,
    MakePrivate);

GlobalConfUint64 GCFG_HCCL_PRIM_COLLECTIVE_MASK(
        "HCCL_PRIM_COLLECTIVE_MASK",
        "Bitmask to enable using primitive implementation of collectives (bitshift based on HCL_CollectiveOp)",
        DfltUint64(DEFAULT_PRIM_COLLECTIVE_MASK) ,
        MakePublic);

GlobalConfBool GCFG_HCL_COLLECTIVE_LOG(
        "HCL_COLLECTIVE_LOG",
        "Collect HCL collective logs from all ranks to coordinator",
        false,
        MakePublic);

/**
 * @brief single collective call drift threshold between all communicator ranks
 * when threshold expires, a WARN is issued by COORD logger
 */
GlobalConfInt64  GCFG_OP_DRIFT_THRESHOLD_MS(
        "OP_DRIFT_THRESHOLD_MS",
        "HCL collective logs drift between ranks threshold in milliseconds",
        10000,
        MakePublic);

GlobalConfUint64 GCFG_SCALE_OUT_PORTS_MASK(
        "SCALE_OUT_PORTS_MASK",
        "Port mask to enable / disable scaleout ports (e.g. 0xc00000)",
        DfltUint64(DEFAULT_G3_SCALEOUT_PORTS_MASK) << deviceValue(synDeviceGaudi2,  DEFAULT_SCALEOUT_PORTS_MASK),
        MakePublic);

GlobalConfUint64 GCFG_LOGICAL_SCALE_OUT_PORTS_MASK(
        "LOGICAL_SCALE_OUT_PORTS_MASK",
        "Logical ports mask to enable / disable scaleout ports (e.g. 0x7FFFFF)",
        DfltUint64(0xFFFFFF),
        MakePublic);

GlobalConfString GCFG_HCL_PORT_MAPPING_CONFIG(
    "HCL_PORT_MAPPING_CONFIG",
    "Path to a JSON port mapping config file",
    std::string(),
    MakePublic);

/**
 * @brief max number of ranks
 *        current limit is 1k for gaudi, 8k for gaudi2/3
 *        set value larger than 8k will result invalid argument error on hcclCommInitRank
 */
GlobalConfUint64 GCFG_HCL_MAX_RANKS(
    "HCL_MAX_RANKS",
    "MAX number of supported ranks in a communicator",
    DfltUint64(8192),
    MakePrivate);

GlobalConfUint64 GCFG_HOST_SCHEDULER_OFI_DELAY_MSG_THRESHOLD(
    "HOST_SCHEDULER_OFI_DELAY_MSG_THRESHOLD",
    "OFI delayed processing threshold (msec) to report",
    DfltUint64(1000),
    MakePrivate);

GlobalConfUint64 GCFG_HOST_SCHEDULER_OFI_DELAY_ACK_THRESHOLD(
    "HOST_SCHEDULER_OFI_DELAY_ACK_THRESHOLD",
    "OFI delayed ack threshold (msec) to report",
    DfltUint64(10000),
    MakePrivate);

GlobalConfUint64 GCFG_HOST_SCHEDULER_OFI_DELAY_ACK_THRESHOLD_LOG_INTERVAL(
    "HOST_SCHEDULER_OFI_DELAY_ACK_THRESHOLD_LOG_INTERVAL",
    "OFI delayed ack logging threshold (msec), protects against log flooding",
    DfltUint64(10000),
    MakePrivate);

GlobalConfUint64 GCFG_HCL_SUBMIT_THRESHOLD(
    "HCL_SUBMIT_THRESHOLD",
    "HW submit threshold",
    DfltUint64(1),
    MakePrivate);

GlobalConfUint64 GCFG_MAX_QP_PER_EXTERNAL_NIC(
    "MAX_QP_PER_EXTERNAL_NIC",
    "Maximum number of QPs per external NIC",
    DfltUint64(4096) << deviceValue(synDeviceGaudi3, 8192), // driver limitation of 8K QPs per nic
    MakePrivate);

GlobalConfUint64 GCFG_HCL_GNIC_SCALE_OUT_QP_SETS(
    "HCL_GNIC_SCALE_OUT_QP_SETS",
    "Number of GNIC Scale-out QP sets per connection with rank",
    DfltUint64(4),
    MakePrivate);

GlobalConfUint64 GCFG_HCL_HNIC_SCALE_OUT_QP_SETS(
    "HCL_HNIC_SCALE_OUT_QP_SETS",
    "Number of HNIC Scale-out QP sets per connection with rank",
    DfltUint64(4),
    MakePrivate);

GlobalConfUint64 GCFG_HCL_GNIC_QP_SETS_COMM_SIZE_THRESHOLD(
    "HCL_GNIC_QP_SETS_COMM_SIZE_THRESHOLD",
    "Size of World Communicator from which, each GNIC connection gets only single QP set",
    DfltUint64(1601) << deviceValue(synDeviceGaudi3, 2049),
    MakePrivate);

GlobalConfUint64 GCFG_HCL_HNIC_QP_SETS_COMM_SIZE_THRESHOLD(
    "HCL_HNIC_QP_SETS_COMM_SIZE_THRESHOLD",
    "Size of World Communicator from which, each HNIC connection gets only single QP set",
    DfltUint64(2000),
    MakePrivate);

GlobalConfSize GCFG_HCL_HNIC_QP_SPRAY_THRESHOLD(
    "HCL_HNIC_QP_SPRAY_THRESHOLD",
    "Threshold of transaction size from which HNIC QP packet spray is enabled",
    DfltSize(hl_gcfg::SizeParam("512kb")),
    MakePrivate);

GlobalConfBool GCFG_HCL_ENABLE_G3_SR_AGG(
        "HCL_ENABLE_G3_SR_AGG",
        "For G3 send/receive, enable NIC commands aggregation",
        true,
        MakePrivate);

GlobalConfBool GCFG_ENABLE_HNIC_MICRO_STREAMS(
    "ENABLE_HNIC_MICRO_STREAMS",
    "When true, hnic will use multi micro archStreams",
    true,
    MakePrivate);

GlobalConfInt64 GCFG_OFI_CQ_BURST_PROC(
        "OFI_CQ_BURST_PROC",
        "Maximum number of OFI CQ processing each iteration",
        256,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_OFI_MAX_RETRY_DURATION(
        "HCL_OFI_MAX_RETRY_DURATION",
        "Maximum duration in seconds of OFI operation retry",
        10,
        MakePrivate);

GlobalConfBool GCFG_HCL_REDUCE_NON_PEER_QPS(
    "HCL_REDUCE_NON_PEER_QPS",
    "Do not use INVALID_QP value when open QPs for non-peers",
    true,
    MakePrivate);

GlobalConfBool GCFG_HCCL_GET_MACS_FROM_DRIVER(
        "HCCL_GET_MACS_FROM_DRIVER",
        "When false, unless the user passed MAC Addr Info file, hcl will retrieve the MAC addresses",
        false,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_HLCP_CLIENT_IO_THREADS(
        "HCL_HLCP_CLIENT_IO_THREADS",
        "HLCP client IO thread count",
        2,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_HLCP_SERVER_IO_THREADS(
        "HCL_HLCP_SERVER_IO_THREADS",
        "HLCP server IO thread count",
        4,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_HLCP_SERVER_SEND_THREAD_RANKS(
        "HCL_HLCP_SERVER_SEND_THREAD_RANKS",
        "Number of ranks to handle in one send thread. (num of threads == comm_size / ranks_in_thread)",
        8,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_HLCP_OPS_TIMEOUT(
        "HCL_HLCP_OPS_TIMEOUT",
        "HLCP operation timeout (seconds)",
        120,
        MakePrivate);

GlobalConfBool GCFG_HCL_SINGLE_QP_PER_SET(
        "HCL_SINGLE_QP_PER_SET",
        "When true each QP set will contain a single QP, as opposed to 4 QPs when false",
        false,
        MakePrivate);

GlobalConfBool GCFG_HCL_PROFILER_DEBUG_MODE(
        "HCL_PROFILER_DEBUG_MODE",
        "use debug mode when running with profiler",
        false,
        MakePublic);

GlobalConfString GCFG_HCL_HNIC_TCP_EXCLUDE_IF(
        "HCL_HNIC_TCP_EXCLUDE_IF",
        "Regex pattern of interface names to be filtered out for TCP providers",
        std::string("^(lo|docker0|tunl0|virbr\\d)$"),
        MakePrivate);

GlobalConfString GCFG_HCL_HNIC_VERBS_EXCLUDE_IF(
        "HCL_HNIC_VERBS_EXCLUDE_IF",
        "Comma-separated list of interface names to be filtered out for VERBS providers",
        std::string(""),
        MakePublic);

GlobalConfBool GCFG_HCL_DFA_DUMP_WQE(
        "HCL_DFA_DUMP_WQE",
        "When true, dump all WQEs from all requester QPs on DFA. NOTE: Need to run as root.",
        false,
        MakePublic);

GlobalConfBool GCFG_HCL_USE_NET_DETECT(
        "HCL_USE_NET_DETECT",
        "Detect network interfaces (don't use gaudi net file)",
        false,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_IBV_RETRY_TIMEOUT_SEC(
        "HCL_IBV_RETRY_TIMEOUT_SEC",
        "Retry timeout in seconds for ibv EBUSY error",
        180,
        MakePrivate);

GlobalConfUint64 GCFG_FORCE_ORDER_SIGNALS(
        "FORCE_ORDER_SIGNALS",
        "define number of signals for force_order. By default, we use the number defined in json. If this envar value is != 0, it will override the json definition",
        0,
        MakePrivate);

GlobalConfUint64 GCFG_EDMA_SIGNALS(
        "EDMA_SIGNALS",
        "define number of signals for EDMA operations, per core",
        1,
        MakePrivate);

GlobalConfUint64 GCFG_NIC_SEND_SIGNALS(
        "NIC_SEND_SIGNALS",
        "define number of signals for NIC SEND operation, per NIC in connection",
        1,
        MakePrivate);

GlobalConfUint64 GCFG_NIC_RECV_SIGNALS(
        "NIC_RECV_SIGNALS",
        "define number of signals for NIC RECV operation, per NIC in connection",
        1,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_FAULT_INJECT_LISTENER_PORT(
        "HCL_FAULT_INJECT_LISTENER_PORT",
        "TCP Port base to listen for fault injection thread, 0 if disabled",
        0,
        MakePrivate);

GlobalConfBool GCFG_HCL_FAULT_INJECT_LISTENER_ENABLE(
        "HCL_FAULT_INJECT_LISTENER_ENABLE",
        "Enable listen for fault injection thread, 0 if disabled, need to set the port in another envar",
        false,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_FAULT_TOLERANCE_DELAY_BEFORE_START(
        "HCL_FAULT_TOLERANCE_DELAY_BEFORE_START",
        "Delay in msec before starting fault tolerance process",
        100,
        MakePrivate);

GlobalConfUint64 GCFG_HCL_FAULT_TOLERANCE_COMM_POLL_INTERVAL(
        "HCL_FAULT_TOLERANCE_COMM_POLL_INTERVAL",
        "Interval in msec in polling loop of comm API # by the FT thread",
        1000,
        MakePrivate);

GlobalConfBool GCFG_HCL_FAULT_TOLERANCE_ENABLE(
        "HCL_FAULT_TOLERANCE_ENABLE",
        "Enable fault tolerance process, only for G3",
        DfltBool("false") << deviceValue(synDeviceGaudi3, true),
        MakePublic);

GlobalConfUint64 GCFG_HCL_FAULT_TOLERANCE_LOGICAL_PORTS_SHUTDOWN_MASK(
        "HCL_FAULT_TOLERANCE_LOGICAL_PORTS_SHUTDOWN_MASK",
        "Logical scaleout port mask to simulate shutdown (e.g. 0x000001 to shutdown first scaleout port)",
        DfltUint64(0x0),
        MakePublic);

GlobalConfUint64 GCFG_HCL_FAULT_TOLERANCE_FAILBACK_DELAY(
        "HCL_FAULT_TOLERANCE_FAILBACK_DELAY",
        "Time is seconds from the link-up notification until we start the failback process",
        DfltUint64(300),
        MakePublic);

GlobalConfBool GCFG_HCL_DFA_DUMP_MEMORY(
        "HCL_DFA_DUMP_MEMORY",
        "Dump most recently used memory",
        false,
        MakePublic);
