#include "so.h"
#include <dlfcn.h>
#include "hcl_utils.h"

SharedObject::SharedObject(const std::string& name, uint32_t flags) : m_handle(create_handle(name, flags)) {}

SharedObject::~SharedObject()
{
    try
    {  // Never throw out of dtor
        dlclose(m_handle);
    }
    catch (...)
    {
    }
}

void* SharedObject::create_handle(const std::string& name, uint32_t flags)
{
    void* const result = dlopen(name.c_str(), flags);
    if (nullptr == result)
    {
        throw hcl::HclException("Failed to load so ", name);
    }
    return result;
}

void* SharedObject::get_symbol(void* handle, const std::string& name)
{
    void* const result = dlsym(handle, name.c_str());
    VERIFY(result, "Error in dlsym for {}. Please update OFI wrapper to use host scale-out.", name);
    return result;
}
