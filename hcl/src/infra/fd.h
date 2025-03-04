#pragma once

#include <unistd.h>  // for close
#include <utility>   // for std::forward
#include <optional>  // for std::optional

class FileDescriptor
{
public:
    inline FileDescriptor() : m_fd(std::nullopt) {}
    explicit inline FileDescriptor(const int fd) : m_fd(fd) {}

    virtual ~FileDescriptor()
    try
    {
        if (m_fd.has_value())
        {
            (void)::close(m_fd.value());
        }
    }
    catch (...)
    {
    }

    FileDescriptor(const FileDescriptor&)            = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) { *this = std::forward<FileDescriptor>(other); };
    FileDescriptor& operator=(FileDescriptor&& other)
    {
        m_fd       = other.m_fd;
        other.m_fd = std::nullopt;
        return *this;
    };

    [[nodiscard]] const int& get() const { return m_fd.value(); }

    [[nodiscard]] inline bool has_value() { return m_fd.has_value(); }
    [[nodiscard]] operator bool() const { return m_fd.operator bool(); }
    [[nodiscard]] inline int value_or(int&& u) const { return m_fd.value_or(u); }

private:
    std::optional<int> m_fd;
};
