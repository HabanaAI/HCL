#include "platform/gen2_arch_common/server_connectivity_types.h"  // for Gen2ArchNicsDeviceSingleConfig, ServerNicsConnectivityArray

#include <tuple>  // for make_tuple

#include "platform/gaudi3/connectivity_autogen_HLS3PCIE.h"  // for extern

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_0_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(1, 2, 0),                        // NIC=2
    std::make_tuple(1, 3, 1),                        // NIC=3
    std::make_tuple(1, 4, 2),                        // NIC=4
    std::make_tuple(1, 5, 3),                        // NIC=5
    std::make_tuple(1, 6, 4),                        // NIC=6
    std::make_tuple(1, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(3, 12, 0),                       // NIC=12
    std::make_tuple(3, 13, 1),                       // NIC=13
    std::make_tuple(3, 14, 2),                       // NIC=14
    std::make_tuple(3, 15, 3),                       // NIC=15
    std::make_tuple(2, 16, 0),                       // NIC=16
    std::make_tuple(2, 17, 1),                       // NIC=17
    std::make_tuple(3, 18, 4),                       // NIC=18
    std::make_tuple(3, 19, 5),                       // NIC=19
    std::make_tuple(2, 20, 2),                       // NIC=20
    std::make_tuple(2, 21, 3),                       // NIC=21
    std::make_tuple(2, 22, 4),                       // NIC=22
    std::make_tuple(2, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_1_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(0, 2, 0),                        // NIC=2
    std::make_tuple(0, 3, 1),                        // NIC=3
    std::make_tuple(0, 4, 2),                        // NIC=4
    std::make_tuple(0, 5, 3),                        // NIC=5
    std::make_tuple(0, 6, 4),                        // NIC=6
    std::make_tuple(0, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(2, 12, 0),                       // NIC=12
    std::make_tuple(2, 13, 1),                       // NIC=13
    std::make_tuple(2, 14, 2),                       // NIC=14
    std::make_tuple(2, 15, 3),                       // NIC=15
    std::make_tuple(3, 16, 0),                       // NIC=16
    std::make_tuple(3, 17, 1),                       // NIC=17
    std::make_tuple(2, 18, 4),                       // NIC=18
    std::make_tuple(2, 19, 5),                       // NIC=19
    std::make_tuple(3, 20, 2),                       // NIC=20
    std::make_tuple(3, 21, 3),                       // NIC=21
    std::make_tuple(3, 22, 4),                       // NIC=22
    std::make_tuple(3, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_2_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(3, 2, 0),                        // NIC=2
    std::make_tuple(3, 3, 1),                        // NIC=3
    std::make_tuple(3, 4, 2),                        // NIC=4
    std::make_tuple(3, 5, 3),                        // NIC=5
    std::make_tuple(3, 6, 4),                        // NIC=6
    std::make_tuple(3, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(1, 12, 0),                       // NIC=12
    std::make_tuple(1, 13, 1),                       // NIC=13
    std::make_tuple(1, 14, 2),                       // NIC=14
    std::make_tuple(1, 15, 3),                       // NIC=15
    std::make_tuple(0, 16, 0),                       // NIC=16
    std::make_tuple(0, 17, 1),                       // NIC=17
    std::make_tuple(1, 18, 4),                       // NIC=18
    std::make_tuple(1, 19, 5),                       // NIC=19
    std::make_tuple(0, 20, 2),                       // NIC=20
    std::make_tuple(0, 21, 3),                       // NIC=21
    std::make_tuple(0, 22, 4),                       // NIC=22
    std::make_tuple(0, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_3_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(2, 2, 0),                        // NIC=2
    std::make_tuple(2, 3, 1),                        // NIC=3
    std::make_tuple(2, 4, 2),                        // NIC=4
    std::make_tuple(2, 5, 3),                        // NIC=5
    std::make_tuple(2, 6, 4),                        // NIC=6
    std::make_tuple(2, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(0, 12, 0),                       // NIC=12
    std::make_tuple(0, 13, 1),                       // NIC=13
    std::make_tuple(0, 14, 2),                       // NIC=14
    std::make_tuple(0, 15, 3),                       // NIC=15
    std::make_tuple(1, 16, 0),                       // NIC=16
    std::make_tuple(1, 17, 1),                       // NIC=17
    std::make_tuple(0, 18, 4),                       // NIC=18
    std::make_tuple(0, 19, 5),                       // NIC=19
    std::make_tuple(1, 20, 2),                       // NIC=20
    std::make_tuple(1, 21, 3),                       // NIC=21
    std::make_tuple(1, 22, 4),                       // NIC=22
    std::make_tuple(1, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_4_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(5, 2, 0),                        // NIC=2
    std::make_tuple(5, 3, 1),                        // NIC=3
    std::make_tuple(5, 4, 2),                        // NIC=4
    std::make_tuple(5, 5, 3),                        // NIC=5
    std::make_tuple(5, 6, 4),                        // NIC=6
    std::make_tuple(5, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(7, 12, 0),                       // NIC=12
    std::make_tuple(7, 13, 1),                       // NIC=13
    std::make_tuple(7, 14, 2),                       // NIC=14
    std::make_tuple(7, 15, 3),                       // NIC=15
    std::make_tuple(6, 16, 0),                       // NIC=16
    std::make_tuple(6, 17, 1),                       // NIC=17
    std::make_tuple(7, 18, 4),                       // NIC=18
    std::make_tuple(7, 19, 5),                       // NIC=19
    std::make_tuple(6, 20, 2),                       // NIC=20
    std::make_tuple(6, 21, 3),                       // NIC=21
    std::make_tuple(6, 22, 4),                       // NIC=22
    std::make_tuple(6, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_5_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(4, 2, 0),                        // NIC=2
    std::make_tuple(4, 3, 1),                        // NIC=3
    std::make_tuple(4, 4, 2),                        // NIC=4
    std::make_tuple(4, 5, 3),                        // NIC=5
    std::make_tuple(4, 6, 4),                        // NIC=6
    std::make_tuple(4, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(6, 12, 0),                       // NIC=12
    std::make_tuple(6, 13, 1),                       // NIC=13
    std::make_tuple(6, 14, 2),                       // NIC=14
    std::make_tuple(6, 15, 3),                       // NIC=15
    std::make_tuple(7, 16, 0),                       // NIC=16
    std::make_tuple(7, 17, 1),                       // NIC=17
    std::make_tuple(6, 18, 4),                       // NIC=18
    std::make_tuple(6, 19, 5),                       // NIC=19
    std::make_tuple(7, 20, 2),                       // NIC=20
    std::make_tuple(7, 21, 3),                       // NIC=21
    std::make_tuple(7, 22, 4),                       // NIC=22
    std::make_tuple(7, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_6_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(7, 2, 0),                        // NIC=2
    std::make_tuple(7, 3, 1),                        // NIC=3
    std::make_tuple(7, 4, 2),                        // NIC=4
    std::make_tuple(7, 5, 3),                        // NIC=5
    std::make_tuple(7, 6, 4),                        // NIC=6
    std::make_tuple(7, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(5, 12, 0),                       // NIC=12
    std::make_tuple(5, 13, 1),                       // NIC=13
    std::make_tuple(5, 14, 2),                       // NIC=14
    std::make_tuple(5, 15, 3),                       // NIC=15
    std::make_tuple(4, 16, 0),                       // NIC=16
    std::make_tuple(4, 17, 1),                       // NIC=17
    std::make_tuple(5, 18, 4),                       // NIC=18
    std::make_tuple(5, 19, 5),                       // NIC=19
    std::make_tuple(4, 20, 2),                       // NIC=20
    std::make_tuple(4, 21, 3),                       // NIC=21
    std::make_tuple(4, 22, 4),                       // NIC=22
    std::make_tuple(4, 23, 5),                       // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3PCIE_card_location_7_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),  // NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),  // NIC=1
    std::make_tuple(6, 2, 0),                        // NIC=2
    std::make_tuple(6, 3, 1),                        // NIC=3
    std::make_tuple(6, 4, 2),                        // NIC=4
    std::make_tuple(6, 5, 3),                        // NIC=5
    std::make_tuple(6, 6, 4),                        // NIC=6
    std::make_tuple(6, 7, 5),                        // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),       // NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 1),       // NIC=9
    std::make_tuple(SCALEOUT_DEVICE_ID, 10, 2),      // NIC=10
    std::make_tuple(SCALEOUT_DEVICE_ID, 11, 3),      // NIC=11
    std::make_tuple(4, 12, 0),                       // NIC=12
    std::make_tuple(4, 13, 1),                       // NIC=13
    std::make_tuple(4, 14, 2),                       // NIC=14
    std::make_tuple(4, 15, 3),                       // NIC=15
    std::make_tuple(5, 16, 0),                       // NIC=16
    std::make_tuple(5, 17, 1),                       // NIC=17
    std::make_tuple(4, 18, 4),                       // NIC=18
    std::make_tuple(4, 19, 5),                       // NIC=19
    std::make_tuple(5, 20, 2),                       // NIC=20
    std::make_tuple(5, 21, 3),                       // NIC=21
    std::make_tuple(5, 22, 4),                       // NIC=22
    std::make_tuple(5, 23, 5),                       // NIC=23
};

// clang-format off

const ServerNicsConnectivityArray g_HLS3PCIEServerConnectivityArray = {
    g_HLS3PCIE_card_location_0_mapping,
    g_HLS3PCIE_card_location_1_mapping,
    g_HLS3PCIE_card_location_2_mapping,
    g_HLS3PCIE_card_location_3_mapping,
    g_HLS3PCIE_card_location_4_mapping,
    g_HLS3PCIE_card_location_5_mapping,
    g_HLS3PCIE_card_location_6_mapping,
    g_HLS3PCIE_card_location_7_mapping
};

// clang-format on