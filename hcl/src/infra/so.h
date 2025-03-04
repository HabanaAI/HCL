#pragma once

#include <string>    // for std::string
#include <stdint.h>  // for uint32_t

class SharedObject final
{
public:
    SharedObject(const std::string& name, uint32_t flags);
    virtual ~SharedObject();

public:
    template<typename T>
    inline T symbol(const std::string& symbol_name) const
    {
        return reinterpret_cast<T>(get_symbol(m_handle, symbol_name));
    }

private:
    void* const m_handle;

private:
    static void* create_handle(const std::string& name, uint32_t flags);
    static void* get_symbol(void* handle, const std::string& name);
};
