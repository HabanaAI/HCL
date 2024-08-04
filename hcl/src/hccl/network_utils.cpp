/******************************************************************************
 * Copyright (C) 2021 Habana Labs, Ltd. an Intel Company
 * All Rights Reserved.
 *
 * Unauthorized copying of this file or any element(s) within it, via any medium
 * is strictly prohibited.
 * This file contains Habana Labs, Ltd. proprietary and confidential information
 * and is subject to the confidentiality and license agreements under which it
 * was provided.
 *
 ******************************************************************************/

#include "network_utils.h"


#include <cerrno>                       // for errno, EAGAIN, EINTR, ENODATA
#include <ifaddrs.h>                    // for freeifaddrs, ifaddrs, getifa...
#include <linux/ethtool.h>              // for ethtool_drvinfo, ETHTOOL_GDR...
#include <linux/sockios.h>              // for SIOCETHTOOL
#include <net/if.h>                     // for ifreq, ifr_data, ifr_name
#include <cstdint>                      // for uint8_t
#include <cstddef>                      // for size_t
#include <cstring>                      // for memset, strcpy, stre...
#include <sys/ioctl.h>                  // for ioctl
#include <sys/socket.h>                 // for AF_INET, sockaddr, AF_INET6
#include <unistd.h>                     // for close
#include <algorithm>                    // for mismatch
#include <chrono>                       // for operator>, seconds, operator-
#include <limits>                       // for numeric_limits
#include <memory>                       // for allocator_traits<>::value_type
#include <ostream>                      // for operator<<, basic_ostream
#include <utility>                      // for pair
#include <vector>                       // for vector
#include "hcl_global_conf.h"            // for GCFG_HCCL_COMM_ID, GCFG_HCCL...
#include "hcl_utils.h"                  // for VERIFY
#include "hcl_log_manager.h"            // for LOG_DEBUG, LOG_ERR, LOG_WARN

constexpr auto MAX_RECV_WARN_TIME = std::chrono::seconds(2);
constexpr auto MAX_RECV_TIMEOUT   = std::chrono::seconds(10);

// Determines whether the specified TCP network interface name matches a prefix-based pattern, e.g. pattern "eth"
// matches "eth0".
bool match_tcp_if_pattern(const std::string& tcp_if_name, const std::vector<std::string>& ifs)
{
    if (ifs.empty())
    {
        return true;
    }

    for (auto const& interface : ifs)
    {
        auto res = std::mismatch(interface.cbegin(), interface.cend(), tcp_if_name.cbegin());
        if (res.first == interface.cend())
        {
            return true;
        }
    }

    return false;
}

bool match_tcp_if_pattern(const std::string& tcp_if_name, const char* tcp_if)
{
    std::vector<std::string> interface;
    interface.push_back(tcp_if);
    return match_tcp_if_pattern(tcp_if_name, interface);
}

// Retrieves the TCP network interface to be used for server socket creation from HCCL_SOCKET_IFNAME environment
// variable
std::string get_desired_tcp_if_from_env_var()
{
    return GCFG_HCCL_SOCKET_IFNAME.value();
}

// Verify that the NIC isn't a Gaudi NIC
bool verify_useable_if(ifaddrs* net_if)
{
    struct ethtool_drvinfo drvinfo;
    std::string            habDrv = "habanalabs";
    struct ifreq           ifr;
    int                    sock;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1)
    {
        LOG_WARN(HCL, "Failed opening socket for driver info on: {}", net_if->ifa_name);
        return false;
    }

    strcpy(ifr.ifr_name, net_if->ifa_name);

    ifr.ifr_data = (char*)&drvinfo;
    drvinfo.cmd  = ETHTOOL_GDRVINFO;

    if (ioctl(sock, SIOCETHTOOL, &ifr) == -1)
    {
        LOG_WARN(HCL, "Failed getting driver version on: {}", net_if->ifa_name);
        close(sock);
        return false;
    }

    std::string net_if_drv(drvinfo.driver);

    if (net_if_drv.find(habDrv.c_str()) != std::string::npos)
    {
        close(sock);
        return false;
    }

    close(sock);

    return true;
}

