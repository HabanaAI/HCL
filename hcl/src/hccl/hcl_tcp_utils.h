#pragma once

#include <cstddef>  // for size_t
#include <string>   // for allocator, string

#include "infra/hcl_sockaddr.h"

/*
 * Try to open a new socket with given port.
 * addr_family - address family, AF_INET (IPv4 protocol) or AF_INET6 (IPv6 protocol)
 * port - port to bind. If port is 0, random available port will be used and port value will
 *        be updated accordingly.
 */

int createServerSocket(sockaddr_t& addr);

int socketConnect(sockaddr_t& ip_addr, std::string if_name = "");

int readXBytes(const int socket, void* buffer, const unsigned int x);

int recvFromSocket(const int socket_fd, const void* buff, const size_t size);

bool recvAllFromSocket(const int socket_fd, const void* buff, const size_t size);

bool sendAllToSocket(const int socket_fd, const void* buff, const size_t size);

bool setNonBlockingSocket(const int socket);

std::string getListenPorts();