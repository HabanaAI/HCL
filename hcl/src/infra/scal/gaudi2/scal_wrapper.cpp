#include "scal/gen2_arch_common/scal_exceptions.h"
#include "infra/scal/gaudi2/scal_wrapper.h"

#include "infra/scal/gaudi2/scal_utils.h"            // for Gaudi2HclScalUtils
#include "infra/scal/gen2_arch_common/scal_utils.h"  // for Gen2ArchScalUtils
namespace hcl
{
class ScalJsonNames;
}

using namespace hcl;

Gaudi2ScalWrapper::Gaudi2ScalWrapper(int fd, ScalJsonNames& scalNames) : Gen2ArchScalWrapper(fd, scalNames)
{
    m_utils = new Gaudi2HclScalUtils();
}

Gaudi2ScalWrapper::~Gaudi2ScalWrapper()
{
    if (m_utils) delete m_utils;
}

uint64_t Gaudi2ScalWrapper::getMonitorPayloadAddr(std::string name, unsigned /*fenceIdx*/)
{
    scal_core_handle_t schedulerHandle;

    int rc = scal_get_core_handle_by_name(m_deviceHandle, name.c_str(), &schedulerHandle);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_get_core_handle_by_name with device handle: " +
                                 std::to_string(uint64_t(m_deviceHandle)) + " and name: " + name);
    }

    scal_control_core_info_t info;
    rc = scal_control_core_get_info(schedulerHandle, &info);
    if (rc != SCAL_SUCCESS)
    {
        throw ScalErrorException("Failed on scal_control_core_get_info with core handle: " +
                                 std::to_string(uint64_t(schedulerHandle)));
    }
    return info.dccm_message_queue_address;
}
