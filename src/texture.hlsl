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
    uint pixel_index = input.position.y * width + input.position.x;
    uint load_index = pixel_index & ~0x03;
    uint byte_index_in_load = pixel_index % 4;
    uint shift = byte_index_in_load * 8;
    uint mask = 0xFFu << shift;
    uint status = 0;
    uint loaded_value = subsampled_texture.Load(load_index, status);
    uint result = (loaded_value & mask) >> shift;
    float magnitude = result / 255.0f;
    return float4(magnitude, magnitude, magnitude, 1.0f);
}