void parse_user_tcp_ifs(std::string ifs_list, std::vector<std::string>& parsed_ifs_list)
{
    std::string delimiter = ",";
    size_t      pos       = 0;

    if (ifs_list.size() == 0)
    {
        return;
    }

    LOG_DEBUG(HCL, "User required interfaces ({})", ifs_list);

    while ((pos = ifs_list.find(delimiter)) != std::string::npos)
    {
        parsed_ifs_list.push_back(ifs_list.substr(0, pos));
        ifs_list.erase(0, pos + delimiter.length());
    }

    if (!ifs_list.empty())
    {
        parsed_ifs_list.push_back(ifs_list);
    }

    return;
}

int detect_tcp_ifs(std::vector<detected_tcp_if>& detected_tcp_ifs)
{
    auto desired_tcp_if = get_desired_tcp_if_from_env_var();

    std::vector<std::string> parsed_ifs_prefix_list;
    parse_user_tcp_ifs(desired_tcp_if, parsed_ifs_prefix_list);

    // Retrieve and iterate over all network interfaces.
    ifaddrs*    net_ifs {};
    std::string net_if_name;
    std::string ip_addr;

    if (!getifaddrs(&net_ifs))
    {
        for (auto* net_if = net_ifs; net_if != nullptr; net_if = net_if->ifa_next)
        {
            // Discard interfaces other than AF_INET.
            if (net_if->ifa_addr == nullptr || net_if->ifa_addr->sa_family != AF_INET) continue;

            net_if_name = std::string {net_if->ifa_name};

            if (!desired_tcp_if.empty())
            {
                if (match_tcp_if_pattern(net_if_name, parsed_ifs_prefix_list) && verify_useable_if(net_if) == true)
                {
                    ip_addr = std::string {inet_ntoa(reinterpret_cast<const sockaddr_in*>(net_if->ifa_addr)->sin_addr)};
                    detected_tcp_ifs.push_back(detected_tcp_if {net_if_name, ip_addr});
                    LOG_DEBUG(HCL, "Detected if={}, ip_addr={} as potential interface", net_if_name, ip_addr);
                }
            }
            else
            {
                // If the desired network interface is not explicitly specified by the user,
                // choose the interfaces which is neither loopback nor docker nor tunneling nw if.
                if (match_tcp_if_pattern(net_if_name, "lo")) continue;
                if (match_tcp_if_pattern(net_if_name, "docker")) continue;
                if (match_tcp_if_pattern(net_if_name, "tunl")) continue;

                if (verify_useable_if(net_if) == true)
                {
                    ip_addr = std::string {inet_ntoa(reinterpret_cast<const sockaddr_in*>(net_if->ifa_addr)->sin_addr)};
                    detected_tcp_ifs.push_back(detected_tcp_if {net_if_name, ip_addr});
                    LOG_DEBUG(HCL, "Detected if={}, ip_addr={} as potential interface", net_if_name, ip_addr);
                }
            }
        }
    }
    else
    {
        LOG_ERR(HCL, "Unable to retrieve network interfaces");
    }

    if (detected_tcp_ifs.size() == 0)
    {
        std::string desired("");
        if (!desired_tcp_if.empty())
        {
            desired = "in HCCL_SOCKET_IFNAME(" + desired_tcp_if + ")";
        }
        VERIFY(false, "No network interfaces found {}", desired);
    }

    freeifaddrs(net_ifs);

    return detected_tcp_ifs.size();
}

detected_tcp_if detect_tcp_if()
{
    std::vector<detected_tcp_if> detected_tcp_ifs;
    detect_tcp_ifs(detected_tcp_ifs);

    LOG_DEBUG(HCL,
              "Using network interface: {} (IP: {})",
              detected_tcp_ifs.front().if_name,
              detected_tcp_ifs.front().ip_addr);

    return detected_tcp_ifs.front();
}

std::string address_to_string(const sockaddr_storage* addr)
{
    std::string out {};

    std::vector<char> address_presentation;

    if (AF_INET == addr->ss_family)
    {
        const sockaddr_in* client_in = reinterpret_cast<const sockaddr_in*>(addr);
        address_presentation.resize(INET_ADDRSTRLEN);
        const char* ptr =
            inet_ntop(AF_INET, (&client_in->sin_addr), address_presentation.data(), address_presentation.size());
        VERIFY(ptr == address_presentation.data(), "inet_ntop(AF_INET, ...) returned invalid pointer");
    }
    if (AF_INET6 == addr->ss_family)
    {
        const sockaddr_in6* client_in6 = reinterpret_cast<const sockaddr_in6*>(addr);
        address_presentation.resize(INET6_ADDRSTRLEN);
        const char* ptr =
            inet_ntop(AF_INET6, (&client_in6->sin6_addr), address_presentation.data(), address_presentation.size());
        VERIFY(ptr == address_presentation.data(), "inet_ntop(AF_INET6, ...) returned invalid pointer");
    }
    return std::string(address_presentation.data());
}

