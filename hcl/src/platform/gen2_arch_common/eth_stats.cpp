#include "platform/gen2_arch_common/eth_stats.hpp"

#include <cstring>
#include <ifaddrs.h>  // for freeifaddrs, ifaddrs, getifa...
#include <net/if.h>
#include <linux/ethtool.h>  // for ethtool_drvinfo, ETHTOOL_GDR...
#include <linux/sockios.h>  // for SIOCETHTOOL
#include <sys/ioctl.h>      // for ioctl
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>

#include "hcl_log_manager.h"

EthStats::~EthStats()
{
    auto logger = hl_logger::getLogger(hcl::LogManager::LogType::HCL);
    dump(logger, false);
}

static int getPortNum(std::string name)
{
    int devPort = -1;

    std::string fileName = "/sys/class/net/" + name + "/dev_port";

    std::ifstream devPortFile(fileName);

    if (!devPortFile.is_open())
    {
        LOG_ERR(HCL, "Couldn't get port number for {}", fileName);
        return -1;
    }

    std::stringstream devPortStr;

    devPortStr << devPortFile.rdbuf();

    devPortFile.close();

    devPortStr >> devPort;

    return devPort;
}

void EthStats::getHabanaInterfaces(std::string pciAddr)
{
    ifaddrs*    netIfs {};
    std::string ifName;

    int rtn = getifaddrs(&netIfs);

    if (rtn)
    {
        LOG_ERR(HCL, "Unable to retrieve network interfaces with rtn {} errno {} {}", rtn, errno, strerror(errno));
        return;
    }

    for (auto* oneNetIf = netIfs; oneNetIf != nullptr; oneNetIf = oneNetIf->ifa_next)
    {
        ifName = std::string {oneNetIf->ifa_name};

        if (any_of(m_habanaInterfaces.begin(), m_habanaInterfaces.end(), [&](const InterfaceInfo& info) {
                return info.ifName == ifName;
            }))
        {
            continue;
        }

        struct ethtool_drvinfo drvinfo;
        std::string            habDrv = "habanalabs";
        struct ifreq           ifr;
        int                    sockFd;

        sockFd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sockFd == -1)
        {
            LOG_WARN(HCL, "Failed opening socket for driver info on: {}", ifName);
            continue;
        }

        strcpy(ifr.ifr_name, ifName.c_str());

        ifr.ifr_data = (char*)&drvinfo;
        drvinfo.cmd  = ETHTOOL_GDRVINFO;

        if (ioctl(sockFd, SIOCETHTOOL, &ifr) == -1)
        {
            // This is expected fo lo interface
            close(sockFd);
            continue;
        }

        std::string netIfDrv(drvinfo.driver);

        if (netIfDrv.find(habDrv.c_str()) == std::string::npos)
        {
            // not an habana driver
            close(sockFd);
            continue;
        }
        close(sockFd);

        if (pciAddr != drvinfo.bus_info)
        {
            // not a port of this device
            close(sockFd);
            continue;
        }

        int port = getPortNum(ifName);

        InterfaceInfo interfaceInfo {.ifName = ifName, .numStats = drvinfo.n_stats, .port = port};
        m_habanaInterfaces.push_back(interfaceInfo);
    }

    if (m_habanaInterfaces.empty())
    {
        LOG_INFO(HCL, "No network interfaces found");
    }

    freeifaddrs(netIfs);
}

std::vector<std::string> EthStats::getStatsNames(const InterfaceInfo& interfaceInfo)
{
    uint32_t numStats = interfaceInfo.numStats;

    std::vector<std::string> rtn(numStats);  // resize vector to size, fill with max

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd == -1)
    {
        LOG_TRACE(HCL, "Could not open socket");
        return rtn;
    }

    // get needed size for strings
    size_t gstringsSize = sizeof(ethtool_gstrings) + ETH_GSTRING_LEN * numStats;

    std::vector<uint8_t> stringsVec(gstringsSize);  // create memory for ethtool_gstrings with numStats array
    ethtool_gstrings*    strings = reinterpret_cast<ethtool_gstrings*>(stringsVec.data());

    strings->cmd        = ETHTOOL_GSTRINGS;
    strings->string_set = ETH_SS_STATS;
    strings->len        = numStats;

    ifreq ifr;

    ifr.ifr_data = (char*)strings;
    strncpy(ifr.ifr_name, interfaceInfo.ifName.c_str(), IFNAMSIZ - 1);

    int ioctlRtn = ioctl(fd, SIOCETHTOOL, &ifr);
    if (ioctlRtn != 0)
    {
        LOG_ERR(HCL, "Could not get ioctl ETHTOOL_GSTRINGS with rtn {}", ioctlRtn);
        close(fd);
        return rtn;
    }

    for (unsigned i = 0; i < numStats; i++)
    {
        char charArray[ETH_GSTRING_LEN + 1] = {};
        memcpy(charArray, &strings->data[i * ETH_GSTRING_LEN], ETH_GSTRING_LEN);

        rtn[i] = std::string(charArray, strlen(charArray));
    }
    close(fd);
    return rtn;
}

