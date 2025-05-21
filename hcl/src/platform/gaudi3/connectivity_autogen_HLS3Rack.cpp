#include "platform/gen2_arch_common/server_connectivity_types.h"  // for Gen2ArchNicsDeviceSingleConfig, ServerNicsConnectivityVector

#include <tuple>  // for make_tuple

#include "platform/gaudi3/connectivity_autogen_HLS3Rack.h"  // for extern

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_0_mapping = {
    std::make_tuple(3, 0, 1),                    // NIC=0
    std::make_tuple(3, 1, 0),                    // NIC=1
    std::make_tuple(7, 2, 0),                    // NIC=2
    std::make_tuple(3, 3, 2),                    // NIC=3
    std::make_tuple(7, 4, 2),                    // NIC=4
    std::make_tuple(7, 5, 1),                    // NIC=5
    std::make_tuple(4, 6, 1),                    // NIC=6
    std::make_tuple(4, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(4, 9, 2),                    // NIC=9
    std::make_tuple(2, 16, 2),                   // NIC=10
    std::make_tuple(2, 17, 1),                   // NIC=11
    std::make_tuple(2, 18, 0),                   // NIC=12
    std::make_tuple(1, 13, 2),                   // NIC=13
    std::make_tuple(1, 14, 1),                   // NIC=14
    std::make_tuple(1, 15, 0),                   // NIC=15
    std::make_tuple(6, 16, 2),                   // NIC=16
    std::make_tuple(6, 17, 1),                   // NIC=17
    std::make_tuple(6, 18, 0),                   // NIC=18
    std::make_tuple(5, 19, 2),                   // NIC=19
    std::make_tuple(5, 20, 1),                   // NIC=20
    std::make_tuple(5, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_1_mapping = {
    std::make_tuple(2, 0, 1),                    // NIC=0
    std::make_tuple(2, 1, 0),                    // NIC=1
    std::make_tuple(6, 2, 0),                    // NIC=2
    std::make_tuple(2, 3, 2),                    // NIC=3
    std::make_tuple(6, 4, 2),                    // NIC=4
    std::make_tuple(6, 5, 1),                    // NIC=5
    std::make_tuple(5, 6, 1),                    // NIC=6
    std::make_tuple(5, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(5, 9, 2),                    // NIC=9
    std::make_tuple(7, 10, 2),                   // NIC=10
    std::make_tuple(7, 11, 1),                   // NIC=11
    std::make_tuple(7, 12, 0),                   // NIC=12
    std::make_tuple(0, 13, 2),                   // NIC=13
    std::make_tuple(0, 14, 1),                   // NIC=14
    std::make_tuple(0, 15, 0),                   // NIC=15
    std::make_tuple(3, 16, 2),                   // NIC=16
    std::make_tuple(3, 17, 1),                   // NIC=17
    std::make_tuple(3, 18, 0),                   // NIC=18
    std::make_tuple(4, 19, 2),                   // NIC=19
    std::make_tuple(4, 20, 1),                   // NIC=20
    std::make_tuple(4, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_2_mapping = {
    std::make_tuple(1, 0, 1),                    // NIC=0
    std::make_tuple(1, 1, 0),                    // NIC=1
    std::make_tuple(5, 2, 0),                    // NIC=2
    std::make_tuple(1, 3, 2),                    // NIC=3
    std::make_tuple(5, 4, 2),                    // NIC=4
    std::make_tuple(5, 5, 1),                    // NIC=5
    std::make_tuple(6, 6, 1),                    // NIC=6
    std::make_tuple(6, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(6, 9, 2),                    // NIC=9
    std::make_tuple(3, 10, 2),                   // NIC=10
    std::make_tuple(3, 11, 1),                   // NIC=11
    std::make_tuple(3, 12, 0),                   // NIC=12
    std::make_tuple(4, 13, 2),                   // NIC=13
    std::make_tuple(4, 14, 1),                   // NIC=14
    std::make_tuple(4, 15, 0),                   // NIC=15
    std::make_tuple(0, 10, 2),                   // NIC=16
    std::make_tuple(0, 11, 1),                   // NIC=17
    std::make_tuple(0, 12, 0),                   // NIC=18
    std::make_tuple(7, 19, 2),                   // NIC=19
    std::make_tuple(7, 20, 1),                   // NIC=20
    std::make_tuple(7, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_3_mapping = {
    std::make_tuple(0, 0, 1),                    // NIC=0
    std::make_tuple(0, 1, 0),                    // NIC=1
    std::make_tuple(4, 2, 0),                    // NIC=2
    std::make_tuple(0, 3, 2),                    // NIC=3
    std::make_tuple(4, 4, 2),                    // NIC=4
    std::make_tuple(4, 5, 1),                    // NIC=5
    std::make_tuple(7, 6, 1),                    // NIC=6
    std::make_tuple(7, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(7, 9, 2),                    // NIC=9
    std::make_tuple(2, 10, 2),                   // NIC=10
    std::make_tuple(2, 11, 1),                   // NIC=11
    std::make_tuple(2, 12, 0),                   // NIC=12
    std::make_tuple(5, 13, 2),                   // NIC=13
    std::make_tuple(5, 14, 1),                   // NIC=14
    std::make_tuple(5, 15, 0),                   // NIC=15
    std::make_tuple(1, 16, 2),                   // NIC=16
    std::make_tuple(1, 17, 1),                   // NIC=17
    std::make_tuple(1, 18, 0),                   // NIC=18
    std::make_tuple(6, 19, 2),                   // NIC=19
    std::make_tuple(6, 20, 1),                   // NIC=20
    std::make_tuple(6, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_4_mapping = {
    std::make_tuple(7, 0, 1),                    // NIC=0
    std::make_tuple(7, 1, 0),                    // NIC=1
    std::make_tuple(3, 2, 0),                    // NIC=2
    std::make_tuple(7, 3, 2),                    // NIC=3
    std::make_tuple(3, 4, 2),                    // NIC=4
    std::make_tuple(3, 5, 1),                    // NIC=5
    std::make_tuple(0, 6, 1),                    // NIC=6
    std::make_tuple(0, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(0, 9, 2),                    // NIC=9
    std::make_tuple(5, 10, 2),                   // NIC=10
    std::make_tuple(5, 11, 1),                   // NIC=11
    std::make_tuple(5, 12, 0),                   // NIC=12
    std::make_tuple(2, 13, 2),                   // NIC=13
    std::make_tuple(2, 14, 1),                   // NIC=14
    std::make_tuple(2, 15, 0),                   // NIC=15
    std::make_tuple(6, 10, 2),                   // NIC=16
    std::make_tuple(6, 11, 1),                   // NIC=17
    std::make_tuple(6, 12, 0),                   // NIC=18
    std::make_tuple(1, 19, 2),                   // NIC=19
    std::make_tuple(1, 20, 1),                   // NIC=20
    std::make_tuple(1, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_5_mapping = {
    std::make_tuple(6, 0, 1),                    // NIC=0
    std::make_tuple(6, 1, 0),                    // NIC=1
    std::make_tuple(2, 2, 0),                    // NIC=2
    std::make_tuple(6, 3, 2),                    // NIC=3
    std::make_tuple(2, 4, 2),                    // NIC=4
    std::make_tuple(2, 5, 1),                    // NIC=5
    std::make_tuple(1, 6, 1),                    // NIC=6
    std::make_tuple(1, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(1, 9, 2),                    // NIC=9
    std::make_tuple(4, 10, 2),                   // NIC=10
    std::make_tuple(4, 11, 1),                   // NIC=11
    std::make_tuple(4, 12, 0),                   // NIC=12
    std::make_tuple(3, 13, 2),                   // NIC=13
    std::make_tuple(3, 14, 1),                   // NIC=14
    std::make_tuple(3, 15, 0),                   // NIC=15
    std::make_tuple(7, 16, 2),                   // NIC=16
    std::make_tuple(7, 17, 1),                   // NIC=17
    std::make_tuple(7, 18, 0),                   // NIC=18
    std::make_tuple(0, 19, 2),                   // NIC=19
    std::make_tuple(0, 20, 1),                   // NIC=20
    std::make_tuple(0, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_6_mapping = {
    std::make_tuple(5, 0, 1),                    // NIC=0
    std::make_tuple(5, 1, 0),                    // NIC=1
    std::make_tuple(1, 2, 0),                    // NIC=2
    std::make_tuple(5, 3, 2),                    // NIC=3
    std::make_tuple(1, 4, 2),                    // NIC=4
    std::make_tuple(1, 5, 1),                    // NIC=5
    std::make_tuple(2, 6, 1),                    // NIC=6
    std::make_tuple(2, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(2, 9, 2),                    // NIC=9
    std::make_tuple(4, 16, 2),                   // NIC=10
    std::make_tuple(4, 17, 1),                   // NIC=11
    std::make_tuple(4, 18, 0),                   // NIC=12
    std::make_tuple(7, 13, 2),                   // NIC=13
    std::make_tuple(7, 14, 1),                   // NIC=14
    std::make_tuple(7, 15, 0),                   // NIC=15
    std::make_tuple(0, 16, 2),                   // NIC=16
    std::make_tuple(0, 17, 1),                   // NIC=17
    std::make_tuple(0, 18, 0),                   // NIC=18
    std::make_tuple(3, 19, 2),                   // NIC=19
    std::make_tuple(3, 20, 1),                   // NIC=20
    std::make_tuple(3, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_7_mapping = {
    std::make_tuple(4, 0, 1),                    // NIC=0
    std::make_tuple(4, 1, 0),                    // NIC=1
    std::make_tuple(0, 2, 0),                    // NIC=2
    std::make_tuple(4, 3, 2),                    // NIC=3
    std::make_tuple(0, 4, 2),                    // NIC=4
    std::make_tuple(0, 5, 1),                    // NIC=5
    std::make_tuple(3, 6, 1),                    // NIC=6
    std::make_tuple(3, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(3, 9, 2),                    // NIC=9
    std::make_tuple(1, 10, 2),                   // NIC=10
    std::make_tuple(1, 11, 1),                   // NIC=11
    std::make_tuple(1, 12, 0),                   // NIC=12
    std::make_tuple(6, 13, 2),                   // NIC=13
    std::make_tuple(6, 14, 1),                   // NIC=14
    std::make_tuple(6, 15, 0),                   // NIC=15
    std::make_tuple(5, 16, 2),                   // NIC=16
    std::make_tuple(5, 17, 1),                   // NIC=17
    std::make_tuple(5, 18, 0),                   // NIC=18
    std::make_tuple(2, 19, 2),                   // NIC=19
    std::make_tuple(2, 20, 1),                   // NIC=20
    std::make_tuple(2, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_8_mapping = {
    std::make_tuple(11, 0, 1),                   // NIC=0
    std::make_tuple(11, 1, 0),                   // NIC=1
    std::make_tuple(15, 2, 0),                   // NIC=2
    std::make_tuple(11, 3, 2),                   // NIC=3
    std::make_tuple(15, 4, 2),                   // NIC=4
    std::make_tuple(15, 5, 1),                   // NIC=5
    std::make_tuple(12, 6, 1),                   // NIC=6
    std::make_tuple(12, 7, 0),                   // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(12, 9, 2),                   // NIC=9
    std::make_tuple(10, 16, 2),                  // NIC=10
    std::make_tuple(10, 17, 1),                  // NIC=11
    std::make_tuple(10, 18, 0),                  // NIC=12
    std::make_tuple(9, 13, 2),                   // NIC=13
    std::make_tuple(9, 14, 1),                   // NIC=14
    std::make_tuple(9, 15, 0),                   // NIC=15
    std::make_tuple(14, 16, 2),                  // NIC=16
    std::make_tuple(14, 17, 1),                  // NIC=17
    std::make_tuple(14, 18, 0),                  // NIC=18
    std::make_tuple(13, 19, 2),                  // NIC=19
    std::make_tuple(13, 20, 1),                  // NIC=20
    std::make_tuple(13, 21, 0),                  // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_9_mapping = {
    std::make_tuple(10, 0, 1),                   // NIC=0
    std::make_tuple(10, 1, 0),                   // NIC=1
    std::make_tuple(14, 2, 0),                   // NIC=2
    std::make_tuple(10, 3, 2),                   // NIC=3
    std::make_tuple(14, 4, 2),                   // NIC=4
    std::make_tuple(14, 5, 1),                   // NIC=5
    std::make_tuple(13, 6, 1),                   // NIC=6
    std::make_tuple(13, 7, 0),                   // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(13, 9, 2),                   // NIC=9
    std::make_tuple(15, 10, 2),                  // NIC=10
    std::make_tuple(15, 11, 1),                  // NIC=11
    std::make_tuple(15, 12, 0),                  // NIC=12
    std::make_tuple(8, 13, 2),                   // NIC=13
    std::make_tuple(8, 14, 1),                   // NIC=14
    std::make_tuple(8, 15, 0),                   // NIC=15
    std::make_tuple(11, 16, 2),                  // NIC=16
    std::make_tuple(11, 17, 1),                  // NIC=17
    std::make_tuple(11, 18, 0),                  // NIC=18
    std::make_tuple(12, 19, 2),                  // NIC=19
    std::make_tuple(12, 20, 1),                  // NIC=20
    std::make_tuple(12, 21, 0),                  // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_10_mapping = {
    std::make_tuple(9, 0, 1),                    // NIC=0
    std::make_tuple(9, 1, 0),                    // NIC=1
    std::make_tuple(13, 2, 0),                   // NIC=2
    std::make_tuple(9, 3, 2),                    // NIC=3
    std::make_tuple(13, 4, 2),                   // NIC=4
    std::make_tuple(13, 5, 1),                   // NIC=5
    std::make_tuple(14, 6, 1),                   // NIC=6
    std::make_tuple(14, 7, 0),                   // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(14, 9, 2),                   // NIC=9
    std::make_tuple(11, 10, 2),                  // NIC=10
    std::make_tuple(11, 11, 1),                  // NIC=11
    std::make_tuple(11, 12, 0),                  // NIC=12
    std::make_tuple(12, 13, 2),                  // NIC=13
    std::make_tuple(12, 14, 1),                  // NIC=14
    std::make_tuple(12, 15, 0),                  // NIC=15
    std::make_tuple(8, 10, 2),                   // NIC=16
    std::make_tuple(8, 11, 1),                   // NIC=17
    std::make_tuple(8, 12, 0),                   // NIC=18
    std::make_tuple(15, 19, 2),                  // NIC=19
    std::make_tuple(15, 20, 1),                  // NIC=20
    std::make_tuple(15, 21, 0),                  // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_11_mapping = {
    std::make_tuple(8, 0, 1),                    // NIC=0
    std::make_tuple(8, 1, 0),                    // NIC=1
    std::make_tuple(12, 2, 0),                   // NIC=2
    std::make_tuple(8, 3, 2),                    // NIC=3
    std::make_tuple(12, 4, 2),                   // NIC=4
    std::make_tuple(12, 5, 1),                   // NIC=5
    std::make_tuple(15, 6, 1),                   // NIC=6
    std::make_tuple(15, 7, 0),                   // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(15, 9, 2),                   // NIC=9
    std::make_tuple(10, 10, 2),                  // NIC=10
    std::make_tuple(10, 11, 1),                  // NIC=11
    std::make_tuple(10, 12, 0),                  // NIC=12
    std::make_tuple(13, 13, 2),                  // NIC=13
    std::make_tuple(13, 14, 1),                  // NIC=14
    std::make_tuple(13, 15, 0),                  // NIC=15
    std::make_tuple(9, 16, 2),                   // NIC=16
    std::make_tuple(9, 17, 1),                   // NIC=17
    std::make_tuple(9, 18, 0),                   // NIC=18
    std::make_tuple(14, 19, 2),                  // NIC=19
    std::make_tuple(14, 20, 1),                  // NIC=20
    std::make_tuple(14, 21, 0),                  // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_12_mapping = {
    std::make_tuple(15, 0, 1),                   // NIC=0
    std::make_tuple(15, 1, 0),                   // NIC=1
    std::make_tuple(11, 2, 0),                   // NIC=2
    std::make_tuple(15, 3, 2),                   // NIC=3
    std::make_tuple(11, 4, 2),                   // NIC=4
    std::make_tuple(11, 5, 1),                   // NIC=5
    std::make_tuple(8, 6, 1),                    // NIC=6
    std::make_tuple(8, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(8, 9, 2),                    // NIC=9
    std::make_tuple(13, 10, 2),                  // NIC=10
    std::make_tuple(13, 11, 1),                  // NIC=11
    std::make_tuple(13, 12, 0),                  // NIC=12
    std::make_tuple(10, 13, 2),                  // NIC=13
    std::make_tuple(10, 14, 1),                  // NIC=14
    std::make_tuple(10, 15, 0),                  // NIC=15
    std::make_tuple(14, 10, 2),                  // NIC=16
    std::make_tuple(14, 11, 1),                  // NIC=17
    std::make_tuple(14, 12, 0),                  // NIC=18
    std::make_tuple(9, 19, 2),                   // NIC=19
    std::make_tuple(9, 20, 1),                   // NIC=20
    std::make_tuple(9, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_13_mapping = {
    std::make_tuple(14, 0, 1),                   // NIC=0
    std::make_tuple(14, 1, 0),                   // NIC=1
    std::make_tuple(10, 2, 0),                   // NIC=2
    std::make_tuple(14, 3, 2),                   // NIC=3
    std::make_tuple(10, 4, 2),                   // NIC=4
    std::make_tuple(10, 5, 1),                   // NIC=5
    std::make_tuple(9, 6, 1),                    // NIC=6
    std::make_tuple(9, 7, 0),                    // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(9, 9, 2),                    // NIC=9
    std::make_tuple(12, 10, 2),                  // NIC=10
    std::make_tuple(12, 11, 1),                  // NIC=11
    std::make_tuple(12, 12, 0),                  // NIC=12
    std::make_tuple(11, 13, 2),                  // NIC=13
    std::make_tuple(11, 14, 1),                  // NIC=14
    std::make_tuple(11, 15, 0),                  // NIC=15
    std::make_tuple(15, 16, 2),                  // NIC=16
    std::make_tuple(15, 17, 1),                  // NIC=17
    std::make_tuple(15, 18, 0),                  // NIC=18
    std::make_tuple(8, 19, 2),                   // NIC=19
    std::make_tuple(8, 20, 1),                   // NIC=20
    std::make_tuple(8, 21, 0),                   // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_14_mapping = {
    std::make_tuple(13, 0, 1),                   // NIC=0
    std::make_tuple(13, 1, 0),                   // NIC=1
    std::make_tuple(9, 2, 0),                    // NIC=2
    std::make_tuple(13, 3, 2),                   // NIC=3
    std::make_tuple(9, 4, 2),                    // NIC=4
    std::make_tuple(9, 5, 1),                    // NIC=5
    std::make_tuple(10, 6, 1),                   // NIC=6
    std::make_tuple(10, 7, 0),                   // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(10, 9, 2),                   // NIC=9
    std::make_tuple(12, 16, 2),                  // NIC=10
    std::make_tuple(12, 17, 1),                  // NIC=11
    std::make_tuple(12, 18, 0),                  // NIC=12
    std::make_tuple(15, 13, 2),                  // NIC=13
    std::make_tuple(15, 14, 1),                  // NIC=14
    std::make_tuple(15, 15, 0),                  // NIC=15
    std::make_tuple(8, 16, 2),                   // NIC=16
    std::make_tuple(8, 17, 1),                   // NIC=17
    std::make_tuple(8, 18, 0),                   // NIC=18
    std::make_tuple(11, 19, 2),                  // NIC=19
    std::make_tuple(11, 20, 1),                  // NIC=20
    std::make_tuple(11, 21, 0),                  // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// <remote card location, remote nic, sub nic index>
static const Gen2ArchNicsDeviceSingleConfig g_HLS3RACK_card_location_15_mapping = {
    std::make_tuple(12, 0, 1),                   // NIC=0
    std::make_tuple(12, 1, 0),                   // NIC=1
    std::make_tuple(8, 2, 0),                    // NIC=2
    std::make_tuple(12, 3, 2),                   // NIC=3
    std::make_tuple(8, 4, 2),                    // NIC=4
    std::make_tuple(8, 5, 1),                    // NIC=5
    std::make_tuple(11, 6, 1),                   // NIC=6
    std::make_tuple(11, 7, 0),                   // NIC=7
    std::make_tuple(SCALEOUT_DEVICE_ID, 8, 0),   // NIC=8
    std::make_tuple(11, 9, 2),                   // NIC=9
    std::make_tuple(9, 10, 2),                   // NIC=10
    std::make_tuple(9, 11, 1),                   // NIC=11
    std::make_tuple(9, 12, 0),                   // NIC=12
    std::make_tuple(14, 13, 2),                  // NIC=13
    std::make_tuple(14, 14, 1),                  // NIC=14
    std::make_tuple(14, 15, 0),                  // NIC=15
    std::make_tuple(13, 16, 2),                  // NIC=16
    std::make_tuple(13, 17, 1),                  // NIC=17
    std::make_tuple(13, 18, 0),                  // NIC=18
    std::make_tuple(10, 19, 2),                  // NIC=19
    std::make_tuple(10, 20, 1),                  // NIC=20
    std::make_tuple(10, 21, 0),                  // NIC=21
    std::make_tuple(SCALEOUT_DEVICE_ID, 22, 1),  // NIC=22
    std::make_tuple(SCALEOUT_DEVICE_ID, 23, 2),  // NIC=23
};

// clang-format off

const ServerNicsConnectivityVector g_HLS3RackServerConnectivityVector = {
    g_HLS3RACK_card_location_0_mapping,
    g_HLS3RACK_card_location_1_mapping,
    g_HLS3RACK_card_location_2_mapping,
    g_HLS3RACK_card_location_3_mapping,
    g_HLS3RACK_card_location_4_mapping,
    g_HLS3RACK_card_location_5_mapping,
    g_HLS3RACK_card_location_6_mapping,
    g_HLS3RACK_card_location_7_mapping,
    g_HLS3RACK_card_location_8_mapping,
    g_HLS3RACK_card_location_9_mapping,
    g_HLS3RACK_card_location_10_mapping,
    g_HLS3RACK_card_location_11_mapping,
    g_HLS3RACK_card_location_12_mapping,
    g_HLS3RACK_card_location_13_mapping,
    g_HLS3RACK_card_location_14_mapping,
    g_HLS3RACK_card_location_15_mapping
};

// clang-format on