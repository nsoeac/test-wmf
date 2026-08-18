SamplerState default_sampler : register(s0);
Texture2D<float4> default_texture : register(t0);

struct PSInput {
    float4 position : SV_POSITION;
};

PSInput VSMain(float4 position : SV_Position) {
    PSInput result;
    result.position = position;
    return result;
}

float4 PSMain(PSInput input) : SV_Target {
    return default_texture.Sample(default_sampler, input.position.xy);
}