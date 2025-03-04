#pragma once
#include <string_view>

/**
 * @brief Helper function to get the class name from __PRETTY_FUNCTION__
 */
constexpr bool isReturnTypeKeyword(const char* str, unsigned len)
{
    constexpr const char* keywords[] = {"virtual", "static", "const", "constexpr"};
    for (unsigned k = 0; k < sizeof(keywords) / sizeof(keywords[0]); ++k)
    {
        const bool match = [&]() {
            for (unsigned i = 0; i < len; ++i)
            {
                if (keywords[k][i] != str[i])
                {
                    return false;
                }
            };
            return true;
        }();
        if (match) return true;
    }
    return false;
}

template<unsigned N>
constexpr std::string_view getClassName(const char (&name)[N])
{
    // find the end of the function name - first '('
    unsigned fnNameEnd = N;
    for (unsigned i = 0; i < N - 1; ++i)
    {
        if (name[i] == '(')
        {
            fnNameEnd = i;
            break;
        }
    }

    // skip return type (if exists)

    unsigned startType = 0;
    unsigned start     = 0;
    do
    {
        for (unsigned i = startType; i < fnNameEnd - 1; ++i)
        {
            if (name[i] == ' ')
            {
                start = i + 1;
                break;
            }
        }
        if (!isReturnTypeKeyword(&name[startType], start - startType - 1))
        {
            break;
        }
        startType = start;
    } while (true);

    // find the beginning of the function name - last '::'
    unsigned fnNameStart = start;
    for (unsigned i = fnNameEnd - 1; i > start; --i)
    {
        if (name[i] == ':' && name[i - 1] == ':')
        {
            fnNameStart = i - 1;
            break;
        }
    }

    // find the beginning of the class name - last '::'
    unsigned classNameStart = start;
    for (unsigned i = fnNameStart; i > start; --i)
    {
        if (name[i] == ':' && name[i - 1] == ':')
        {
            classNameStart = i + 1;
            break;
        }
    }

    // for templated class - find the beginning of the templated parameters
    unsigned classNameEnd = fnNameStart;
    for (unsigned i = classNameStart; i < fnNameStart; ++i)
    {
        if (name[i] == '<')
        {
            classNameEnd = i;
            break;
        }
    }

    return std::string_view(&name[start], classNameEnd - start);
}

#if (defined(__clang__) && __clang_major__ >= 5) || (defined(__GNUC__) && __GNUC__ >= 9)
#define COMPILE_TIME_GET_CLASS_NAME_SUPPORTED
#endif

#ifdef COMPILE_TIME_GET_CLASS_NAME_SUPPORTED
#define CLASS_NAME                                                                                                     \
    []() constexpr {                                                                                                   \
        constexpr std::string_view className = getClassName(__PRETTY_FUNCTION__);                                      \
        return className;                                                                                              \
    }()
#else
#define CLASS_NAME                                                                                                     \
    []() {                                                                                                             \
        static const std::string_view className = getClassName(__PRETTY_FUNCTION__);                                   \
        return className;                                                                                              \
    }()
#endif
