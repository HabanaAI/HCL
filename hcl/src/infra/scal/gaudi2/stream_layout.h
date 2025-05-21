
#pragma once
#include "infra/scal/gen2_arch_common/stream_layout.h"

class Gaudi2StreamLayout : public Gen2ArchStreamLayout
{
public:
    Gaudi2StreamLayout();
    Gaudi2StreamLayout(Gaudi2StreamLayout&& other)           = delete;
    Gaudi2StreamLayout(const Gaudi2StreamLayout& other)      = delete;
    Gaudi2StreamLayout& operator=(Gaudi2StreamLayout&&)      = delete;
    Gaudi2StreamLayout& operator=(const Gaudi2StreamLayout&) = delete;
    virtual ~Gaudi2StreamLayout() {};

    virtual void initLayout() override;
};