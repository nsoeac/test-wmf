ByteAddressBuffer subsampled_texture : register(t0);

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
    // float2 normalised_offset = (input.position.xy + 1.0f) / 2.0f;
    // int2 coords = normalised_offset.xy * int2(width, height);
    // uint status = 0;
    // float luminance = subsampled_texture.Load(coords.x * coords.y, status) / 256.0f;
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}