
#pragma once
#include <cstdint>  // for int64_t, uint64_t, uint32_t
#include <array>    // for array
#include <map>      // for map

enum BufferType
{
    STATIC_BUFFER = 0,
    TEMP_BUFFER,
    INVALID_BUFFER
};

struct BufferToken
{
    BufferType bufferType = INVALID_BUFFER;
    uint64_t   bufferIdx  = UINT64_MAX;

    BufferToken(BufferType type, uint64_t idx) : bufferType(type), bufferIdx(idx) {}
    BufferToken() : bufferType(INVALID_BUFFER), bufferIdx(UINT64_MAX) {}
};

class BufferTokenGenerator
{
public:
    virtual ~BufferTokenGenerator() = default;

    BufferTokenGenerator()                                       = default;
    BufferTokenGenerator(BufferTokenGenerator&&)                 = delete;
    BufferTokenGenerator(const BufferTokenGenerator&)            = delete;
    BufferTokenGenerator& operator=(BufferTokenGenerator&&)      = delete;
    BufferTokenGenerator& operator=(const BufferTokenGenerator&) = delete;

    BufferToken generateBufferToken(BufferType type);
    void        verifyHandle(const BufferToken& buffHandle);
    bool        checkTypeAllocation(BufferType type);

private:
    std::array<uint64_t, INVALID_BUFFER> handleCtr  = {0, 0};
    std::map<BufferType, uint64_t>       maxBuffers = {{STATIC_BUFFER, 1}, {TEMP_BUFFER, UINT64_MAX}};
};
