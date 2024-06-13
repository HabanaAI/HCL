#include "scal_exceptions.h"

#include "hcl_log_manager.h"  // for LOG_*

using namespace hcl;

NotImplementedScalException::NotImplementedScalException(const std::string& apiName)
: ScalException(-1, apiName + " not implemented.")
{
    LOG_ERR(HCL_SCAL, "{} {}", apiName, " not implemented.");
}

ScalErrorException::ScalErrorException(const std::string& errorMsg) : ScalException(-1, errorMsg)
{
    LOG_ERR(HCL_SCAL, "{}", errorMsg);
}

ScalBusyException::ScalBusyException(const std::string& errorMsg) : ScalException(-1, errorMsg)
{
    LOG_DEBUG(HCL_SCAL, "{}", errorMsg);
}
