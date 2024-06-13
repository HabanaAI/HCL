#pragma once

#include <netinet/in.h>                 // for sockaddr_in, sockaddr_in6
#include <arpa/inet.h>                  // for inet_ntoa, inet_ntop, inet_pton
#include <string>

// automatic IPv4/v6 handling of sockaddr_*

class sockaddr_str_t
{
public:
    sockaddr_str_t() = default;
    sockaddr_str_t(const sockaddr_storage& address) { set(address); }

    sockaddr_str_t& operator=(const sockaddr_storage& address) { return set(address); }
    sockaddr_str_t& set(const sockaddr_storage& address);

    operator const std::string&() const { return m_str; }

private:
    std::string m_str;
};

class sockaddr_t
{
public:
    sockaddr_t() = default;
    sockaddr_t(const sockaddr_storage& addr);
    sockaddr_t(const std::string& ipaddress, uint16_t _port = 0);
    sockaddr_t& operator=(const sockaddr_storage& addr);

    operator const struct sockaddr *() const { return sa_; }
    operator const sockaddr_storage&() const { return m_sockAddr; }
    operator sa_family_t() const { return m_sockAddr.ss_family; }

    const std::string& str() { return m_ips.set(m_sockAddr); }

    operator const std::string&() { return str(); }

    uint16_t  port() const;
    socklen_t size_of() const;

private:
    struct sockaddr_storage m_sockAddr = {};

    struct sockaddr*     sa_  = (struct sockaddr*)&m_sockAddr;
    struct sockaddr_in*  sa4_ = (struct sockaddr_in*)&m_sockAddr;
    struct sockaddr_in6* sa6_ = (struct sockaddr_in6*)&m_sockAddr;

    sockaddr_str_t m_ips;

    bool IPv4() const { return m_sockAddr.ss_family == AF_INET; }
    void port(uint16_t _port);

    void fromString(const std::string& ipaddress);
    void fromAddress(const sockaddr_storage& address);
};
