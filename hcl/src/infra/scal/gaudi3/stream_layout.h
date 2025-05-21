
#pragma once
#include "infra/scal/gen2_arch_common/stream_layout.h"

class Gaudi3StreamLayout : public Gen2ArchStreamLayout
{
public:
    Gaudi3StreamLayout();
    Gaudi3StreamLayout(Gaudi3StreamLayout&& other)           = delete;
    Gaudi3StreamLayout(const Gaudi3StreamLayout& other)      = delete;
    Gaudi3StreamLayout& operator=(Gaudi3StreamLayout&&)      = delete;
    Gaudi3StreamLayout& operator=(const Gaudi3StreamLayout&) = delete;
    virtual ~Gaudi3StreamLayout() {};

    virtual void initLayout() override;
};