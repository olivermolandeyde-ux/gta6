// Master PBR Shader - GGX Microfacet BRDF
cbuffer PerObject : register(b0) {
    float4x4 ObjectTransform;
    float4 AlbedoColor;
    float Roughness;
    float Metallic;
    float AO;
    float Alpha;
};

cbuffer PerFrame : register(b1) {
    float3 LightDirection;
    float LightIntensity;
    float3 CameraPosition;
    float Time;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (3.14159265 * d * d);
}

float G_SchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float4 PS_Main(PSInput input) : SV_TARGET {
    float3 N = normalize(input.Normal);
    float3 V = normalize(CameraPosition - input.WorldPos);
    float3 L = normalize(-LightDirection);
    float3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), AlbedoColor.rgb, Metallic);

    float3 F = F_Schlick(VdotH, F0);
    float D = D_GGX(NdotH, Roughness);
    float G = G_Smith(NdotV, NdotL, Roughness);

    float3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    float3 specular = numerator / denominator;

    float3 kD = (1.0 - F) * (1.0 - Metallic);
    float3 diffuse = kD * AlbedoColor.rgb / 3.14159265;

    float3 radiance = LightIntensity * (diffuse + specular) * NdotL;
    radiance *= AO;

    radiance = pow(radiance, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    return float4(radiance, Alpha);
}
