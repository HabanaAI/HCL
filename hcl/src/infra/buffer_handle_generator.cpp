#include "infra/buffer_handle_generator.h"
#include "hcl_utils.h"  // for VERIFY, LOG_HCL_ERR

BufferToken BufferTokenGenerator::generateBufferToken(BufferType type)
{
    uint64_t next_handle = handleCtr[type]++;
    VERIFY(handleCtr[type] <= maxBuffers[type],
           "cannot allocate, max buffer for type {} is {}, handleCtr[type] = {}",
           type,
           maxBuffers[type],
           handleCtr[type]);
    return BufferToken(type, next_handle);
}

void BufferTokenGenerator::verifyHandle(const BufferToken& buffHandle)
{
    VERIFY(!(buffHandle.bufferType == TEMP_BUFFER && buffHandle.bufferIdx != handleCtr[TEMP_BUFFER] - 1),
           "Invalid use of TEMP_BUFFER, tried to r/w from stale buffer (index {}), current buffer index is {}",
           buffHandle.bufferIdx,
           handleCtr[TEMP_BUFFER] - 1);
}

bool BufferTokenGenerator::checkTypeAllocation(BufferType type)
{
    return handleCtr[type] > 0;
}