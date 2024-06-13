#include "hcl_sockaddr.h"
#include "hcl_utils.h"

sockaddr_str_t& sockaddr_str_t::set(const sockaddr_storage& address)
{
    char        str_addr[INET6_ADDRSTRLEN] = {};
    const char* ptr                        = nullptr;
    in_port_t   port                       = 0;

    if (AF_INET == address.ss_family)
    {
        struct sockaddr_in* addr = (struct sockaddr_in*)&address;
        ptr                      = inet_ntop(AF_INET, (&addr->sin_addr), str_addr, sizeof(str_addr));
        port                     = ntohs(addr->sin_port);
    }
    else if (AF_INET6 == address.ss_family)
    {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&address;
        ptr                       = inet_ntop(AF_INET6, (&addr->sin6_addr), str_addr, sizeof(str_addr));
        port                      = ntohs(addr->sin6_port);
    }

    if (ptr)
    {
        m_str = std::string(ptr) + ":" + std::to_string(port);
    }
    else
    {
        m_str = "";
        LOG_HCL_ERR(HCL, "Invalid sockaddr_storage provided");
    }

    return *this;
}

sockaddr_t::sockaddr_t(const sockaddr_storage& addr)
{
    fromAddress(addr);
}

sockaddr_t& sockaddr_t::operator=(const sockaddr_storage& addr)
{
    fromAddress(addr);
    return *this;
}

void sockaddr_t::fromAddress(const sockaddr_storage& address)
{
    sockaddr_str_t sas(address);

    if (AF_INET == address.ss_family)
    {
        struct sockaddr_in* addr = (struct sockaddr_in*)&address;
        *sa4_                    = *addr;
        m_sockAddr.ss_family     = AF_INET;
    }

    if (AF_INET6 == address.ss_family)
    {
        struct sockaddr_in6* addr = (struct sockaddr_in6*)&address;
        *sa6_                     = *addr;
        m_sockAddr.ss_family      = AF_INET6;
    }

    m_ips = sas;
}


sockaddr_t::sockaddr_t(const std::string& ipaddress, uint16_t _port)
{
    fromString(ipaddress);
    port(_port);
}

void sockaddr_t::fromString(const std::string& ipaddress)
{
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
    {  // invalid address
        VERIFY(false, "Invalid IP address specified: {}", ipaddress);
    }
}

socklen_t sockaddr_t::size_of() const
{
    return IPv4() ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
}

uint16_t sockaddr_t::port() const
{
    return IPv4() ? ntohs(sa4_->sin_port) : ntohs(sa6_->sin6_port);
}

void sockaddr_t::port(uint16_t _port)
{
    IPv4() ? sa4_->sin_port = htons(_port) : sa6_->sin6_port = htons(_port);
}
