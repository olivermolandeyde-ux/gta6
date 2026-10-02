// Pre-integrated Subsurface Scattering for Human Skin
cbuffer SkinParams : register(b2) {
    float3 SSS_Color;
    float SSS_Scale;
    float3 SSS_Ambient;
    float _pad;
};

Texture2D PreintegratedSSS_LUT : register(t0);
SamplerState LinearClampSampler : register(s0);

float3 CalculateSkinShading(PSInput input, float3 N, float3 V, float3 L, float3 albedo, float lightIntensity) {
    float NdotL = dot(N, L);
    float curvature = 0.5; 
    
    float wrap = 0.2;
    float wrappedNdotL = (NdotL + wrap) / (1.0 + wrap);
    float diffusion = max(wrappedNdotL, 0.0);

    float scatter = SSS_Scale * (1.0 - NdotL);
    float2 lutUV = float2(curvature, scatter);
    float3 sssProfile = PreintegratedSSS_LUT.Sample(LinearClampSampler, lutUV).rgb;

    float3 finalColor = (albedo * sssProfile * diffusion * lightIntensity) + (SSS_Ambient * albedo);
    return finalColor;
}
