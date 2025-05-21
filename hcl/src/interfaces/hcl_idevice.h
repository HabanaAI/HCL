#pragma once

#include <cstdint>        // for uint32_t, uint8_t, uint16_t
#include <functional>     // for function
#include <map>            // for map
#include <unordered_map>  // for unordered_map
#include <unordered_set>  // for unordered_set
#include <vector>         // for vector

#include "hcl_api_types.h"                                // for HCL_Comm, HCL_Rank
#include "hccl_types.h"                                   // for hcclResult_t
#include "hcl_config.h"                                   // for HclConfig
#include "platform/gen2_arch_common/hcl_device_config.h"  // for HclDeviceConfig
#include "hcl_dynamic_comms_manager.h"                    // for HclDynamicCommsManager
#include "hcl_hal.h"                                      // for HalPtr
#include "hcl_types.h"                                    // for HclConfigType, eIbvNicPhysicalState
#include "synapse_api_types.h"                            // for synDeviceId, synStreamHandle
#include "synapse_common_types.h"                         // for synDeviceType
#include "hlthunk.h"                                      // for hlthunk_device_name
#include "hcl_nic.h"
#include "infra/hcl_affinity_manager.h"
#include "libfabric/hl_ofi_component.h"
#include "platform/gen2_arch_common/server_connectivity_types.h"  // for DEFAULT_COMM_ID
#include "platform/gen2_arch_common/hcl_device_config.h"          // for HclDeviceConfig

class HclDynamicCommunicator;
class HclEvent;
class UniqueSortedVector;
class ofi_t;

class OfiPlugin;
class HcclHostBufferManager;
class Gen2ArchServerDef;

class IHclDevice
{
public:
    IHclDevice(HclDeviceConfig& deviceConfig);  // Used by Runtime and tests ctor
    virtual ~IHclDevice() noexcept(false);
    IHclDevice(const IHclDevice&)            = delete;
    IHclDevice& operator=(const IHclDevice&) = delete;

    const std::string getDeviceTypeStr() const { return m_deviceConfig.getDeviceTypeStr(); }

    /**
     * @brief enable parametrized destruction
     * to be called before destructor
     *
     */
    virtual void destroy();

    // called by communicator after device has been created and
    // and performed basic setup. put extended initialization/setup code here
    virtual hcclResult_t onNewCommStart(HCL_Comm comm, uint32_t commSize, HclConfig& config);

    virtual hcclResult_t destroyComm(HCL_Comm comm, bool force = false);

    /**
     * get this device's name (gaudi1/gaudi2/gaudi3)
     */
    virtual hlthunk_device_name getDeviceName() = 0;

    /**
     * get this rank Rank ID
     */
    virtual HCL_Rank getMyRank(HCL_Comm comm);

    /**
     * get Rank IDs set of the global communicator
     */
    virtual const UniqueSortedVector& getRanks(HCL_Comm comm);

    /**
     * get logical Rank ID of current device in comm
     */
    virtual HCL_Rank getCommRank(HCL_Comm comm);

    /**
     * get logical Rank ID of specific device device in comm
     */
    virtual HCL_Rank getCommRank(HCL_Comm comm, HCL_Rank rank);

    hcclResult_t connectRankQps(HCL_Comm comm, HCL_Rank rank);

    HCL_Rank getGlobalRankForComm(HCL_Comm comm, HCL_Rank rankID) const;

    /**
     * get dynamic communicator with given id
     */
    virtual HclDynamicCommunicator& getComm(const HCL_Comm comm);

    virtual const HclDynamicCommunicator& getComm(const HCL_Comm comm) const;

    /**
     * get logical Rank IDs of devices in the same ScaleupGroup in comm
     * not including this ranks
     */
    virtual void getInnerRanks(const HCL_Comm comm, UniqueSortedVector& innerRanks);

    /**
     * get logical Rank IDs of peers in other ScaleupGroups in comm
     * not including this ranks
     */
    virtual void getOuterRanks(const HCL_Comm comm, UniqueSortedVector& outerRanks);

    /**
     * get logical Rank IDs of peers in other ScaleupGroups in comm
     * and logical Rank IDs of devices in the same ScaleupGroup in comm
     * not including this ranks
     */
    virtual void getPeerRanks(const HCL_Comm comm, UniqueSortedVector& syncRanks);

