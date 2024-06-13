#pragma once
#include <array>            // for array
#include <cstdint>          // for uint64_t
#include <memory>           // for unique_ptr
#include <vector>           // for vector
#include "hcl_api_types.h"  // for HCL_Comm
#include "libfabric/hl_ofi.h"
#include "platform/gen2_arch_common/host_stream.h"         // for HostStream...
#include "platform/gen2_arch_common/signals/calculator.h"  // for nicsPerCon...
#include "platform/gen2_arch_common/port_mapping.h"        // for Gen2ArchDevicePortMapping

#include "interfaces/hcl_unique_sorted_vector.h"
#include "hcl_types.h"            // for HostNicConnectInfo
#include "buffer_manager_base.h"  // for e_hostPoolID

struct SliceState;
class NonCollectiveState;
class HclDeviceGen2Arch;
class HostScheduler;
class HostBufferManager;
class SignalsManager;
struct NonCollectiveState;
struct SliceState;
class ofi_t;  // for getOfiHandle()

constexpr unsigned MAX_NUM_POOLS = 20;
struct HostBuffersAmount
{
    static constexpr std::array<std::pair<e_hostPoolID, unsigned>, MAX_NUM_POOLS> buffersArr = {
        {{HNIC_SEND_POOL, 16}, {HNIC_RECV_POOL, 16}}};

    static unsigned getBufferCount(e_hostPoolID key)
    {
        for (const auto& pair : buffersArr)
        {
            if (pair.first == key)
            {
                return pair.second;
            }
        }
        throw hcl::VerifyException("Host buffer pool was not found.");
    }
};

class ScaleoutProvider
{
public:
    ScaleoutProvider(HclDeviceGen2Arch* device);
    virtual ~ScaleoutProvider() = default;

    virtual bool isHostNic() const     = 0;
    virtual bool isGaudiDirect() const = 0;

    virtual void openConnectionsOuterRanks(const HCL_Comm comm, const UniqueSortedVector& outerRanks) = 0;
    virtual void verifyConnections(HCL_Comm comm)                                                     = 0;
    virtual void updateConnectionsNonPeer(const HCL_Comm                         comm,
                                          const UniqueSortedVector&              nonPeerRemoteRanks,
                                          const std::vector<HostNicConnectInfo>& bufferFromTargets)   = 0;
    virtual void closeConnections(HCL_Comm comm) = 0;  // Close outer peer and non-peer ranks
    virtual void destroy()                       = 0;

    virtual void               requestScaleoutResources(SliceState& sliceState, SignalsManager& signalsManager) = 0;
    virtual void               requestScaleoutResources(NonCollectiveState& nonCollectiveState)                 = 0;
    virtual unsigned           getNumOfNicsPerDevice(unsigned spotlightType = DEFAULT_SPOTLIGHT) const          = 0;
    virtual HostBufferManager* getHostBufferManager(unsigned streamIdx);

    static ScaleoutProvider* createScaleOutProvider(HclDeviceGen2Arch* device);

    // protected:
    HclDeviceGen2Arch* m_device = nullptr;
};

class Gen2ArchScaleoutProvider : public ScaleoutProvider
{
public:
    Gen2ArchScaleoutProvider(HclDeviceGen2Arch* device);
    virtual bool isHostNic() const override;
    virtual bool isGaudiDirect() const override;

    virtual void openConnectionsOuterRanks(const HCL_Comm comm, const UniqueSortedVector& outerRanks) override;
    virtual void verifyConnections(HCL_Comm comm) override;
    virtual void updateConnectionsNonPeer(const HCL_Comm                         comm,
                                          const UniqueSortedVector&              nonPeerRemoteRanks,
                                          const std::vector<HostNicConnectInfo>& bufferFromTargets) override;
    virtual void closeConnections(HCL_Comm comm) override;
    virtual void destroy() override {};

    virtual unsigned getNumOfNicsPerDevice(unsigned spotlightType) const override;
    virtual void     requestScaleoutResources(SliceState& sliceState, SignalsManager& signalsManager) override;
    virtual void     requestScaleoutResources(NonCollectiveState& nonCollectiveState) override;

protected:
    void calculateScaleoutSendResources(SliceState& scaleupSetupInput, SignalsManager& signalsManager);
    void calculateScaleoutRecvResources(SliceState& scaleupSetupInput, SignalsManager& signalsManager);
};

class LibfabricScaleoutProvider : public ScaleoutProvider
{
public:
    LibfabricScaleoutProvider(HclDeviceGen2Arch* device);
    ~LibfabricScaleoutProvider();
    virtual bool isHostNic() const override;
    virtual bool isGaudiDirect() const override;

    virtual void openConnectionsOuterRanks(const HCL_Comm comm, const UniqueSortedVector& outerRanks) override;
    virtual void verifyConnections(HCL_Comm comm) override;
    virtual void updateConnectionsNonPeer(const HCL_Comm                         comm,
                                          const UniqueSortedVector&              nonPeerRemoteRanks,
                                          const std::vector<HostNicConnectInfo>& bufferFromTargets) override;
    virtual void closeConnections(HCL_Comm comm) override;
    virtual void destroy() override;

    virtual unsigned getNumOfNicsPerDevice(unsigned spotlightType) const override { return 1; };
    virtual void     requestScaleoutResources(SliceState& sliceState, SignalsManager& signalsManager) override;
    virtual void     requestScaleoutResources(NonCollectiveState& nonCollectiveState) override;
    void             notifyHostScheduler(int archStreamIdx);

    virtual HostBufferManager* getHostBufferManager(unsigned streamIdx) override;

    std::vector<std::array<std::array<HostStream*, NUM_HOST_STREAMS>, HOST_MICRO_ARCH_STREAMS>> m_hostStreamVec;

    uint64_t m_deviceHandle;
    void*    m_hostAddress;
    unsigned m_streamsPerHostSched;
    uint64_t m_numArchStreams;

    std::vector<HostBufferManager*> m_hostBufferManager;

private:
    bool                                        m_isGaudiDirect = false;
    std::vector<std::unique_ptr<HostScheduler>> m_hostScheduler;
};
