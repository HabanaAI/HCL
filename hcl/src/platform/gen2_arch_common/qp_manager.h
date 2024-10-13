#pragma once

#include "platform/gen2_arch_common/types.h"          // for QpsVector
#include "platform/gen2_arch_common/hcl_device.h"     // for HclDeviceGen2Arch
#include "infra/scal/gen2_arch_common/scal_stream.h"  // for ScalStream
#include "interfaces/hcl_unique_sorted_vector.h"      // for UniqueSortedVector
#include "hcl_types.h"                                // for HCL_INVALID_COMM/RANK

#include <cstdint>

constexpr uint32_t INVALID_QP = 0;
using QPn                     = uint32_t;

class QPUsage
{
public:
    uint32_t qpn;
    bool     disregardRank;
};

struct QPManagerHints
{
    explicit QPManagerHints(HCL_Comm comm,
                            unsigned remoteRank = HCL_INVALID_RANK,
                            unsigned nic        = INVALID_QP,
                            unsigned qpi        = INVALID_QP,
                            unsigned qpn        = INVALID_QP,
                            unsigned qpSet      = INVALID_QP)
    : m_comm(comm), m_remoteRank(remoteRank), m_nic(nic), m_qpi(qpi), m_qpn(qpn), m_qpSet(qpSet) {};

    HCL_Comm m_comm       = HCL_INVALID_COMM;
    unsigned m_remoteRank = HCL_INVALID_RANK;
    unsigned m_nic        = INVALID_QP;
    unsigned m_qpi        = INVALID_QP;
    unsigned m_qpn        = INVALID_QP;
    unsigned m_qpSet      = INVALID_QP;
};

class QPManager
{
public:
    QPManager(HclDeviceGen2Arch& device) : m_device(device) {};
    virtual ~QPManager() = default;

    virtual void registerQPs(const QPManagerHints& hints, const QpsVector& qps) = 0;
    virtual void closeQPs(const QPManagerHints& hints)                          = 0;
    virtual void allocateQPDBStorage(const HCL_Comm comm) {};

    virtual uint32_t getQPn(const QPManagerHints& hints) const                      = 0;
    virtual uint32_t getQPi(const QPManagerHints& hints) const                      = 0;
    virtual uint32_t getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend) = 0;
    virtual uint32_t getDestQPi(const unsigned qpi) const                           = 0;

    virtual void setConfiguration(hcl::ScalStream& stream, HCL_Comm comm, bool isSend)
    {
        VERIFY(false, "unreachable code");
    };

    virtual QPUsage getBaseQpAndUsage(HclDynamicCommunicator& dynamicComm,
                                      HCL_CollectiveOp        collectiveOp,
                                      bool                    isSend,
                                      bool                    isComplexCollective,
                                      bool                    isReductionInIMB,
                                      bool                    isHierarchical,
                                      uint64_t                count,
                                      uint64_t                cellCount,
                                      HclConfigType           boxType,
                                      bool                    isScaleOut        = false,
                                      HCL_Rank                remoteRank        = HCL_INVALID_RANK,
                                      uint8_t                 qpSet             = 0,
                                      const bool              isReduction       = false,
                                      HCL_CollectiveOp        complexCollective = eHCLNoCollective,
                                      bool                    isRoot            = false)
    {
        VERIFY(false, "unreachable code");
        QPUsage ret = {0, false};
        return ret;
    };

    inline bool isInvalidQPn(const uint32_t qpn) const { return (qpn == INVALID_QP); };

protected:
    HclDeviceGen2Arch& m_device;
    unsigned           m_maxQPsPerConnection;
};
