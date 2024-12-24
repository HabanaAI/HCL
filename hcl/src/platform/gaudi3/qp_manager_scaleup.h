#pragma once

#include "qp_manager.h"

class HclDeviceGaudi3;

class QPManagerGaudi3ScaleUp : public QPManagerGaudi3
{
public:
    QPManagerGaudi3ScaleUp(HclDeviceGaudi3& device);
    virtual ~QPManagerGaudi3ScaleUp() = default;

    virtual void addQPsToQPManagerDB(const QPManagerHints& hints, const QpsVector& qps) override;
    virtual void ReleaseQPsResource(const QPManagerHints& hints) override;
    virtual void setNicOffsetsAndLastRank(hcl::ScalStream& stream, const HCL_Comm comm, const bool isSend) override;

    virtual uint32_t getQPn(const QPManagerHints& hints) const override;
    virtual uint32_t getQPi(const QPManagerHints& hints) const override;

protected:
    virtual void
    setNicOffsets(hcl::ScalStream& stream, const HCL_Comm comm, const HCL_CollectiveOp collectiveOp, const bool isSend);

    virtual void setLastRankScaleup(hcl::ScalStream&       stream,
                                    const HCL_Comm         comm,
                                    const HCL_CollectiveOp collectiveOp,
                                    const bool             isSend);

    uint32_t getLastRankPortMask(HclDynamicCommunicator& dynamicComm,
                                 const HCL_CollectiveOp  collectiveOp,
                                 const bool              isSend) const;

private:
    void resizeDBForNewComms(const HCL_Comm comm);
    void resizeOffsetDBs(const HCL_Comm comm);

    std::array<uint16_t, MAX_NICS_GEN2ARCH>&
    getRemoteRankIndices(HCL_Comm comm, HCL_CollectiveOp collectiveOp, bool isSend);

    // m_qpInfoScaleUp[comm][qpi] -> qpn
    std::vector<std::array<QPn, MAX_QPS_PER_CONNECTION_G3>> m_qpInfoScaleUp;

    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_remoteRankOffsets;
    std::vector<std::array<uint16_t, MAX_NICS_GEN2ARCH>> m_myRankOffsets;
};