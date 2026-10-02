// Anisotropic tire rubber shader with dynamic wetness and skid mark intensity output.
cbuffer TireParams : register(b4) {
    float WearLevel; // 0.0 (new) to 1.0 (bald)
    float Wetness; // 0.0 (dry) to 1.0 (soaking wet)
    float SlipIntensity; // Passed from CPU based on slip_ratio and slip_angle
};

float3 CalculateTireShading(PSInput input, float3 N, float3 V, float3 L, float3 tangent) {
    // Anisotropic highlight simulation (simplified)
    float3 H = normalize(V + L);
    float TdotH = dot(tangent, H);
    float anisotropic = pow(1.0 - abs(TdotH), 4.0);
    
    float3 baseRubber = float3(0.05, 0.05, 0.05);
    float3 wetSpecular = float3(0.1, 0.1, 0.1) * Wetness;
    
    // Skid mark intensity: High slip + low wear = more rubber deposition
    float skidIntensity = saturate(SlipIntensity * (1.0 - WearLevel));
    
    float3 finalColor = baseRubber + (wetSpecular * anisotropic);
    
    // Output skid intensity to an unused render target or encode in alpha for decal system
    return float4(finalColor, skidIntensity);
}
