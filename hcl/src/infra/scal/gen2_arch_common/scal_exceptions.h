#pragma once

#include <string>  // for string, allocator
#include "hcl_exceptions.h"

namespace hcl
{
class ScalException : public HclException
{
public:
    ScalException(int fd = -1, const std::string& msg = "") : HclException(msg), m_fd(fd) {}

    int getFd() const noexcept { return m_fd; }

private:
    int m_fd;
};

class NotImplementedScalException : public ScalException
{
public:
    NotImplementedScalException(const std::string& apiName);
};

class ScalErrorException : public ScalException
{
public:
    ScalErrorException(const std::string& errorMsg);
};

class ScalBusyException : public ScalException
{
public:
    ScalBusyException(const std::string& errorMsg);
};

}  // namespace hcl