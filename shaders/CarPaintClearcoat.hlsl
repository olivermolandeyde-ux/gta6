// Multi-layer PBR shader for automotive paint (Base coat + Clear coat).
cbuffer CarPaintParams : register(b3) {
    float3 BaseAlbedo;
    float BaseRoughness;
    float BaseMetallic;
    float3 ClearcoatAlbedo; // Usually white/gray
    float ClearcoatRoughness; // Very low (0.05 - 0.1)
    float ClearcoatWeight; // 0.0 to 1.0
};

// Re-use D_GGX, G_Smith, F_Schlick from MasterPBR.hlsl
float3 CalculateCarPaintShading(PSInput input, float3 N, float3 V, float3 L) {
    float3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // 1. Base Layer (Metallic Paint)
    float3 F0_base = lerp(float3(0.04, 0.04, 0.04), BaseAlbedo, BaseMetallic);
    float3 F_base = F_Schlick(VdotH, F0_base);
    float D_base = D_GGX(NdotH, BaseRoughness);
    float G_base = G_Smith(NdotV, NdotL, BaseRoughness);
    float3 specular_base = (D_base * G_base * F_base) / max(4.0 * NdotV * NdotL, 0.001);
    float3 kD_base = (1.0 - F_base) * (1.0 - BaseMetallic);
    float3 radiance_base = (kD_base * BaseAlbedo / 3.14159 + specular_base) * NdotL;

    // 2. Clear Coat Layer (Top dielectric layer, fixed F0 = 0.04)
    float3 F0_clear = float3(0.04, 0.04, 0.04);
    float3 F_clear = F_Schlick(VdotH, F0_clear);
    float D_clear = D_GGX(NdotH, ClearcoatRoughness);
    float G_clear = G_Smith(NdotV, NdotL, ClearcoatRoughness);
    float3 specular_clear = (D_clear * G_clear * F_clear) / max(4.0 * NdotV * NdotL, 0.001);
    
    // Clear coat absorbs some light from reaching the base layer (energy conservation)
    float3 finalRadiance = radiance_base * (1.0 - ClearcoatWeight * F_clear) + (specular_clear * ClearcoatWeight * NdotL);
    
    return pow(finalRadiance, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}
