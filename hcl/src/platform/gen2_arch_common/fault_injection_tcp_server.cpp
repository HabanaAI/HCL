#include "fault_injection_tcp_server.h"

#include <iostream>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>  // for uint*
#include <string>
#include <thread>

#include "hcl_utils.h"                        // for LOG_*, VERIFY
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH

FaultInjectionTcpServer::FaultInjectionTcpServer(const uint32_t moduleId,
                                                 const uint32_t numScaleoutPorts,
                                                 const uint32_t baseServerPort)
: m_moduleId(moduleId), m_numScaleoutPorts(numScaleoutPorts), m_baseServerPort(baseServerPort)
{
    LOG_HCL_DEBUG(HCL_FAILOVER,
                  "moduleId={}, numScaleoutPorts={}, baseServerPort={}",
                  moduleId,
                  numScaleoutPorts,
                  baseServerPort);
    VERIFY(baseServerPort > 0);
}

FaultInjectionTcpServer::~FaultInjectionTcpServer()
{
    stop();
}

// Starts the server and the listening thread
void FaultInjectionTcpServer::start()
{
    m_port = m_baseServerPort + m_moduleId;
    LOG_HCL_INFO(HCL_FAILOVER, "Request to start with port {}", m_port);
    m_isRunning    = true;
    m_serverThread = std::thread(&FaultInjectionTcpServer::run, this);
}

// Stops the server and joins the listening thread
void FaultInjectionTcpServer::stop()
{
    LOG_HCL_DEBUG(HCL_FAILOVER, "Called");
    m_isRunning = false;
    if (m_port > 0)  // assure start was called
    {
        LOG_HCL_INFO(HCL_FAILOVER, "Stopping threads with port {}", m_port);
        if (m_serverThread.joinable())
        {
            m_serverThread.join();
            LOG_HCL_INFO(HCL_FAILOVER, "Server thread stopped with port {}", m_port);
        }

        if (m_serverSocket > 0)
        {
            close(m_serverSocket);
        }
        LOG_HCL_INFO(HCL_FAILOVER, "Stopped");
    }
}

// This function will execute a given shell command and return the status and output
static std::pair<bool, std::string> execCmd(std::string cmd)
{
    std::array<char, 4096> buffer {};
    std::string            result;

    cmd += " 2>&1";
    std::unique_ptr<FILE, void (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), [](FILE* f) -> void {
        // wrapper to ignore the return value from pclose() is needed with newer versions of gnu g++
        std::ignore = pclose(f);
    });
    if (!pipe)
    {
        LOG_ERR(HCL, "couldn't open pipe for command {} with errno {}", cmd, errno);
        return {false, "couldn't open pipe"};
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }
    return {true, result};
}