    /**
     * get comm size - the number of devices in communicator
     */
    virtual uint32_t getCommSize(HCL_Comm comm);

    /**
     * get DeviceIDs set of HCL_Comm comm
     */
    virtual const UniqueSortedVector& getCommRanks(HCL_Comm comm);

    /**
     * Determine whether communicator exists
     * @return true if communicator exists
     *         false if communicator doesn't exist
     */
    virtual bool isCommExist(HCL_Comm comm);
    size_t       getMaxCommNum() const;

    /**
     * allocate a new communicator. returns allocated comm id.
     */
    virtual HCL_Comm allocateNewComm();

    virtual hcclResult_t networkFlush(HCL_Request* phRequest, synStreamHandle streamHandle);

    virtual hcclResult_t sync(HCL_Comm comm, uint16_t tag);

    virtual void waitForAllEvents(bool isCsDone = true);
    virtual void waitForAllEvents(uint32_t queueOffset, bool isCsDone = true);

    virtual nics_mask_t getActiveNics(HCL_Rank fromRank, HCL_Rank toRank, int physicalQueueOffset, HCL_Comm comm) = 0;

    /**
     * @brief update a specific nic status - UP or DOWN
     *
     * @param nic    - nic num
     * @param event  - NIC state event
     * @param atInit - is this function called at init
     */
    virtual void updateNicState(const uint32_t nic, const NicLkdEventsEnum event, const bool atInit);

    /**
     * @brief check if a nic is up using hl_thunk
     *
     * @param nic - nic to check
     * @return true if nic is up
     * @return false if nic is down
     */
    virtual bool isNicUp(uint32_t nic);  // move to protected

    /**
     * @brief check if a given port is scale-out port
     *
     * @param port - port to check
     * @return true if port is scal-out port
     * @return false otherwise
     */
    virtual bool isScaleOutPort(const uint16_t port, const HCL_Comm comm = DEFAULT_COMM_ID) const = 0;

    HclDeviceConfig&       getDeviceConfig() { return m_deviceConfig; }
    const HclDeviceConfig& getDeviceConfig() const { return m_deviceConfig; }
    int                    getFd() const;
    inline const hcl::Hal& getHal() const { return *m_hal; }  // Avoid copy ctor of shared ptr

    virtual void         openWQs();
    hcclResult_t         openQpToRemoteRanks(const HCL_Comm comm);
    virtual hcclResult_t connectCommQps(const HCL_Comm comm);
    virtual uint8_t      getPeerNic(const HCL_Rank rank, const HCL_Comm comm, const uint8_t port);

    virtual bool isDramAddressValid(uint64_t addr) const = 0;

    virtual bool isCommunicatorInScaleupGroup(HCL_Comm comm);

    virtual bool isCommunicatorScaleupGroupPeers(HCL_Comm comm);

    virtual bool isCommunicatorHierarchical(HCL_Comm comm);

    virtual hcclResult_t prepareAndValidateCommunicator(HCL_Comm comm, bool isLoopbackModeOrNullSubmission);

    virtual int getNumActiveComms() const;

    virtual uint32_t getScaleupGroupSize(HCL_Comm comm);

    virtual unsigned getSenderWqeTableSize()   = 0;
    virtual unsigned getReceiverWqeTableSize() = 0;

    virtual nics_mask_t getNicsStatusMask() const;

    virtual ofi_t*                 getOfiHandle();
    virtual int                    getOfiDeviceId();
    virtual HcclHostBufferManager* getHostBufferManager() { return nullptr; }

    int  getHwModuleId();
    bool isScaleOutAvailable() { return m_scaleoutAvailable; }

    virtual spHclNic allocateNic(uint32_t nic, [[maybe_unused]] uint32_t max_qps)
    {
        return std::make_shared<IHclNic>(this, nic);
    }

    virtual uint32_t createQpnInLKD(HCL_Comm comm, const uint32_t port, const uint8_t qpId) = 0;

    virtual hcclResult_t establishQpConnectionWithPeerQp(const HCL_Comm comm,
                                                         const HCL_Rank rank,
                                                         const uint32_t stream,
                                                         const uint32_t port,
                                                         const uint32_t qpn,
                                                         const uint8_t  qpSet) = 0;