std::vector<uint64_t> EthStats::getStats(const InterfaceInfo& interfaceInfo)
{
    uint32_t numStats = interfaceInfo.numStats;

    std::vector<uint64_t> rtn(numStats, std::numeric_limits<uint64_t>::max());  // resize vector to size, fill with max

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd == -1)
    {
        LOG_TRACE(HCL, "Could not open socket");
        return rtn;
    }

    int ethtool_stats_size = sizeof(ethtool_stats) + sizeof(ethtool_stats::data[0]) * numStats;

    std::vector<uint8_t> statsVec(ethtool_stats_size, 0);
    ethtool_stats*       stats = reinterpret_cast<ethtool_stats*>(statsVec.data());

    stats->cmd     = ETHTOOL_GSTATS;
    stats->n_stats = 0;

    ifreq ifr;

    ifr.ifr_data = (char*)stats;
    strncpy(ifr.ifr_name, interfaceInfo.ifName.c_str(), IFNAMSIZ - 1);

    int ioctlRtn = ioctl(fd, SIOCETHTOOL, &ifr);
    if (ioctlRtn != 0)
    {
        LOG_ERR(HCL, "Could not get ioctl ETHTOOL_GSTATS with rtn {}", ioctlRtn);
        close(fd);
        return rtn;
    }

    for (unsigned i = 0; i < numStats; i++)
    {
        rtn[i] = stats->data[i];
    }
    close(fd);
    return rtn;
}

void EthStats::init(const char* pciAddr)
{
    getHabanaInterfaces(pciAddr);
    LOG_INFO(HCL, "Found {} habana interfaces", m_habanaInterfaces.size());

    // fill the stats names and stats
    for (auto& singleIf : m_habanaInterfaces)
    {
        singleIf.statsNames = getStatsNames(singleIf);
        singleIf.statsVal   = getStats(singleIf);

        if ((singleIf.numStats != singleIf.statsNames.size()) || (singleIf.numStats != singleIf.statsVal.size()))
        {
            LOG_ERR(HCL,
                    "numStats {} should be same as statNames {} and statsVal {}",
                    singleIf.numStats,
                    singleIf.statsNames.size(),
                    singleIf.statsVal.size());
            continue;
        }
    }
}

void EthStats::dump(hl_logger::LoggerSPtr usrLogger, bool dumpAll)
{
    HLLOG_UNTYPED(usrLogger,
                  HLLOG_LEVEL_INFO,
                  "----------------- interface counters (for {} interfaces) -------------",
                  m_habanaInterfaces.size());
    for (auto& singleIf : m_habanaInterfaces)
    {
        std::vector<uint64_t> statsVal = getStats(singleIf);

        if ((singleIf.numStats != singleIf.statsNames.size()) || (singleIf.numStats != singleIf.statsVal.size()) ||
            (singleIf.numStats != statsVal.size()))
        {
            HLLOG_UNTYPED(usrLogger,
                          HLLOG_LEVEL_ERROR,
                          "if {}, not all vectors are the same {} {} {}",
                          singleIf.ifName,
                          singleIf.statsNames.size(),
                          statsVal.size(),
                          singleIf.statsVal.size());
            continue;
        }

        HLLOG_UNTYPED(usrLogger,
                      HLLOG_LEVEL_INFO,
                      "---------------------------------------------------"
                      "---------------------------------------------------");
        HLLOG_UNTYPED(usrLogger,
                      HLLOG_LEVEL_INFO,
                      "     if         :             name                 : "
                      "   val start    :     val now     :      diff      ");
        HLLOG_UNTYPED(usrLogger,
                      HLLOG_LEVEL_INFO,
                      "---------------------------------------------------"
                      "---------------------------------------------------");

        if (singleIf.numStats == 0)
        {
            HLLOG_UNTYPED(usrLogger, HLLOG_LEVEL_INFO, "No stats for {}/{}", singleIf.ifName, singleIf.port);
        }
        for (unsigned i = 0; i < singleIf.numStats; i++)
        {
            std::string& statName = singleIf.statsNames[i];

            bool shouldDump = dumpAll || statName.find("spmu") != std::string::npos ||
                              statName.find("PAUSEFramesReceived") != std::string::npos;

            if (!shouldDump) continue;

            std::string diffStr;
            uint64_t    diff = statsVal[i] - singleIf.statsVal[i];

            if (statsVal[i] >= singleIf.statsVal[i])
            {
                diffStr = std::to_string(diff);
            }
            else
            {
                diffStr = "???";
            }

            std::string marker = (diff != 0) ? "<---" : "    ";

            HLLOG_UNTYPED(usrLogger,
                          HLLOG_LEVEL_INFO,
                          "{:12} {:2} : {:32} : {:15} : {:15} : {:15} {} interface_counters",
                          singleIf.ifName,
                          singleIf.port,
                          statName,
                          singleIf.statsVal[i],
                          statsVal[i],
                          diffStr,
                          marker);
        }
    }
}

std::vector<std::vector<uint64_t>> EthStats::getEthStatsVal()
{
    std::vector<std::vector<uint64_t>> rtn;

    for (const auto& interface : m_habanaInterfaces)
    {
        rtn.push_back(getStats(interface));
    }

    return rtn;
}
