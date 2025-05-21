#include "platform/gen2_arch_common/server_connectivity_types.h"  // for Gen2ArchNicsDeviceSingleConfig, ServerNicsConnectivityVector

#include <tuple>  // for make_tuple

#include "platform/gaudi2/connectivity_autogen_HLS2PCIE.h"  // for extern

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_0_mapping = {
    std::make_tuple(1, 0, 0),                         // NIC=0
    std::make_tuple(1, 1, 1),                         // NIC=1
    std::make_tuple(1, 2, 2),                         // NIC=2
    std::make_tuple(1, 3, 3),                         // NIC=3
    std::make_tuple(1, 4, 4),                         // NIC=4
    std::make_tuple(1, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(3, 10, 0),                        // NIC=10
    std::make_tuple(3, 11, 1),                        // NIC=11
    std::make_tuple(3, 12, 2),                        // NIC=12
    std::make_tuple(3, 13, 3),                        // NIC=13
    std::make_tuple(3, 14, 4),                        // NIC=14
    std::make_tuple(3, 15, 5),                        // NIC=15
    std::make_tuple(2, 16, 0),                        // NIC=16
    std::make_tuple(2, 17, 1),                        // NIC=17
    std::make_tuple(2, 18, 2),                        // NIC=18
    std::make_tuple(2, 19, 3),                        // NIC=19
    std::make_tuple(2, 20, 4),                        // NIC=20
    std::make_tuple(2, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_1_mapping = {
    std::make_tuple(0, 0, 0),                         // NIC=0
    std::make_tuple(0, 1, 1),                         // NIC=1
    std::make_tuple(0, 2, 2),                         // NIC=2
    std::make_tuple(0, 3, 3),                         // NIC=3
    std::make_tuple(0, 4, 4),                         // NIC=4
    std::make_tuple(0, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(2, 10, 0),                        // NIC=10
    std::make_tuple(2, 11, 1),                        // NIC=11
    std::make_tuple(2, 12, 2),                        // NIC=12
    std::make_tuple(2, 13, 3),                        // NIC=13
    std::make_tuple(2, 14, 4),                        // NIC=14
    std::make_tuple(2, 15, 5),                        // NIC=15
    std::make_tuple(3, 16, 0),                        // NIC=16
    std::make_tuple(3, 17, 1),                        // NIC=17
    std::make_tuple(3, 18, 2),                        // NIC=18
    std::make_tuple(3, 19, 3),                        // NIC=19
    std::make_tuple(3, 20, 4),                        // NIC=20
    std::make_tuple(3, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_2_mapping = {
    std::make_tuple(3, 0, 0),                         // NIC=0
    std::make_tuple(3, 1, 1),                         // NIC=1
    std::make_tuple(3, 2, 2),                         // NIC=2
    std::make_tuple(3, 3, 3),                         // NIC=3
    std::make_tuple(3, 4, 4),                         // NIC=4
    std::make_tuple(3, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(1, 10, 0),                        // NIC=10
    std::make_tuple(1, 11, 1),                        // NIC=11
    std::make_tuple(1, 12, 2),                        // NIC=12
    std::make_tuple(1, 13, 3),                        // NIC=13
    std::make_tuple(1, 14, 4),                        // NIC=14
    std::make_tuple(1, 15, 5),                        // NIC=15
    std::make_tuple(0, 16, 0),                        // NIC=16
    std::make_tuple(0, 17, 1),                        // NIC=17
    std::make_tuple(0, 18, 2),                        // NIC=18
    std::make_tuple(0, 19, 3),                        // NIC=19
    std::make_tuple(0, 20, 4),                        // NIC=20
    std::make_tuple(0, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_3_mapping = {
    std::make_tuple(2, 0, 0),                         // NIC=0
    std::make_tuple(2, 1, 1),                         // NIC=1
    std::make_tuple(2, 2, 2),                         // NIC=2
    std::make_tuple(2, 3, 3),                         // NIC=3
    std::make_tuple(2, 4, 4),                         // NIC=4
    std::make_tuple(2, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(0, 10, 0),                        // NIC=10
    std::make_tuple(0, 11, 1),                        // NIC=11
    std::make_tuple(0, 12, 2),                        // NIC=12
    std::make_tuple(0, 13, 3),                        // NIC=13
    std::make_tuple(0, 14, 4),                        // NIC=14
    std::make_tuple(0, 15, 5),                        // NIC=15
    std::make_tuple(1, 16, 0),                        // NIC=16
    std::make_tuple(1, 17, 1),                        // NIC=17
    std::make_tuple(1, 18, 2),                        // NIC=18
    std::make_tuple(1, 19, 3),                        // NIC=19
    std::make_tuple(1, 20, 4),                        // NIC=20
    std::make_tuple(1, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_4_mapping = {
    std::make_tuple(5, 0, 0),                         // NIC=0
    std::make_tuple(5, 1, 1),                         // NIC=1
    std::make_tuple(5, 2, 2),                         // NIC=2
    std::make_tuple(5, 3, 3),                         // NIC=3
    std::make_tuple(5, 4, 4),                         // NIC=4
    std::make_tuple(5, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(7, 10, 0),                        // NIC=10
    std::make_tuple(7, 11, 1),                        // NIC=11
    std::make_tuple(7, 12, 2),                        // NIC=12
    std::make_tuple(7, 13, 3),                        // NIC=13
    std::make_tuple(7, 14, 4),                        // NIC=14
    std::make_tuple(7, 15, 5),                        // NIC=15
    std::make_tuple(6, 16, 0),                        // NIC=16
    std::make_tuple(6, 17, 1),                        // NIC=17
    std::make_tuple(6, 18, 2),                        // NIC=18
    std::make_tuple(6, 19, 3),                        // NIC=19
    std::make_tuple(6, 20, 4),                        // NIC=20
    std::make_tuple(6, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_5_mapping = {
    std::make_tuple(4, 0, 0),                         // NIC=0
    std::make_tuple(4, 1, 1),                         // NIC=1
    std::make_tuple(4, 2, 2),                         // NIC=2
    std::make_tuple(4, 3, 3),                         // NIC=3
    std::make_tuple(4, 4, 4),                         // NIC=4
    std::make_tuple(4, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(6, 10, 0),                        // NIC=10
    std::make_tuple(6, 11, 1),                        // NIC=11
    std::make_tuple(6, 12, 2),                        // NIC=12
    std::make_tuple(6, 13, 3),                        // NIC=13
    std::make_tuple(6, 14, 4),                        // NIC=14
    std::make_tuple(6, 15, 5),                        // NIC=15
    std::make_tuple(7, 16, 0),                        // NIC=16
    std::make_tuple(7, 17, 1),                        // NIC=17
    std::make_tuple(7, 18, 2),                        // NIC=18
    std::make_tuple(7, 19, 3),                        // NIC=19
    std::make_tuple(7, 20, 4),                        // NIC=20
    std::make_tuple(7, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_6_mapping = {
    std::make_tuple(7, 0, 0),                         // NIC=0
    std::make_tuple(7, 1, 1),                         // NIC=1
    std::make_tuple(7, 2, 2),                         // NIC=2
    std::make_tuple(7, 3, 3),                         // NIC=3
    std::make_tuple(7, 4, 4),                         // NIC=4
    std::make_tuple(7, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(5, 10, 0),                        // NIC=10
    std::make_tuple(5, 11, 1),                        // NIC=11
    std::make_tuple(5, 12, 2),                        // NIC=12
    std::make_tuple(5, 13, 3),                        // NIC=13
    std::make_tuple(5, 14, 4),                        // NIC=14
    std::make_tuple(5, 15, 5),                        // NIC=15
    std::make_tuple(4, 16, 0),                        // NIC=16
    std::make_tuple(4, 17, 1),                        // NIC=17
    std::make_tuple(4, 18, 2),                        // NIC=18
    std::make_tuple(4, 19, 3),                        // NIC=19
    std::make_tuple(4, 20, 4),                        // NIC=20
    std::make_tuple(4, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS2PCIE_card_location_7_mapping = {
    std::make_tuple(6, 0, 0),                         // NIC=0
    std::make_tuple(6, 1, 1),                         // NIC=1
    std::make_tuple(6, 2, 2),                         // NIC=2
    std::make_tuple(6, 3, 3),                         // NIC=3
    std::make_tuple(6, 4, 4),                         // NIC=4
    std::make_tuple(6, 5, 5),                         // NIC=5
    std::make_tuple(SCALEOUT_DEVICE_ID, 6, 0),        // NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 1),        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 2),        // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 3),        // NIC=9
    std::make_tuple(4, 10, 0),                        // NIC=10
    std::make_tuple(4, 11, 1),                        // NIC=11
    std::make_tuple(4, 12, 2),                        // NIC=12
    std::make_tuple(4, 13, 3),                        // NIC=13
    std::make_tuple(4, 14, 4),                        // NIC=14
    std::make_tuple(4, 15, 5),                        // NIC=15
    std::make_tuple(5, 16, 0),                        // NIC=16
    std::make_tuple(5, 17, 1),                        // NIC=17
    std::make_tuple(5, 18, 2),                        // NIC=18f
    std::make_tuple(5, 19, 3),                        // NIC=19
    std::make_tuple(5, 20, 4),                        // NIC=20
    std::make_tuple(5, 21, 5),                        // NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 0),  // NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 1),  // NIC=23
};

// clang-format off

const ServerNicsConnectivityVector g_HLS2PCIEServerConnectivityVector = {
    g_HLS2PCIE_card_location_0_mapping,
    g_HLS2PCIE_card_location_1_mapping,
    g_HLS2PCIE_card_location_2_mapping,
    g_HLS2PCIE_card_location_3_mapping,
    g_HLS2PCIE_card_location_4_mapping,
    g_HLS2PCIE_card_location_5_mapping,
    g_HLS2PCIE_card_location_6_mapping,
    g_HLS2PCIE_card_location_7_mapping
};

// clang-format on