#include "hcl_tcp_utils.h"

#include <arpa/inet.h>        // for inet_pton
#include <cerrno>             // for errno, EINTR
#include <netinet/in.h>       // for sockaddr_in, htons, in_port_t
#include <sys/socket.h>       // for setsockopt, socket, AF_INET
#include <unistd.h>           // for close, read, sleep
#include <cstring>            // for strerror, memset, size_t
#include <iostream>           // for basic_ostream::operator<<
#include <string>             // for operator!=, string, basic_st...
#include "hcl_global_conf.h"  // for GCFG_HCCL_TRIALS
#include "hcl_utils.h"        // for VERIFY
#include "hcl_log_manager.h"  // for LOG_ERR, LOG_DEBUG

#include <sys/types.h>
#include <dirent.h>

#define SERVER_SOCKET_MAX_CONNECTIONS 5120
#define SYS_CALL_ERR                  -1

#define RETURN_SYS_FAILURE(name)                                                                                       \
    return SYS_CALL_ERR;                                                                                               \
    //        LOG_ERR(HCL, "{}: " name "failed with errno - {}", __FUNCTION__, std::strerror(errno));

#define CREATE_SOCKET_SYS_CALL(call, name, socket_fd)                                                                  \
    {                                                                                                                  \
        int ret;                                                                                                       \
        ret = call;                                                                                                    \
        if (ret == SYS_CALL_ERR)                                                                                       \
        {                                                                                                              \
            LOG_ERR(HCL, "{} - failed with error: {}", name, std::strerror(errno));                                    \
            close(socket_fd);                                                                                          \
            return SYS_CALL_ERR;                                                                                       \
        }                                                                                                              \
    }

enum HcclSocketOperations
{
    HCCL_SOCKET_SEND = 0,
    HCCL_SOCKET_RECV,
};

int createServerSocket(sockaddr_t& address)
{
    in_port_t port = address.port();

    int socket_fd = socket(address, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        LOG_ERR(HCL, "Failed to open a socket");
        RETURN_SYS_FAILURE("socket")
    }

    int socket_opt = 1;
    CREATE_SOCKET_SYS_CALL(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof(socket_opt)),
                           "setsockopt",
                           socket_fd)

    CREATE_SOCKET_SYS_CALL(bind(socket_fd, address, address.size_of()), "bind", socket_fd);

    struct sockaddr_storage open_socket_addr = {};

    socklen_t addr_len = sizeof(open_socket_addr);
    CREATE_SOCKET_SYS_CALL(getsockname(socket_fd, (struct sockaddr*)&open_socket_addr, &addr_len),
                           "getsockname",
                           socket_fd);

    sockaddr_t open_sock(open_socket_addr);
    in_port_t  opened_port = open_sock.port();

    if (port == 0)
    {
        address = open_socket_addr;
        LOG_DEBUG(HCL, "Bound socket to port={}", opened_port);
    }
    else
    {
        if (port != opened_port)
        {
            close(socket_fd);
            LOG_ERR(HCL, "Failed to bind the socket to port: {} | {}", address.str(), open_sock.str());
            return SYS_CALL_ERR;
        }
    }
    CREATE_SOCKET_SYS_CALL(listen(socket_fd, SERVER_SOCKET_MAX_CONNECTIONS), "listen", socket_fd);

    return socket_fd;
}

int socketConnect(sockaddr_t& ip_addr, std::string if_name)
{
    int socket_fd = socket(ip_addr, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        RETURN_SYS_FAILURE("socket")
    }

    if (if_name != "")
    {
        setsockopt(socket_fd, SOL_SOCKET, SO_BINDTODEVICE, if_name.c_str(), if_name.size());
    }

    /* Set the option active */
    int retval;
    int socket_opt = 1;
    retval         = setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &socket_opt, sizeof(socket_opt));
    if (retval != 0)
    {
        LOG_ERR(HCL, "Setting SO_KEEPALIVE socket option failed. Retval: {} errno: {}", retval, std::strerror(errno));
        close(socket_fd);
        return SYS_CALL_ERR;
    }

    int connectionTrials = GCFG_HCCL_TRIALS.value();
    int connectResult    = (-1);
    while (connectionTrials > 0)
    {
        connectResult = connect(socket_fd, ip_addr, ip_addr.size_of());
        if (connectResult == 0) break;
        connectionTrials--;
        LOG_DEBUG(HCL, "Connect to server ended with timeout. ip {}. Trying again.", ip_addr.str());
        sleep(1);
    }
    if (connectResult == -1)
    {
        LOG_ERR(HCL, "Connect to server ended with timeout. ip {}", ip_addr.str());
        close(socket_fd);
        return connectResult;
    }
    return socket_fd;
}

