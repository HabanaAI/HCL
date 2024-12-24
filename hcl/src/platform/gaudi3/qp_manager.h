#pragma once

#include <cstdint>          // for uint32_t, uint8_t
#include <array>            // for array
#include <set>              // for set
#include <vector>           // for vector
#include "hcl_api_types.h"  // for HCL_Comm
#include "hcl_dynamic_communicator.h"
#include "hcl_types.h"
#include "infra/scal/gen2_arch_common/scal_stream.h"
#include "platform/gen2_arch_common/qp_manager.h"

// since we use collective qps in gaudi3, we use the same qp IDs for all ranks in scaleUp, so the ranks is irelevant in
// th DB. the same set of qp IDs are used throughout all scale up ports. but for scale out we use the same nics for all
// peers, so each rank gets a new set of qps. and they are saved separately in the DB

#define INVALID_COUNT ((uint64_t) - 1)

namespace G3
{
enum QP_e
{
    QPE_RS_RECV = 0,
    QPE_AG_RECV,
    QPE_A2A_RECV,
    QPE_RS_SEND,
    QPE_AG_SEND,
    QPE_A2A_SEND
};
}

class HclDeviceGaudi3;

class QPManagerGaudi3 : public QPManager
{
public:
    QPManagerGaudi3(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3() = default;

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override = 0;
    virtual void ReleaseQPsResource(const QPManagerHints& hints) override                        = 0;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override = 0;
    virtual uint32_t getQPi(const HCL_CollectiveOp collectiveOp, const bool isSend) override;
    virtual uint32_t getDestQPi(const unsigned qpi) const override;

    virtual QPUsage getBaseQpAndUsage(const HclDynamicCommunicator& dynamicComm,
                                      HCL_CollectiveOp              collectiveOp,
                                      bool                          isSend,
                                      bool                          isComplexCollective,
                                      bool                          isReductionInIMB,
                                      bool                          isHierarchical,
                                      uint64_t                      count,
                                      uint64_t                      cellCount,
                                      HclConfigType                 boxType,
                                      bool                          isScaleOut        = false,
                                      HCL_Rank                      remoteRank        = HCL_INVALID_RANK,
                                      uint8_t                       qpSet             = 0,
                                      const bool                    isReduction       = false,
                                      HCL_CollectiveOp              complexCollective = eHCLNoCollective,
                                      bool                          isRoot            = false) override;

    /* declared for the interface, but only implemented for scaleUp */
    virtual void setNicOffsetsAndLastRank(hcl::ScalStream& stream, const HCL_Comm comm, const bool isSend) override {};
};
