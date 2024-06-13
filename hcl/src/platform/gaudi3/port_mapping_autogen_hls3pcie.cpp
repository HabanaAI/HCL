#include "platform/gen2_arch_common/port_mapping_config.h"  // for Gen2ArchNicsDeviceSingleConfig

#include <tuple>  // for make_tuple

#include "platform/gaudi3/port_mapping_autogen_hls3pcie.h"  // for extern

// clang-format off

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_0_mapping = {
    std::make_tuple(1, 12, 0),	// NIC=0
    std::make_tuple(1, 13, 1),	// NIC=1
    std::make_tuple(3, 4, 0),	// NIC=2
    std::make_tuple(3, 5, 1),	// NIC=3
    std::make_tuple(2, 12, 0),	// NIC=4
    std::make_tuple(2, 13, 1),	// NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 12, 0),	// NIC=6
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 13, 1),	// NIC=7
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),	// NIC=8
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),	// NIC=9
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),	// NIC=10
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),	// NIC=11
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 12, 0),	// NIC=12
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 13, 1),	// NIC=13
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 2),	// NIC=14
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 17, 2),	// NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 2),	// NIC=16
    std::make_tuple(SCALEOUT_DEVICE_ID, 17, 0),	// NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 8, 2),	// NIC=18
    std::make_tuple(2, 7, 2),	// NIC=19
    std::make_tuple(SCALEOUT_DEVICE_ID, 20, 1),	// NIC=20
    std::make_tuple(SCALEOUT_DEVICE_ID, 21, 2),	// NIC=21
    std::make_tuple(1, 11, 2),	// NIC=22
    std::make_tuple(3, 22, 2),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_1_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 10, 0),	// NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 11, 1),	// NIC=1
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 0),	// NIC=2
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 5, 0),	// NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 2),	// NIC=4
    std::make_tuple(SCALEOUT_DEVICE_ID, 5, 0),	// NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 20, 0),	// NIC=6
    std::make_tuple(3, 19, 0),	// NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 1),	// NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 2),	// NIC=9
    std::make_tuple(2, 11, 0),	// NIC=10
    std::make_tuple(0, 22, 2),	// NIC=11
    std::make_tuple(0, 0, 0),	// NIC=12
    std::make_tuple(0, 1, 1),	// NIC=13
    std::make_tuple(2, 16, 1),	// NIC=14
    std::make_tuple(2, 17, 2),	// NIC=15
    std::make_tuple(3, 0, 1),	// NIC=16
    std::make_tuple(3, 1, 2),	// NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 1),	// NIC=18
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 2),	// NIC=19
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 14, 1),	// NIC=20
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 15, 2),	// NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 1),	// NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 2),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_2_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 10, 0),	// NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 11, 1),	// NIC=1
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 0),	// NIC=2
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 5, 0),	// NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 2),	// NIC=4
    std::make_tuple(SCALEOUT_DEVICE_ID, 5, 0),	// NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 20, 0),	// NIC=6
    std::make_tuple(0, 19, 2),	// NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 1),	// NIC=8
    std::make_tuple(SCALEOUT_DEVICE_ID, 9, 2),	// NIC=9
    std::make_tuple(3, 23, 0),	// NIC=10
    std::make_tuple(1, 10, 0),	// NIC=11
    std::make_tuple(0, 4, 0),	// NIC=12
    std::make_tuple(0, 5, 1),	// NIC=13
    std::make_tuple(3, 2, 1),	// NIC=14
    std::make_tuple(3, 3, 2),	// NIC=15
    std::make_tuple(1, 14, 1),	// NIC=16
    std::make_tuple(1, 15, 2),	// NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 1),	// NIC=18
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 17, 2),	// NIC=19
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 2, 1),	// NIC=20
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 3, 2),	// NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 2, 1),	// NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 3, 2),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_3_mapping = {
    std::make_tuple(1, 16, 1),	// NIC=0
    std::make_tuple(1, 17, 2),	// NIC=1
    std::make_tuple(2, 14, 1),	// NIC=2
    std::make_tuple(2, 15, 2),	// NIC=3
    std::make_tuple(0, 2, 0),	// NIC=4
    std::make_tuple(0, 3, 1),	// NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 14, 0),	// NIC=6
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 15, 1),	// NIC=7
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 0),	// NIC=8
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 5, 1),	// NIC=9
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 0),	// NIC=10
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 5, 1),	// NIC=11
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 0),	// NIC=12
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 17, 1),	// NIC=13
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 2),	// NIC=14
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 17, 2),	// NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 2),	// NIC=16
    std::make_tuple(SCALEOUT_DEVICE_ID, 17, 0),	// NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 8, 2),	// NIC=18
    std::make_tuple(1, 7, 0),	// NIC=19
    std::make_tuple(SCALEOUT_DEVICE_ID, 20, 1),	// NIC=20
    std::make_tuple(SCALEOUT_DEVICE_ID, 21, 2),	// NIC=21
    std::make_tuple(0, 23, 2),	// NIC=22
    std::make_tuple(2, 10, 0),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_4_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 8, 0),	// NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 9, 1),	// NIC=1
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 20, 1),	// NIC=2
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 21, 2),	// NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 10, 0),	// NIC=4
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 11, 1),	// NIC=5
    std::make_tuple(5, 20, 0),	// NIC=6
    std::make_tuple(5, 21, 1),	// NIC=7
    std::make_tuple(7, 8, 0),	// NIC=8
    std::make_tuple(7, 9, 1),	// NIC=9
    std::make_tuple(6, 18, 0),	// NIC=10
    std::make_tuple(6, 19, 1),	// NIC=11
    std::make_tuple(6, 1, 2),	// NIC=12
    std::make_tuple(5, 0, 2),	// NIC=13
    std::make_tuple(SCALEOUT_DEVICE_ID, 14, 0),	// NIC=14
    std::make_tuple(SCALEOUT_DEVICE_ID, 15, 1),	// NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 2, 0),	// NIC=16
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 15, 2),	// NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 2),	// NIC=18
    std::make_tuple(SCALEOUT_DEVICE_ID, 19, 2),	// NIC=19
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 0),	// NIC=20
    std::make_tuple(7, 21, 2),	// NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 1),	// NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 19, 2),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_5_mapping = {
    std::make_tuple(4, 13, 2),	// NIC=0
    std::make_tuple(7, 12, 0),	// NIC=1
    std::make_tuple(SCALEOUT_DEVICE_ID, 2, 0),	// NIC=2
    std::make_tuple(SCALEOUT_DEVICE_ID, 3, 1),	// NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 14, 2),	// NIC=4
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 3, 0),	// NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 2),	// NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 2),	// NIC=7
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 2),	// NIC=8
    std::make_tuple(6, 9, 0),	// NIC=9
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),	// NIC=10
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),	// NIC=11
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 0),	// NIC=12
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 7, 1),	// NIC=13
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 0),	// NIC=14
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 7, 1),	// NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 1),	// NIC=16
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 19, 2),	// NIC=17
    std::make_tuple(6, 20, 1),	// NIC=18
    std::make_tuple(6, 21, 2),	// NIC=19
    std::make_tuple(4, 6, 0),	// NIC=20
    std::make_tuple(4, 7, 1),	// NIC=21
    std::make_tuple(7, 6, 1),	// NIC=22
    std::make_tuple(7, 7, 2),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_6_mapping = {
    std::make_tuple(7, 13, 0),	// NIC=0
    std::make_tuple(4, 12, 2),	// NIC=1
    std::make_tuple(SCALEOUT_DEVICE_ID, 2, 0),	// NIC=2
    std::make_tuple(SCALEOUT_DEVICE_ID, 3, 1),	// NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 14, 2),	// NIC=4
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 3, 0),	// NIC=5
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 4, 2),	// NIC=6
    std::make_tuple(SCALEOUT_DEVICE_ID, 7, 2),	// NIC=7
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 18, 2),	// NIC=8
    std::make_tuple(5, 9, 0),	// NIC=9
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 0, 0),	// NIC=10
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 1, 1),	// NIC=11
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 12, 0),	// NIC=12
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 13, 1),	// NIC=13
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 20, 1),	// NIC=14
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 21, 2),	// NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 12, 0),	// NIC=16
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 13, 1),	// NIC=17
    std::make_tuple(4, 10, 0),	// NIC=18
    std::make_tuple(4, 11, 1),	// NIC=19
    std::make_tuple(5, 18, 1),	// NIC=20
    std::make_tuple(5, 19, 2),	// NIC=21
    std::make_tuple(7, 10, 1),	// NIC=22
    std::make_tuple(7, 11, 2),	// NIC=23
};

