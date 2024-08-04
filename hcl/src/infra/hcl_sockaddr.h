#pragma once

#include <netinet/in.h>                 // for sockaddr_in, sockaddr_in6
#include <arpa/inet.h>                  // for inet_ntoa, inet_ntop, inet_pton
#include <string>

// automatic IPv4/v6 handling of sockaddr_*

class sockaddr_str_t
{
public:
    sockaddr_str_t(const sockaddr_storage& address) { set(address); }
    sockaddr_str_t& operator=(const sockaddr_storage& address) { return set(address); }

    operator const std::string& () const { return m_str; }
private:
    sockaddr_str_t& set(const sockaddr_storage& address);

    std::string m_str;
};

class sockaddr_t
{
public:
    sockaddr_t(const sockaddr_t& addr);
    sockaddr_t(const sockaddr_storage& addr);
    sockaddr_t(const std::string& ipaddress = "", uint16_t _port = 0);

    sockaddr_t& operator=(const sockaddr_t& addr);
    sockaddr_t& operator=(const sockaddr_storage& addr);
    sockaddr_t& operator=(const std::string& ipaddress);

    operator const sockaddr* () const { return sa_; }
    operator sockaddr* () {  return sa_; }
    operator const sockaddr_storage& () const { return m_sockAddr; }
    operator sa_family_t() const { return m_sockAddr.ss_family; }
    operator socklen_t() const { return size_of(); }

    std::string str() const;
    operator std::string() const { return str(); }
    in_port_t port() const;
    socklen_t size_of() const;

private:
    sockaddr_storage m_sockAddr = {};

    sockaddr*     sa_  = (sockaddr*)&m_sockAddr;
    sockaddr_in*  sa4_ = (sockaddr_in*)&m_sockAddr;
    sockaddr_in6* sa6_ = (sockaddr_in6*)&m_sockAddr;

    bool IPv4() const { return m_sockAddr.ss_family == AF_INET; }
    void port(in_port_t _port);

    void fromString(const std::string& ipaddress);
};