    virtual void destroyQp(HCL_Comm comm, uint32_t port, uint32_t qpn) = 0;

    virtual uint64_t getDRAMSize() { return 0; };
    virtual uint64_t getDRAMBaseAddr() { return 0; };
    virtual bool     isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx)    = 0;
    virtual void     setTraceMarker(const synStreamHandle stream_handle, uint32_t val) = 0;

    ofi_component_t* getOfiComponent() { return m_ofiComponent; }
    // The following is an indication if this device was acquired by synapse successfully and it is then sets to true.
    const bool m_deviceAcquired = false;

    virtual Gen2ArchServerDef&       getServerDef()            = 0;
    virtual const Gen2ArchServerDef& getServerDefConst() const = 0;

    const nics_mask_t getFailedScaleOutPortsMask() const { return m_failedScaleOutPortsMask; }

    virtual void handleFaultToleranceGroupEndApi() {};  // Called by HCL Group End API to handle fault tolerance case
    virtual void faultToleranceNotifyGroupApis() {};    // Notify group end API calls for stop/resume
    virtual void clearScaleoutCommsCurrentGroup() {};   // Called in group start to clear the current group comms
    virtual void addScaleoutCommsCurrentGroup([[maybe_unused]] const HCL_Comm hclCommId) {
    };  // Called while doing API calls inside  group call to add the scaleout comm to current group comms
    virtual void setQpManagersForComm(const HCL_Comm, const size_t commSize) = 0;

protected:
    virtual uint32_t allocateQp(uint32_t port, HCL_Rank rank, HCL_Comm comm, uint8_t qpId, uint8_t qpSet = 0);
    virtual void     setGaudiDirect() {};

    void setHal(hcl::HalPtr ptr);
    void registerOpenQpCallback(HclConfigType configType, std::function<hcclResult_t(HCL_Comm)> callback);
    void createOfiPlugin();
    void setScaleoutMode(const unsigned scaleOutGNICs);
    void initNicsMask();
    void fillMacAddresses(HCL_Comm comm);
    void getMacInfo();
    void readMacInfoDriver();
    void getMacAddressInfo();
    bool readMacInfoFromFile(const char* macAddrInfoFilePath);
    // Until This class is merged with HclDeviceGen2Arch, it is implemented at HclDeviceGen2Arch
    virtual uint16_t getMaxNumScaleUpPortsPerConnection(const HCL_Comm hclCommId = DEFAULT_COMM_ID) const = 0;
    virtual uint16_t getLogicalScaleoutPortNum(
        const uint16_t nic) const = 0;  // Translates scaleout physical nic to scaleout logical port number

    /**
     * @brief get NIC initial physical state via IBV
     *
     * @param nic - nic to check
     * @return eIbvNicPhysicalState - NIC physical state
     */
    virtual const eIbvNicPhysicalState getNicPhysicalState([[maybe_unused]] const uint32_t nic)
    {
        return eIbvNicPhysicalState::Undefined;
    }

    class macaddr_t
    {
    private:
        uint64_t addr_ = 0;

    public:
        operator uint64_t() const { return addr_; }
        macaddr_t& operator=(void* other)
        {
            std::memcpy(&addr_, other, ETH_ALEN);
            return *this;
        }
        macaddr_t& operator=(uint64_t other)
        {
            addr_ = other;
            return *this;
        }
    };

    using nics_map = std::unordered_map<uint8_t, spHclNic>;
    using macs_map = std::unordered_map<uint8_t, macaddr_t>;

    struct
    {
        nics_mask_t mask;
        nics_mask_t state;
        nics_map    nics;
        macs_map    macs;

        spHclNic& operator[](uint8_t _nic) { return nics[_nic]; }
    } m_hclNic;

    HclDeviceConfig& m_deviceConfig;
    OfiPlugin*       m_ofiPlugin {nullptr};
    int              m_ofiDeviceID       = -1;
    bool             m_scaleoutAvailable = true;

    HclDynamicCommsManager m_dynamicComms;
    hcl::HalPtr            m_hal;

    std::map<HclConfigType, std::function<hcclResult_t(HCL_Comm)>> m_openQpsCallbacks;

    ofi_component_t* m_ofiComponent = nullptr;

    nics_mask_t m_failedScaleOutPortsMask = {};
};
