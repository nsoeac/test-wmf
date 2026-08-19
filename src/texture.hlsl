RWStructuredBuffer<uint> luminance : register(u0);
RWStructuredBuffer<uint2> chrominance : register(u1);

cbuffer image_dimensions : register(b0) {
    int width;
    int height;
};

struct PSInput {
    float4 position : SV_POSITION;
};

PSInput VSMain(float4 position : SV_Position) {
    PSInput result;
    result.position = position;
    return result;
}

float4 PSMain(PSInput input) : SV_Target {
    float2 normalised_offset = (input.position.xy + 1.0f) / 2.0f;
    int2 coords = normalised_offset.xy * int2(width, height);
    uint status = 0;
    return float4(luminance.Load(coords.x * coords.y, status).rrr, 1.0f);
}