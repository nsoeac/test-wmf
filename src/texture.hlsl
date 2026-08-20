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

uint get_luminance(uint2 position) {
    uint pixel_index = position.y * width + position.x;
    uint load_index = pixel_index & ~0x03;
    uint byte_index_in_load = pixel_index % 4;
    uint shift = byte_index_in_load * 8;
    uint mask = 0xFFu << shift;
    uint status = 0;
    uint loaded_value = subsampled_texture.Load(load_index, status);
    uint luminance = (loaded_value & mask) >> shift;
    return luminance;
}

uint2 get_chrominance(uint2 position) {
    uint image_size = width * height; // This is the start index for the chrominance values.
    uint byte_offset = (position.y / 2) * width + (position.x / 2) * 2;
    uint byte_index = image_size + byte_offset;
    uint load_index = byte_index & ~0x03;
    uint byte_index_in_load = byte_index % 4;
    uint shift = byte_index_in_load * 8;
    uint mask = 0xFFFFu << shift;
    uint status = 0;
    uint loaded_value = subsampled_texture.Load(load_index, status);
    uint shifted_loaded_value = (loaded_value & mask) >> shift;
    uint u = shifted_loaded_value & 0x00FF;
    uint v = (shifted_loaded_value & 0xFF00) >> 8;
    return uint2(u, v);
}

float4 PSMain(PSInput input) : SV_Target {
    uint2 position = uint2(input.position.x, input.position.y);
    uint luminance = get_luminance(position);
    uint2 chrominance = get_chrominance(position);
    int c = luminance - 16;
    int d = chrominance.x - 128;
    int e = chrominance.y - 128;
    float r = (298.0f * c + 0.0f * d + 459.0f * e + 128.0f) / 256.0f;
    float g = (298.0f * c - 55.0f * d - 137.0f * e + 128.0f) / 256.0f;
    float b = (298.0f * c + 541.0f * d + 0.0f * e + 128.0f) / 256.0f;
    return float4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}