// <remote card location, remote nic, sub nic index>
const Gen2ArchNicsDeviceSingleConfig g_hls3pcie_card_location_7_mapping = {
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 10, 0),	// NIC=0
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 11, 1),	// NIC=1
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 1),	// NIC=2
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 2),	// NIC=3
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 8, 0),	// NIC=4
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 9, 1),	// NIC=5
    std::make_tuple(5, 22, 1),	// NIC=6
    std::make_tuple(5, 23, 2),	// NIC=7
    std::make_tuple(4, 8, 0),	// NIC=8
    std::make_tuple(4, 9, 1),	// NIC=9
    std::make_tuple(6, 22, 1),	// NIC=10
    std::make_tuple(6, 23, 2),	// NIC=11
    std::make_tuple(5, 1, 0),	// NIC=12
    std::make_tuple(6, 0, 0),	// NIC=13
    std::make_tuple(SCALEOUT_DEVICE_ID, 14, 0),	// NIC=14
    std::make_tuple(SCALEOUT_DEVICE_ID, 15, 1),	// NIC=15
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 2, 0),	// NIC=16
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 15, 2),	// NIC=17
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 16, 2),	// NIC=18
    std::make_tuple(SCALEOUT_DEVICE_ID, 19, 2),	// NIC=19
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 6, 0),	// NIC=20
    std::make_tuple(4, 21, 2),	// NIC=21
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 22, 1),	// NIC=22
    std::make_tuple(NOT_CONNECTED_DEVICE_ID, 23, 2),	// NIC=23
};

// clang-format on