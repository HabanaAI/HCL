#include "fault_injection_tcp_server.h"

#include <thread>
#include <cstring>  // for strerror, memset
#include <unistd.h>
#include <sys/socket.h>  // for socket functions
#include <cstdint>       // for uint*
#include <string>

#include "hcl_utils.h"                        // for LOG_*, VERIFY
#include "platform/gen2_arch_common/types.h"  // for MAX_NICS_GEN2ARCH
#include "fault_tolerance_inc.h"              // for HLFT.* macros

FaultInjectionTcpServer::FaultInjectionTcpServer(const uint32_t moduleId,
                                                 const uint32_t numScaleoutPorts,
                                                 const uint32_t baseServerPort)
: m_moduleId(moduleId), m_numScaleoutPorts(numScaleoutPorts), m_baseServerPort(baseServerPort)
{
    HLFT_DBG("moduleId={}, numScaleoutPorts={}, baseServerPort={}", moduleId, numScaleoutPorts, baseServerPort);
}

FaultInjectionTcpServer::~FaultInjectionTcpServer()
{
    stop();
}

// Starts the server and the listening thread
void FaultInjectionTcpServer::start()
{
    m_port = m_baseServerPort + m_moduleId;
    HLFT_INF("Initial port {}, waiting for new port", m_port);
    m_isRunning    = true;
    m_serverThread = std::thread(&FaultInjectionTcpServer::run, this);
}

// Stops the server and joins the listening thread
void FaultInjectionTcpServer::stop()
{
    HLFT_DBG("Called");
    m_isRunning = false;
    if (m_port > 0)  // assure start was called
    {
        HLFT_INF("Stopping threads with port {}", m_port);
        if (m_serverThread.joinable())
        {
            m_serverThread.join();
            HLFT_INF("Server thread stopped with port {}", m_port);
        }

        if (m_serverSocket > 0)
        {
            close(m_serverSocket);
        }
        HLFT_INF("Stopped");
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

// This functions runs for multiple TCP client threads
void FaultInjectionTcpServer::processClientCommands(const int clientSocket)
{
    char buffer[1024];

    while (m_isRunning)  // Allow external to stop the client thread
    {
        std::memset(buffer, 0, sizeof(buffer));

        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Short delay to avoid busy loop
        const ssize_t bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesReceived > 0)
        {
            HLFT_INF("Client {} connected port {} received {} bytes, buffer={}",
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
                    HLFT_WRN("Invalid scaleout port, client {} connected port {} received {} bytes, "
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
                        HLFT_WRN("Invalid action, client {} connected port {} received {} bytes, "
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
                    HLFT_WRN("Invalid nic number, client {} connected port {} received {} bytes, "
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
                        HLFT_WRN("Invalid action, client {} connected port {} received {} bytes, "
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
                        HLFT_WRN("Invalid apiTarget, client {} connected port {} received {} bytes, "
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
                        HLFT_WRN("Invalid apiTarget, client {} connected port {} received {} bytes, "
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
                    HLFT_WRN("Invalid action, client {} connected port {} received {} bytes, "
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
                HLFT_WRN("Invalid command, client {} connected port {} received {} bytes, command={}",
                         clientSocket,
                         m_port,
                         bytesReceived,
                         command);
            }
        }
        else if (bytesReceived == 0)
        {
            // Client closed the connection
            HLFT_INF("Client {} disconnected port {}", clientSocket, m_port);
            break;
        }
        else
        {
            // Other errors - cant process client anymore
            HLFT_WRN("Error receiving data or client {} disconnected on port {}", clientSocket, m_port);
            break;
        }
    }  // of inner loop while m_isRunning
    close(clientSocket);
    HLFT_INF("Closing Client {}, port {}", clientSocket, m_port);
}

// Method that runs on a separate thread
void FaultInjectionTcpServer::run()
{
    // We don't know what port is free and can be used by the server. Let the user pick the port.
    // Poll the port until the user sets it and then use this port
    static const int tries = 100;
    int              cnt   = tries;

    while (cnt-- > 0)
    {
        if ((int)GCFG_HCL_FAULT_INJECT_LISTENER_PORT.value() != 0)
        {
            m_port = (int)GCFG_HCL_FAULT_INJECT_LISTENER_PORT.value();
            HLFT_INF("Using port {}", m_port);
            break;
        }
        HLFT_INF("Waiting for port to be set {} {}", m_port, GCFG_HCL_FAULT_INJECT_LISTENER_PORT.value());
        sleep(1);
    }
    if (cnt == 0)
    {
        HLFT_ERR("Port was not set after {} tries", tries);
        return;
    }

    HLFT_INF("Running with port {}", m_port);

    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    VERIFY(m_serverSocket > 0, "Unable to create socket, port={}", m_port);

    const int opt    = 1;
    const int setRtn = setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (setRtn < 0)
    {
        LOG_HCL_ERR(HCL_FAILOVER,
                    "Unable to set socket option sock {} errno {} {}",
                    m_serverSocket,
                    errno,
                    strerror(errno));
    }

    sockaddr_in serverAddr {};

    serverAddr.sin_family      = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port        = htons(m_port);

    int rc;
    // Give the server time to bind
    for (int i = 0; i < 3; i++)
    {
        rc = bind(m_serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
        if (rc < 0)
        {
            HLFT_INF("{} Unable to bind socket, port={}, rc={} errno {} {}", i, m_port, rc, errno, strerror(errno));
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
        HLFT_INF("Failed to bind port {}. result/output of {}: {} {}", m_port, cmd, ok, res);
    }

    VERIFY(rc >= 0, "Unable to bind socket, port={}, rc={} errno {} {}", m_port, rc, errno, strerror(errno));
    HLFT_INF("Bind done with port {}", m_port);

    static constexpr int listenTimeout = 2;  // secs
    rc                                 = listen(m_serverSocket, listenTimeout);
    VERIFY(rc >= 0, "Unable to listen on socket, port={}, rc={}", m_port, rc);

    HLFT_INF("Server {} listening on port {}", m_serverSocket, m_port);

    while (m_isRunning)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Give the server time to process
        LOG_HCL_DEBUG(HCL, "Server {} after sleep on port {}", m_serverSocket, m_port);

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
            HLFT_WRN("Server {} in select loop on port {} got select error", m_serverSocket, m_port);
            break;
        }

        LOG_HCL_DEBUG(HCL, "Server {} after select on port {}, activity={}", m_serverSocket, m_port, activity);

        if (activity > 0 && FD_ISSET(m_serverSocket, &readfds))
        {
            HLFT_DBG("Server {} before accept on port {}", m_serverSocket, m_port);
            sockaddr_in clientAddr;
            socklen_t   clientLen    = sizeof(clientAddr);
            const int   clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket < 0)
            {
                HLFT_WRN("Error accepting connection on port {}", m_port);
                continue;
            }

            HLFT_INF("Client {} connected port {}", clientSocket, m_port);

            // Handle client communication in a separate thread, while the server continues to listen for new
            // connections
            std::thread clientThread([clientSocket, this]() { processClientCommands(clientSocket); });
            clientThread.detach();
            HLFT_INF("Accept was successful - Client {}, port {}", clientSocket, m_port);
        }  // accept done
        // If `select()` times out and there's no activity, loop continues.
    }  // end of  outer loop while m_isRunning

    HLFT_INF("Server {} stopped listening on port {}", m_serverSocket, m_port);
}
