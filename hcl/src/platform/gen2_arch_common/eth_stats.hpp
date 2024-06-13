#pragma once

#include <map>
#include <vector>

#include "hl_logger/hllog_core.hpp"

class EthStats
{
public:
    struct InterfaceInfo
    {
        std::string              ifName;
        uint32_t                 numStats;
        int                      port;
        std::vector<std::string> statsNames;
        std::vector<uint64_t>    statsVal;
    };

    ~EthStats();

    void                               init(const char* piAddr);
    void                               dump(hl_logger::LoggerSPtr usrLogger, bool dumpAll);
    const std::vector<InterfaceInfo>&  getInterfaces() const { return m_habanaInterfaces; };
    std::vector<std::vector<uint64_t>> getEthStatsVal();

private:
    void getHabanaInterfaces(std::string pciAddr);

    static std::vector<std::string> getStatsNames(const InterfaceInfo& interfaceInfo);
    static std::vector<uint64_t>    getStats(const InterfaceInfo& interfaceInfo);

    std::vector<InterfaceInfo> m_habanaInterfaces;
};
