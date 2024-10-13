#pragma once
namespace hcl
{
/* 7-bit stream_context passed by HCL to FW through edma cmds */
#pragma pack(push, 1)

struct StreamContextEncoding
{
    union
    {
        struct
        {
            uint8_t stream_index : 2;
            uint8_t api_id : 5;
        };
        struct
        {
            uint8_t debug_api_id : 4;
            uint8_t slice : 2;
            uint8_t is_scale_out : 1;
        };
        uint8_t raw : 7;
    };
};

/* 16-bit context_id passed to Profiler by FW */
struct ContextIdEncoding
{
    uint8_t chunk_id : 3;
    uint8_t stream_context : 7;
    uint8_t opcode : 4;
    uint8_t reserved : 2;
};

#pragma pack(pop)
}  // namespace hcl
