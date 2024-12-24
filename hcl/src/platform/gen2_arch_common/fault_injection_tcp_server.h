#pragma once

#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <string>
#include <cstdint>  // for uint*

const std::string FAULT_INJECT_SCALEOUT_PORT_CMD = "port";
const std::string FAULT_INJECT_PORT_UP_CMD       = "up";
const std::string FAULT_INJECT_PORT_DOWN_CMD     = "down";

const std::string FAULT_INJECT_NIC_STATUS_CMD   = "nic";
const std::string FAULT_INJECT_NIC_UP_CMD       = "up";
const std::string FAULT_INJECT_NIC_DOWN_CMD     = "down";
const std::string FAULT_INJECT_NIC_SHUTDOWN_CMD = "shutdown";

const std::string FAULT_INJECT_API_CMD        = "api";
const std::string FAULT_INJECT_API_STOP_CMD   = "stop";
const std::string FAULT_INJECT_API_RESUME_CMD = "resume";
const std::string FAULT_INJECT_API_ALL_CMD    = "all";

class FaultInjectionTcpServer
{
public:
    FaultInjectionTcpServer(const uint32_t moduleId, const uint32_t numScaleoutPorts, const uint32_t baseServerPort);
    virtual ~FaultInjectionTcpServer();
    FaultInjectionTcpServer(const FaultInjectionTcpServer&)            = delete;
    FaultInjectionTcpServer& operator=(const FaultInjectionTcpServer&) = delete;

    // Starts the server and the listening thread
    void start();

    // Stops the server and joins the listening thread
    void stop();

    bool isRunning() const { return m_isRunning; }

protected:
    const uint32_t m_moduleId;
    const uint32_t m_numScaleoutPorts;
    const uint32_t m_baseServerPort;

    virtual void portUp(const uint16_t portNum)   = 0;  // Logical scaleout port number
    virtual void portDown(const uint16_t portNum) = 0;  // Logical scaleout port number

    virtual void nicUp(const uint16_t nicNum)       = 0;  // physical scaleup/scaleout nic number
    virtual void nicDown(const uint16_t nicNum)     = 0;  // physical scaleup/scaleout nic number
    virtual void nicShutdown(const uint16_t nicNum) = 0;  // physical scaleup/scaleout nic number

    virtual void stopAllApi()   = 0;
    virtual void resumeAllApi() = 0;

private:
    int         m_port         = -1;
    int         m_serverSocket = -1;
    std::thread m_serverThread;
    bool        m_isRunning = false;

    // Method that runs on a separate thread
    void run();
};