int readXBytes(const int socket, void* buffer, const unsigned int x)
{
    unsigned int bytesRead = 0;
    while (bytesRead < x)
    {
        int result = read(socket, (char*)buffer + bytesRead, x - bytesRead);
        if (result == -1 || result == 0) return result;
        bytesRead += result;
    }

    return bytesRead;
}

/*
 * This function performs send/recv to/from sockets - blocking mode!
 * Return values:
 * - Negative number - an error occurred.
 * - 0               - Receive only, when the socket is closed (remote peer closed the connection).
 * - Positive value  - Will always be passed size (we expect to recv/send all)
 */
size_t socketOp(HcclSocketOperations sockOp, const int socket_fd, const void* buff, const size_t size)
{
    ssize_t dataBytes = 0;
    size_t  offset    = 0;
    while (true)
    {
        if (sockOp == HCCL_SOCKET_SEND)
        {
            dataBytes = send(socket_fd, (char*)buff + offset, size - offset, 0);
        }
        else
        {
            dataBytes = recv(socket_fd, (char*)buff + offset, size - offset, 0);
        }

        if (dataBytes == 0 && sockOp == HCCL_SOCKET_RECV)
        {
            // Connection has been closed.
            LOG_DEBUG(HCL, "Socket={} has been closed", socket_fd);
            return dataBytes;
        }

        if (dataBytes == -1)
        {
            if (errno != EINTR)
            {
                LOG_ERR(HCL, "Socket={} returned with error={}", socket_fd, strerror(errno));
            }

            return dataBytes;
        }

        offset += dataBytes;
        if (offset == size)
        {
            break;
        }
    }

    return offset;
}

int recvFromSocket(const int socket_fd, const void* buff, const size_t size)
{
    VERIFY(socket_fd > 0, "Invalid socket_fd={}", socket_fd);
    VERIFY(buff != nullptr, "Invalid buffer");
    VERIFY(size > 0, "Invalid size=0");

    return socketOp(HCCL_SOCKET_RECV, socket_fd, buff, size);
}

bool recvAllFromSocket(const int socket_fd, const void* buff, const size_t size)
{
    VERIFY(socket_fd > 0, "Invalid socket_fd={}", socket_fd);
    VERIFY(buff != nullptr, "Invalid buffer");
    VERIFY(size > 0, "Invalid size=0");

    size_t bytes_recv = socketOp(HCCL_SOCKET_RECV, socket_fd, buff, size);
    if (bytes_recv != size)
    {
        LOG_ERR(HCL, "recvAllFromSocket: Socket receive failed, expected({}), received({}).", size, bytes_recv);
        return false;
    }

    return true;
}

bool sendAllToSocket(const int socket_fd, const void* buff, const size_t size)
{
    VERIFY(socket_fd > 0, "Invalid socket_fd={}", socket_fd);
    VERIFY(buff != nullptr, "Invalid buffer");
    VERIFY(size > 0, "Invalid size=0");

    size_t bytes_sent = socketOp(HCCL_SOCKET_SEND, socket_fd, buff, size);
    if (bytes_sent != size)
    {
        LOG_ERR(HCL, "Socket send failed.");

        return false;
    }

    return true;
}

/**
 * @brief configure non-blocking mode on socket
 *
 * @param socket - socket to configure
 * @return true on success
 * @return false on failure
 */
bool setNonBlockingSocket(const int socket)
{
    int flags = fcntl(socket, F_GETFL, 0) | O_NONBLOCK;
    if (flags < 0)
    {
        return false;
    }
    if (fcntl(socket, F_SETFL, flags) == SYS_CALL_ERR)
    {
        return false;
    }
    return true;
}

std::string getListenPorts()
{
    char        psBuffer[256];
    FILE*       pPipe;
    std::string result;

    if ((pPipe = popen("netstat -ltnp", "r")) == NULL)
    {
        return result;
    }

    while (fgets(psBuffer, sizeof(psBuffer), pPipe) != NULL)
        result += psBuffer;

    pclose(pPipe);
    return result;
}
