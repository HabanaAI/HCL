#pragma once

#include "infra/scal/gen2_arch_common/scal_stream.h"

namespace hcl
{
/**
 * @brief
 *
 *  ScalStream responsible for managing a cyclic buffer for a given stream name.
 */
class Gaudi3ScalStream : public ScalStream
{
public:
    Gaudi3ScalStream(ScalJsonNames&       scalNames,
                     const std::string&   name,
                     Gen2ArchScalWrapper& scalWrapper,
                     CompletionGroup&     cg,
                     unsigned             schedIdx,
                     unsigned             internalStreamIdx,
                     unsigned             archStreamIdx,
                     HclCommandsGen2Arch& commands);
};
}  // namespace hcl
