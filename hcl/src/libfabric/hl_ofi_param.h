#include "hcl_utils.h"

#ifndef __HL_OFI_PARAM_H__
#define __HL_OFI_PARAM_H__

#ifdef _cplusplus
extern "C" {
#endif

#include <cassert>
#include <cstring>  // for memset, size_t

#define HL_OFI_PARAM_STR(name, env, default_value)                                                                     \
    pthread_mutex_t hl_ofi_param_lock_##name = PTHREAD_MUTEX_INITIALIZER;                                              \
    char*           hl_ofi_##name()                                                                                    \
    {                                                                                                                  \
        assert(default_value != NULL);                                                                                 \
        static char* value = NULL;                                                                                     \
        pthread_mutex_lock(&hl_ofi_param_lock_##name);                                                                 \
        char* str;                                                                                                     \
        if (value == NULL)                                                                                             \
        {                                                                                                              \
            str = getenv("HL_OFI_" env);                                                                               \
            if (str && strlen(str) > 0)                                                                                \
            {                                                                                                          \
                errno = 0;                                                                                             \
                value = strdup(str);                                                                                   \
                if (!errno)                                                                                            \
                {                                                                                                      \
                    LOG_DEBUG(HCL_OFI, "Setting {} environment variable to {}", "HL_OFI_" env, value);                 \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    LOG_ERR(HCL_OFI, "Invalid value {} provided for {} env variable", str, "HL_OFI_" env);             \
                }                                                                                                      \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                value = strdup(default_value);                                                                         \
            }                                                                                                          \
        }                                                                                                              \
        pthread_mutex_unlock(&hl_ofi_param_lock_##name);                                                               \
        return value;                                                                                                  \
    }

/*
 * List of interface names (comma-separated) to be filtered out for TCP
 * provider. By default, it is set to eliminate lo and docker0 interfaces.
 */
HL_OFI_PARAM_STR(exclude_tcp_if, "EXCLUDE_TCP_IF", "lo,docker0");

#ifdef _cplusplus
}
#endif

#endif