#pragma once

#include <cstdint>      // for uint64_t
#include "hcl_utils.h"  // for VERIFY

#include "infra/scal/gen2_arch_common/scal_utils.h"
#include "platform/gen2_arch_common/host_stream.h"
#include "platform/gen2_arch_common/commands/hcl_commands.h"  // for HclCommandsGen2Arch
#include "platform/gen2_arch_common/types.h"
#include "platform/gen2_arch_common/wqe_tracker.h"  // for WqeWraparoundBits

// fwd decl
class ScaleoutProvider;
class NonCollectiveState;
struct SliceState;
class HclCollectiveRoutinesGen2Arch;
namespace hcl
{
class ScalStream;
struct syncInfo;
}  // namespace hcl

class Descriptor
{
public:
    explicit Descriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                        ScaleoutProvider&              scaleoutProvider,
                        hcl::ScalStream&               currentStream,
                        int                            archStreamIdx,
                        unsigned                       uarchStreamIdx,
                        unsigned                       schedIdx);
    virtual ~Descriptor() = default;

    virtual void run(SliceState& sliceState)                 = 0;
    virtual void run(NonCollectiveState& nonCollectiveState) = 0;

protected:
    HclCollectiveRoutinesGen2Arch& m_collectiveRoutines;
    ScaleoutProvider&              m_scaleoutProvider;
    hcl::ScalStream&               m_currentStream;
    int                            m_archStreamIdx;
    unsigned                       m_uarchStreamIdx;
    unsigned                       m_schedIdx;
};

class BarrierArbitratorDescriptor : public Descriptor
{
public:
    explicit BarrierArbitratorDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                         ScaleoutProvider&              scaleoutProvider,
                                         hcl::ScalStream&               currentStream,
                                         hcl::ScalStream&               arbitratorStream,
                                         int                            archStreamIdx,
                                         unsigned                       uarchstreamIdx,
                                         unsigned                       schedIdx,
                                         unsigned                       requiredCredits,
                                         hcl::syncInfo&                 longSo);
    virtual ~BarrierArbitratorDescriptor() = default;

    virtual void run(SliceState& sliceState) override;
    virtual void run(NonCollectiveState& nonCollectiveState) override;

protected:
    hcl::ScalStream& m_arbitratorStream;
    unsigned         m_requiredCredits;
    hcl::syncInfo&   m_longSo;
};

class NativeScaleoutDescriptor : public Descriptor
{
public:
    explicit NativeScaleoutDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                      ScaleoutProvider&              scaleoutProvider,
                                      hcl::ScalStream&               currentStream,
                                      int                            archStreamIdx,
                                      unsigned                       uarchStreamIdx,
                                      unsigned                       schedIdx);

    virtual ~NativeScaleoutDescriptor() = default;
    virtual void run(SliceState& sliceState) override;
    virtual void run(NonCollectiveState& nonCollectiveState) override { VERIFY(false, "Illegal call"); }
};

class NativeNonCollectiveScaleoutDescriptor : public Descriptor
{
public:
    explicit NativeNonCollectiveScaleoutDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                                   ScaleoutProvider&              scaleoutProvider,
                                                   hcl::ScalStream&               currentStream,
                                                   int                            archStreamIdx,
                                                   unsigned                       uarchStreamIdx,
                                                   unsigned                       schedIdx,
                                                   const WqeWraparoundBits&       wraparoundBits);

    virtual ~NativeNonCollectiveScaleoutDescriptor() = default;
    virtual void run(SliceState& sliceState) override { VERIFY(false, "Illegal call"); }
    virtual void run(NonCollectiveState& nonCollectiveState) override;

private:
    WqeWraparoundBits m_wraparoundBits;
};

class LibfabricScaleoutDescriptor : public Descriptor
{
public:
    explicit LibfabricScaleoutDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                         ScaleoutProvider&              scaleoutProvider,
                                         hcl::ScalStream&               currentStream,
                                         int                            archStreamIdx,
                                         unsigned                       uarchStreamIdx,
                                         unsigned                       schedIdx,
                                         HclCommandsGen2Arch&           commands);
    virtual ~LibfabricScaleoutDescriptor() = default;
    virtual void run(SliceState& sliceState) override;
    virtual void run(NonCollectiveState& nonCollectiveState) override { VERIFY(false, "Illegal call"); }

protected:
    void     streamAddWait(spHostStreamFifo hostStream, fence_info fence, const uint64_t srCount);
    unsigned getHostUarchStreamIdx();

    HclCommandsGen2Arch& m_commands;

private:
    Gen2ArchScalUtils* m_utils;
};

class LibfabricNonCollectiveScaleoutDescriptor : public Descriptor
{
public:
    explicit LibfabricNonCollectiveScaleoutDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                                      ScaleoutProvider&              scaleoutProvider,
                                                      hcl::ScalStream&               currentStream,
                                                      const int                      archStreamIdx,
                                                      const unsigned                 uarchStreamIdx,
                                                      const unsigned                 schedIdx,
                                                      const uint64_t                 targetValue,
                                                      HclCommandsGen2Arch&           commands);
    virtual ~LibfabricNonCollectiveScaleoutDescriptor() = default;
    virtual void run(SliceState& sliceState) override { VERIFY(false, "Illegal call"); }
    virtual void run(NonCollectiveState& nonCollectiveState) override;

protected:
    unsigned getHostUarchStreamIdx();

    HclCommandsGen2Arch& m_commands;

private:
    const uint64_t m_targetValue;
};

class GaudiDirectScaleoutDescriptor : public LibfabricScaleoutDescriptor
{
public:
    explicit GaudiDirectScaleoutDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                           ScaleoutProvider&              scaleoutProvider,
                                           hcl::ScalStream&               currentStream,
                                           int                            archStreamIdx,
                                           unsigned                       uarchStreamIdx,
                                           unsigned                       schedIdx,
                                           HclCommandsGen2Arch&           commands);
    virtual ~GaudiDirectScaleoutDescriptor() = default;
    virtual void run(SliceState& sliceState) override;
    virtual void run(NonCollectiveState& nonCollectiveState) override { VERIFY(false, "Illegal call"); }

    Gen2ArchScalUtils* m_utils;
};
class GaudiDirectNonCollectiveScaleoutDescriptor : public LibfabricNonCollectiveScaleoutDescriptor
{
public:
    explicit GaudiDirectNonCollectiveScaleoutDescriptor(HclCollectiveRoutinesGen2Arch& collectiveRoutines,
                                                        ScaleoutProvider&              scaleoutProvider,
                                                        hcl::ScalStream&               currentStream,
                                                        const int                      archStreamIdx,
                                                        const unsigned                 uarchStreamIdx,
                                                        const unsigned                 schedIdx,
                                                        const uint64_t                 targetValue,
                                                        HclCommandsGen2Arch&           commands);
    virtual ~GaudiDirectNonCollectiveScaleoutDescriptor() = default;
    virtual void run(SliceState& sliceState) override { VERIFY(false, "Illegal call"); }
    virtual void run(NonCollectiveState& nonCollectiveState) override;
};