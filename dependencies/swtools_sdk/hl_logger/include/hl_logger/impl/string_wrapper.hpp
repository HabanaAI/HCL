#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <cstring>
#include "hllog_config.hpp"

namespace hl_logger{
inline namespace v1_0{
// simple string owing wrapper compatible with ABI0/ABI1
class string_wrapper {
public:
    string_wrapper() = default;
    string_wrapper(string_wrapper &&) = default;
    string_wrapper(string_wrapper const &) = default;
    string_wrapper(const char * str)
    {
        _buffer.assign(str, str + strlen(str));
    }
    HLLOG_FORCE_INLINE string_wrapper(std::string const & str)
    {
        _buffer.assign(str.begin(), str.end());
    }

    string_wrapper & operator=(string_wrapper const & ) = default;
    string_wrapper & operator=(string_wrapper && ) = default;
    string_wrapper & operator=(const char * str)
    {
        _buffer.assign(str, str + strlen(str));
        return *this;
    }
    HLLOG_FORCE_INLINE string_wrapper & operator=(std::string const & str)
    {
        _buffer.assign(str.begin(), str.end());
        return *this;
    }
    operator std::string_view() const
    {
        return std::string_view(_buffer.data(), _buffer.size());
    }
    std::string_view str() const
    {
        return std::string_view(_buffer.data(), _buffer.size());
    }

private:
    std::vector<char> _buffer;
};
}

inline namespace HLLOG_INLINE_API_NAMESPACE{
inline auto format_as(string_wrapper const & str)
{
    return std::string_view(str);
}

inline auto format_as(string_wrapper && str)
{
    return std::string(str.str());
}
}}