std::string get_global_comm_id()
{
    return GCFG_HCCL_COMM_ID.value();
}

std::string get_global_comm_ip()
{
    std::string ip_and_port_str(get_global_comm_id());
    unsigned    endOfIPAddrss = ip_and_port_str.find_last_of(":");
    std::string ip            = ip_and_port_str.substr(0, endOfIPAddrss);
    return ip;
}

int get_global_comm_port()
{
    std::string ip_and_port_str(get_global_comm_id());
    unsigned    endOfIPAddrss = ip_and_port_str.find_last_of(":");
    int         port          = std::stoi(ip_and_port_str.substr(endOfIPAddrss + 1));
    return port;
}

bool ip_is_local(const std::string ip)
{
    struct ifaddrs *ifap, *ifa;

    getifaddrs(&ifap);

    for (ifa = ifap; ifa; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in* sa   = (struct sockaddr_in*)ifa->ifa_addr;
            char*               addr = inet_ntoa(sa->sin_addr);

            if (std::string(addr) == ip)
            {
                freeifaddrs(ifap);
                return true;
            }
        }
        else  // AF_INET6
        {
            char addr[50];

            struct sockaddr_in6* in6 = (struct sockaddr_in6*)ifa->ifa_addr;
            inet_ntop(AF_INET6, &in6->sin6_addr, addr, sizeof(addr));

            if (std::string(addr) == ip)
            {
                freeifaddrs(ifap);
                return true;
            }
        }
    }

    freeifaddrs(ifap);

    return false;
}

int recv_all(int sockfd, void* buffer, size_t length)
{
    VERIFY(buffer != nullptr, "Invalid buffer");
    VERIFY(length > 0, "Invalid length=0");

    const auto start_time           = std::chrono::high_resolution_clock::now();
    size_t     total_bytes_received = 0;
    bool       warn_logged          = false;

    while (true)
    {
        const auto bytes_left  = length - total_bytes_received;
        const auto recv_result = recv(sockfd, reinterpret_cast<uint8_t*>(buffer) + total_bytes_received, bytes_left, 0);

        if (recv_result == 0)
        {
            if (total_bytes_received == 0)
            {
                LOG_DEBUG(HCL, "socket recv: Trying to receive from a closed socket.");
                return 0;
            }
            else
            {
                LOG_ERR(HCL,
                        "socket recv: Remote peer unexpectedly closed connection while I was waiting for a "
                        "blocking message.");
                return ENODATA;
            }
        }
        else if (recv_result < 0)
        {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                LOG_ERR(HCL, "socket recv: {}", strerror(errno));
                return recv_result;
            }
        }
        else
        {
            // recv_result contains number of bytes received.
            VERIFY(recv_result > 0 && static_cast<size_t>(recv_result) <= bytes_left,
                   "recv returned invalid result={}, bytes_left={}",
                   recv_result,
                   bytes_left);
            total_bytes_received += recv_result;
        }

        if (total_bytes_received == length)
        {
            break;
        }
        else
        {
            const auto total_call_duration = std::chrono::high_resolution_clock::now() - start_time;
            if (total_call_duration > MAX_RECV_TIMEOUT)
            {
                LOG_ERR(HCL,
                        "socket recv: Could not receive the complete message due to timeout: bytes received {} "
                        "out of expected {}",
                        total_bytes_received,
                        length);
                return ETIME;
            }
            else if (!warn_logged && total_call_duration > MAX_RECV_WARN_TIME)
            {
                LOG_WARN(HCL, "socket recv: Still waiting for receiving a complete message.");
                warn_logged = true;
            }
        }
    }

    LOG_DEBUG(HCL, "socket recv: Successfully received {} bytes.", total_bytes_received);
    VERIFY(total_bytes_received <= std::numeric_limits<int>::max(),
           "Invalid total_bytes_received={}",
           total_bytes_received);
    return static_cast<int>(total_bytes_received);
}
