#include "helpers.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <net/if.h>

#include "hcl_utils.h"

/* There is a specific formula to convert the MAC to GID.
 * BYTE 0, 1: subnet Prefix
 * BYTE 8, 9, 10: Mac addr bytes 0, 1, 2 (flip bit 1)
 * BYTE 11, 12: constants 0xFF, 0xFE
 * BYTE 13, 14, 15: Mac addr bytes 3, 4, 5
 */
#define DEFAULT_GID_SUBNET_PREFIX 0xfe80000000000000ULL

void mac_to_gid(uint64_t _mac, ibv_gid& gid)
{
    uint8_t* mac = (uint8_t*)&_mac;

    gid.global.subnet_prefix = htobe64(DEFAULT_GID_SUBNET_PREFIX);

    gid.raw[8]  = mac[0] ^ 2;
    gid.raw[9]  = mac[1];
    gid.raw[10] = mac[2];
    gid.raw[11] = 0xFF;
    gid.raw[12] = 0xFE;
    gid.raw[13] = mac[3];
    gid.raw[14] = mac[4];
    gid.raw[15] = mac[5];
}

void ip4addr_to_gid(uint32_t ipv4_addr /*network byte order*/, ibv_gid& gid)
{
    uint32_t* p32gid = (uint32_t*)&gid;

    p32gid[0] = 0;
    p32gid[1] = 0;
    p32gid[2] = 0xffff0000;
    p32gid[3] = ipv4_addr;
}

#ifndef IBV_MTU_8192
    #define IBV_MTU_8192 6
#endif /* IBV_MTU_8192 */

ibv_mtu to_ibdev_mtu(int mtu)
{
    switch (mtu)
    {
        case 256:
            return IBV_MTU_256;
        case 512:
            return IBV_MTU_512;
        case 1024:
            return IBV_MTU_1024;
        case 2048:
            return IBV_MTU_2048;
        case 4096:
            return IBV_MTU_4096;
        case 8192:
            return (ibv_mtu)IBV_MTU_8192;
        default:
            return (ibv_mtu)0;
    }
}

std::string readFile(const std::string& filePath)
{
    std::string result;
    std::ifstream file(filePath);

    if (file.is_open())
    {
        std::ostringstream sstr;
        sstr << file.rdbuf();
        file.close();

        result = sstr.str();
    }

    return result;
}

// "fe80:0000:0000:0000:b2fd:0bff:fed5:d3c5" -> uint8_t[16]
// [0]=0xfe [1]=0x80 [2]=0 ...   [14]=0xd3 [15]=0xc5
ibv_gid str2gid(const std::string& sgid)
{
    ibv_gid result = {};

    std::istringstream iss(sgid);
    iss >> std::hex;

    char colon;
    uint16_t value;

    for (uint32_t i = 0; i < (sizeof(result) / sizeof(uint16_t)); ++i)
    {
        if (iss.peek() == ':')
            iss >> colon;

        iss >> value;

        if (!iss)
            break;

        ((uint16_t*)result.raw)[i] = __bswap_16(value);
    }

    return result;
}

ibv_gid handle_gid(const std::string& path)
{
    return str2gid(readFile(path));
}

std::string to_lower(const std::string& str)
{
    std::string newStr(str);
    std::transform(newStr.begin(), newStr.end(), newStr.begin(), ::tolower);
    return newStr;
}

ibv_gid_type_sysfs str2gid_type(const std::string& stype)
{
    // IB/RoCE v1
    // RoCE v2

    std::string ls = to_lower(stype);

    if (ls.find("roce") != std::string::npos)
    {
        if (ls.find("v1") != std::string::npos)
        {
            return IBV_GID_TYPE_SYSFS_IB_ROCE_V1;
        }

        if (ls.find("v2") != std::string::npos)
        {
            return IBV_GID_TYPE_SYSFS_ROCE_V2;
        }
    }

    return IBV_GID_TYPE_SYSFS_UNDEFINED;
}

ibv_gid_type_sysfs handle_gid_type(const std::string& path)
{
    return str2gid_type(readFile(path));
}
