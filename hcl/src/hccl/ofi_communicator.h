#pragma once

#include <chrono>                        // for seconds, microseconds
#include <cstddef>                       // for size_t
#include <map>                           // for map
#include <memory>                        // for unique_ptr
#include <vector>                        // for vector
#include "hccl_types.h"                  // for hcclResult_t
#include "interfaces/hcl_idevice.h"      // for IHclDevice
#include "libfabric/hl_ofi_component.h"  // for allConnectionComm_t, ofi_req_t (p...
#include "socket_thread.h"               // for SocketThreadsManager
#include "hcl_utils.h"                   // for VERIFY

class UniqueSortedVector;
class ofi_t;
class IHclDevice;

class ofi_communicator;
struct hcclHandle;


struct RankInfo;

using ofi_communicator_handle = std::unique_ptr<ofi_communicator>;
class ofi_communicator
{
public:
    bool initializeCommunicator(int                       hcclRank,
                                int                       nranks,
                                const UniqueSortedVector& peers,
                                IHclDevice*               hclDevice,
                                RankInfo&                 rankInfo);
    bool updateConnections(const HCL_Rank outerRank, const HostNicConnectInfo& hnicsInfoBuf);

    hcclResult_t sendAsync(void*                  sendbuff,
                           size_t                 size,
                           int                    peer,
                           hcclHandle*            handle,
                           unsigned               hostConnIdx,
                           OfiCompCallbackParams& compParams);
    hcclResult_t recvAsync(void*                  recvbuff,
                           size_t                 size,
                           int                    peer,
                           hcclHandle*            handle,
                           unsigned               hostConnIdx,
                           OfiCompCallbackParams& compParams);
    bool         waitForCompletionNb(void* handle, int& done);

    bool destroy();

    ~ofi_communicator() = default;
    ofi_communicator();

    ofi_communicator(ofi_communicator&)              = delete;
    ofi_communicator(ofi_communicator&&)             = delete;
    ofi_communicator&  operator=(ofi_communicator&)  = delete;
    ofi_communicator&& operator=(ofi_communicator&&) = delete;

private:
    int                                                                my_rank_;
    std::vector<std::array<allConnectionComm_t, MAX_HNIC_CONNECTIONS>> m_peerRankToConnectionInfo;

    ofi_t*               m_ofi_;
    SocketThreadsManager threads_manager_;
    int                  m_ofiDeviceId;
    IHclDevice*          m_device_;
    struct send_recv_vec
    {
        ofi_req_t* req;
        int        my_rank;
        int        peer_rank;
    };
    std::vector<send_recv_vec> send_recv_requests;

    unsigned getNumConnectionPerRank();

    RankInfo* m_myRankInfo = nullptr;
};
