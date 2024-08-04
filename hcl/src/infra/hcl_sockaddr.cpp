#include "hcl_sockaddr.h"
#include "hcl_utils.h"

sockaddr_str_t& sockaddr_str_t::set(const sockaddr_storage& address)
{
    char str_addr[INET6_ADDRSTRLEN] = {};

    const char* ptr  = nullptr;
    in_port_t   port = 0;

    if (AF_INET == address.ss_family)
    {
        sockaddr_in* addr = (sockaddr_in*)&address;
        ptr  = inet_ntop(AF_INET, (&addr->sin_addr), str_addr, sizeof(str_addr));
        port = ntohs(addr->sin_port);
    }
    else if (AF_INET6 == address.ss_family)
    {
        sockaddr_in6* addr = (sockaddr_in6*)&address;
        ptr  = inet_ntop(AF_INET6, (&addr->sin6_addr), str_addr, sizeof(str_addr));
        port = ntohs(addr->sin6_port);
    }

    if (ptr)
    {
        m_str = std::string(ptr) + ":" + std::to_string(port);
    }
    else
    {
        m_str = "invalid ip address";
    }

    return *this;
}

sockaddr_t::sockaddr_t(const sockaddr_t& addr)
{
    m_sockAddr = addr.m_sockAddr;
}

sockaddr_t& sockaddr_t::operator=(const sockaddr_t& addr)
{
    m_sockAddr = addr.m_sockAddr;
    return *this;
}

sockaddr_t::sockaddr_t(const sockaddr_storage& addr)
{
    m_sockAddr = addr;
}

sockaddr_t& sockaddr_t::operator=(const sockaddr_storage& addr)
{
    m_sockAddr = addr;
    return *this;
}

sockaddr_t::sockaddr_t(const std::string& ipaddress, in_port_t _port)
{
    fromString(ipaddress);
    port(_port);
}

sockaddr_t& sockaddr_t::operator=(const std::string& ipaddress)
{
    fromString(ipaddress);
    return *this;
}

void sockaddr_t::fromString(const std::string& ipaddress)
{
    if (ipaddress == "")
    {
        m_sockAddr = {};
        m_sockAddr.ss_family = AF_INET;
        return;
    }

    if (inet_pton(AF_INET, ipaddress.c_str(), &(sa4_->sin_addr)) == 1)
    {
        // IPv4 address
        m_sockAddr.ss_family = AF_INET;
    }
    else if (inet_pton(AF_INET6, ipaddress.c_str(), &(sa6_->sin6_addr)) == 1)
    {
        // IPv6 address
        m_sockAddr.ss_family = AF_INET6;
    }
    else
    {
        VERIFY(false, "invalid ip string: {}", ipaddress);
    }
}

socklen_t sockaddr_t::size_of() const
{
    return IPv4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

in_port_t sockaddr_t::port() const
{
    return IPv4() ? ntohs(sa4_->sin_port) : ntohs(sa6_->sin6_port);
}

void sockaddr_t::port(in_port_t _port)
{
    IPv4() ? sa4_->sin_port = htons(_port) : sa6_->sin6_port = htons(_port);
}

std::string sockaddr_t::str() const
{
    return sockaddr_str_t(m_sockAddr);
}