// Method that runs on a separate thread
void FaultInjectionTcpServer::run()
{
    LOG_HCL_INFO(HCL_FAILOVER, "Running with port {}", m_port);
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    VERIFY(m_serverSocket > 0, "Unable to create socket, port={}", m_port);

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(m_port);

    int rc;
    // Sometime the port is still open from the previous run, so retry a few times
    for (int i = 0; i < 10; i++)
    {
        rc = bind(m_serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
        if (rc < 0)
        {
            LOG_HCL_INFO(HCL_FAILOVER,
                         "{} Unable to bind socket, port={}, rc={} errno {} {}",
                         i,
                         m_port,
                         rc,
                         errno,
                         strerror(errno));
            sleep(1);
        }
        else
        {
            break;
        }
    }

    if (rc < 0)  // if we still failed after all the tries above
    {
        // dump to log who is still holding it
        std::string cmd = "sudo lsof -i |grep " + std::to_string(m_port);
        auto [ok, res]  = execCmd(cmd);
        LOG_HCL_INFO(HCL_FAILOVER, "Failed to bind port {}. result/output of {}: {}\n{}", m_port, cmd, ok, res);
    }

    VERIFY(rc >= 0, "Unable to bind socket, port={}, rc={} errno {} {}", m_port, rc, errno, strerror(errno));
    LOG_HCL_INFO(HCL_FAILOVER, "Bind done with port {}", m_port);

    static constexpr int listenTimeout = 2;  // secs
    rc                                 = listen(m_serverSocket, listenTimeout);
    VERIFY(rc >= 0, "Unable to listen on socket, port={}, rc={}", m_port, rc);

    LOG_HCL_INFO(HCL_FAILOVER, "Server {} listening on port {}", m_serverSocket, m_port);

    while (m_isRunning)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Give the server time to process
        LOG_HCL_DEBUG(HCL_FAILOVER, "Server {} after sleep on port {}", m_serverSocket, m_port);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(m_serverSocket, &readfds);

        // Set timeout (e.g., 2 seconds)
        struct timeval timeout;
        timeout.tv_sec  = 2;
        timeout.tv_usec = 0;

        // Monitor the socket for readability (incoming connections)
        const int activity = select(m_serverSocket + 1, &readfds, nullptr, nullptr, &timeout);

        if (activity < 0 && m_isRunning)
        {
            LOG_HCL_WARN(HCL_FAILOVER, "Server {} in select loop on port {} got select error", m_serverSocket, m_port);
            break;
        }

        LOG_HCL_DEBUG(HCL_FAILOVER, "Server {} after select on port {}, activity={}", m_serverSocket, m_port, activity);

        if (activity > 0 && FD_ISSET(m_serverSocket, &readfds))
        {
            LOG_HCL_DEBUG(HCL_FAILOVER, "Server {} before accept on port {}", m_serverSocket, m_port);
            sockaddr_in clientAddr;
            socklen_t   clientLen    = sizeof(clientAddr);
            const int   clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket < 0)
            {
                LOG_HCL_WARN(HCL_FAILOVER, "Error accepting connection on port {}", m_port);
                continue;
            }

            LOG_HCL_INFO(HCL_FAILOVER, "Client {} connected port {}", clientSocket, m_port);

            while (m_isRunning)
            {
                char buffer[1024];
                std::memset(buffer, 0, sizeof(buffer));

                const ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
                if (bytesReceived > 0)
                {
                    LOG_HCL_INFO(HCL_FAILOVER,
                                 "Client {} connected port {} received {} bytes, buffer={}",
                                 clientSocket,
                                 m_port,
                                 bytesReceived,
                                 buffer);
                    const std::string receivedStr(buffer);

                    // Parse the command
                    std::istringstream iss(receivedStr);
                    std::string        command;

                    iss >> command;
                    if (command == FAULT_INJECT_SCALEOUT_PORT_CMD)
                    {
                        std::string action;
                        uint16_t    portNum;
                        iss >> portNum >> action;
                        if (portNum > (m_numScaleoutPorts - 1))
                        {
                            LOG_HCL_WARN(HCL_FAILOVER,
                                         "Invalid scaleout port, client {} connected port {} received {} bytes, "
                                         "command={}, portNum={}, action={}",
                                         clientSocket,
                                         m_port,
                                         bytesReceived,
                                         command,
                                         portNum,
                                         action);
                        }
                        else
                        {
                            if (action == FAULT_INJECT_PORT_UP_CMD)
                            {
                                portUp(portNum);
                            }
                            else if (action == FAULT_INJECT_PORT_DOWN_CMD)
                            {
                                portDown(portNum);
                            }
                            else
                            {
                                LOG_HCL_WARN(HCL_FAILOVER,
                                             "Invalid action, client {} connected port {} received {} bytes, "
                                             "command={}, portNum={}, action={}",
                                             clientSocket,
                                             m_port,
                                             bytesReceived,
                                             command,
                                             portNum,
                                             action);
                            }
                        }
                    }
                    else if (command == FAULT_INJECT_NIC_STATUS_CMD)
                    {
                        std::string action;
                        uint16_t    nicNum;
                        iss >> nicNum >> action;
                        if (nicNum > (MAX_NICS_GEN2ARCH - 1))
                        {
                            LOG_HCL_WARN(HCL_FAILOVER,
                                         "Invalid nic number, client {} connected port {} received {} bytes, "
                                         "command={}, nicNum={}, action={}",
                                         clientSocket,
                                         m_port,
                                         bytesReceived,
                                         command,
                                         nicNum,
                                         action);
                        }
                        else
                        {
                            if (action == FAULT_INJECT_NIC_UP_CMD)
                            {
                                nicUp(nicNum);
                            }
                            else if (action == FAULT_INJECT_NIC_DOWN_CMD)
                            {
                                nicDown(nicNum);
                            }
                            else if (action == FAULT_INJECT_NIC_SHUTDOWN_CMD)
                            {
                                nicShutdown(nicNum);
                            }
                            else
                            {
                                LOG_HCL_WARN(HCL_FAILOVER,
                                             "Invalid action, client {} connected port {} received {} bytes, "
                                             "command={}, nicNum={}, action={}",
                                             clientSocket,
                                             m_port,
                                             bytesReceived,
                                             command,
                                             nicNum,
                                             action);
                            }
                        }
                    }
                    else if (command == FAULT_INJECT_API_CMD)
                    {
                        std::string action;
                        std::string apiTarget;
                        iss >> action >> apiTarget;

                        if (action == FAULT_INJECT_API_STOP_CMD)
                        {
                            if (apiTarget == FAULT_INJECT_API_ALL_CMD)
                            {
                                stopAllApi();
                            }
                            else
                            {
                                LOG_HCL_WARN(HCL_FAILOVER,
                                             "Invalid apiTarget, client {} connected port {} received {} bytes, "
                                             "command={}, action={}, apiTarget={}",
                                             clientSocket,
                                             m_port,
                                             bytesReceived,
                                             command,
                                             action,
                                             apiTarget);
                            }
                        }
                        else if (action == FAULT_INJECT_API_RESUME_CMD)
                        {
                            if (apiTarget == FAULT_INJECT_API_ALL_CMD)
                            {
                                resumeAllApi();
                            }
                            else
                            {
                                LOG_HCL_WARN(HCL_FAILOVER,
                                             "Invalid apiTarget, client {} connected port {} received {} bytes, "
                                             "command={}, action={}, apiTarget={}",
                                             clientSocket,
                                             m_port,
                                             bytesReceived,
                                             command,
                                             action,
                                             apiTarget);
                            }
                        }
                        else
                        {
                            LOG_HCL_WARN(HCL_FAILOVER,
                                         "Invalid action, client {} connected port {} received {} bytes, "
                                         "command={}, action={}",
                                         clientSocket,
                                         m_port,
                                         bytesReceived,
                                         command,
                                         action);
                        }
                    }
                    else
                    {
                        LOG_HCL_WARN(HCL_FAILOVER,
                                     "Invalid command, client {} connected port {} received {} bytes, command={}",
                                     clientSocket,
                                     m_port,
                                     bytesReceived,
                                     command);
                    }
                }
                else if (bytesReceived == 0)
                {
                    // Client closed the connection
                    LOG_HCL_INFO(HCL_FAILOVER, "Client {} disconnected port {}", clientSocket, m_port);
                    break;
                }
                else
                {
                    LOG_HCL_WARN(HCL_FAILOVER,
                                 "Error receiving data or client {} disconnected on port {}",
                                 clientSocket,
                                 m_port);
                    break;
                }
            }
            close(clientSocket);
            LOG_HCL_INFO(HCL_FAILOVER, "Closing Client {}, port {}", clientSocket, m_port);
        }
        // If `select()` times out and there's no activity, loop continues.
    }

    LOG_HCL_INFO(HCL_FAILOVER, "Server {} stopped listening on port {}", m_serverSocket, m_port);
}
