#pragma once

#include <cstdint>                      // for uint32_t, uint8_t, uint16_t
#include <functional>                   // for function
#include <map>                          // for map
#include <unordered_map>                // for unordered_map
#include <unordered_set>                // for unordered_set
#include <vector>                       // for vector

#include "hcl_api_types.h"              // for HCL_Comm, HCL_Rank
#include "hccl_types.h"                 // for hcclResult_t
#include "hcl_config.h"                 // for HclConfig (ptr only), HclDevi...
#include "hcl_dynamic_comms_manager.h"  // for HclDynamicCommsManager
#include "hcl_hal.h"                    // for HalPtr
#include "hcl_types.h"                  // for HclConfigType, NO_DEVICE_ID
#include "synapse_api_types.h"          // for synDeviceId, synStreamHandle
#include "synapse_common_types.h"       // for synDeviceType
#include "hcl_nic.h"
#include "hcl_config.h"                 // for HclDeviceConfig
#include "infra/hcl_affinity_manager.h"
#include "libfabric/hl_ofi_component.h"

class HclDynamicCommunicator;
class HclEvent;
class UniqueSortedVector;
class ofi_t;

class OfiPlugin;
class HcclHostBufferManager;


class IHclDevice
{
public:
    IHclDevice() = default;  // used for testing only
    IHclDevice(HclDeviceConfig& deviceConfig);

    synDeviceType getDeviceType() const { return m_deviceType; }

    virtual ~IHclDevice() noexcept(false);

    /**
     * @brief enable parametrized destruction
     * to be called before destructor
     *
     * @param force - flag to force destroy, false by default
     */
    virtual hcclResult_t destroy(bool force = false);

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

    hcclResult_t updateRankQps(HCL_Comm comm, HCL_Rank rank);

    HCL_Rank getGlobalRankForComm(HCL_Comm comm, HCL_Rank rankID) const;

    /**
     * get dynamic communicator with given id
     */
    virtual HclDynamicCommunicator& getComm(HCL_Comm comm);

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
    virtual int getCommSize(HCL_Comm comm);

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

    /**
     * allocate a new communicator. returns allocated comm id.
     */
    virtual HCL_Comm allocateNewComm();

    /**
     * allocate HCL_COMM_WORLD communicator.
     */
    virtual HCL_Comm allocateCommWorld();

    virtual hcclResult_t networkFlush(HCL_Request* phRequest, synStreamHandle streamHandle);

    virtual int pcieFlush();

    virtual hcclResult_t sync(HCL_Comm comm, uint16_t tag);

    virtual void waitForAllEvents(bool isCsDone = true);
    virtual void waitForAllEvents(uint32_t queueOffset, bool isCsDone = true);

    virtual nics_mask_t getActiveNics(HCL_Rank fromRank, HCL_Rank toRank, int physicalQueueOffset, HCL_Comm comm) = 0;

    /**
     * @brief Get all Nics State
     * @return
     *      hcclSuccess if all enabled NICs are up
     *      hcclInternalError if one of enabled NICs is down
     */
    hcclResult_t updateNicsState();  // move to private

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
    virtual bool isScaleOutPort(uint16_t port, unsigned spotlightType = DEFAULT_SPOTLIGHT) = 0;

    HclDeviceConfig&   getDeviceConfig();
    int                getFd() const;
    inline const hcl::HalPtr  getHal() const { return m_hal; };

    virtual void      openWQs();
    virtual hcclResult_t openQps(HCL_Comm comm);
    virtual hcclResult_t updateQps(HCL_Comm comm);
    virtual uint8_t   getPeerNic(HCL_Rank rank, HCL_Comm comm, uint8_t port);

    virtual bool isDramAddressValid(uint64_t addr) const = 0;

    virtual bool isCommunicatorInScaleupGroup(HCL_Comm comm);

    virtual bool isCommunicatorScaleupGroupPeers(HCL_Comm comm);

    virtual bool isCommunicatorHierarchical(HCL_Comm comm);

    virtual hcclResult_t prepareAndValidateCommunicator(HCL_Comm comm, bool isLoopbackModeOrNullSubmission);

    virtual int getNumActiveComms() const;

    virtual int getScaleupGroupSize(HCL_Comm comm);

    virtual unsigned getSenderWqeTableSize()   = 0;
    virtual unsigned getReceiverWqeTableSize() = 0;

    virtual nics_mask_t getNicsStatusMask() const;

    virtual ofi_t* getOfiHandle();
    virtual int    getOfiDeviceId();
    virtual HcclHostBufferManager*   getHostBufferManager() { return nullptr; }

    int  getHwModuleId();
    bool isScaleOutAvailable() { return m_scaleoutAvailable; }

    virtual spHclNic allocateNic(uint32_t nic, uint32_t max_qps) { return std::make_shared<IHclNic>(this, nic); }

    virtual uint32_t createQp(uint32_t port, uint8_t qpId) = 0;
    virtual hcclResult_t setupQps(HCL_Comm comm, HCL_Rank rank, uint32_t stream, uint32_t port, uint32_t qpn, uint8_t qpSet) = 0;
    virtual void destroyQp(uint32_t port, uint32_t qpn) = 0;

    virtual uint64_t getDRAMSize() { return 0; };
    virtual uint64_t getDRAMBaseAddr() { return 0; };
    virtual bool     isACcbHalfFullForDeviceBenchMark(const unsigned archStreamIdx);

    ofi_component_t*  getOfiComponent() { return m_ofiComponent; }
    const synDeviceId m_deviceId = NO_DEVICE_ID;
    HclDeviceConfig   m_deviceConfig;

protected:
    virtual uint32_t allocateConnection(uint32_t port, HCL_Rank rank, HCL_Comm comm, uint8_t qpId, uint8_t qpSet = 0);
    virtual void     setGaudiDirect() {};

    void setHal(hcl::HalPtr ptr);
    void registerOpenQpCallback(HclConfigType configType, std::function<hcclResult_t(HCL_Comm)> callback);
    void createOfiPlugin();
    void setScaleoutMode(const int scaleOutGNICs);
    void initNicsMask();
    void fillMacAddresses(HCL_Comm comm);
    void getMacInfo();
    void readMacInfoDriver();
    void getMacAddressInfo();
    bool readMacInfoFromFile(const char* macAddrInfoFilePath);

    class macaddr_t
    {
    private:
        uint64_t addr_ = 0;
    public:
        operator uint64_t() const {return addr_;}
        macaddr_t& operator = (void* other) { memcpy(&addr_, other, ETH_ALEN); return *this; }
        macaddr_t& operator = (uint64_t other) { addr_ = other; return *this; }
    };

    using nics_map = std::unordered_map<uint8_t, spHclNic>;
    using macs_map = std::unordered_map<uint8_t, macaddr_t>;

    struct
    {
        nics_mask_t mask;
        nics_map    nics;
        macs_map    macs;

        spHclNic& operator[] (uint8_t _nic) {return nics[_nic];}
    } m_hclNic;

    synDeviceType    m_deviceType;
    OfiPlugin* m_ofiPlugin {nullptr};
    int              m_ofiDeviceID = -1;
    bool             m_scaleoutAvailable = true;

    HclDynamicCommsManager m_dynamicComms;
    hcl::HalPtr            m_hal;

    std::map<HclConfigType, std::function<hcclResult_t(HCL_Comm)>> m_openQpsCallbacks;

    ofi_component_t* m_ofiComponent = nullptr;
};
