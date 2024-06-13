#pragma once

#include "infra/scal/gen2_arch_common/arch_stream.h"
#include <stdexcept>
namespace hcl
{
/**
 * @brief ArchStream is responsible for managing all the microArch streams belong to it.
 *
 */
class Gaudi2ArchStream : public ArchStream
{
public:
    Gaudi2ArchStream(unsigned                 streamIdx,
                     Gen2ArchScalWrapper&     scalWrapper,
                     scal_comp_group_handle_t externalCgHandle,
                     scal_comp_group_handle_t internalCgHandle,
                     ScalJsonNames&           scalNames,
                     HclCommandsGen2Arch&     commands);
};
}  // namespace hcl
