#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float2 uv;
};

struct Uniforms {
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float3   cameraPos;
    float3   lightDir;
    float    lightIntensity;
    float3   albedoColor;
    float    roughness;
    float    metallic;
};

float D_GGX(float NdotH, float roughness);
float G_SchlickGGX(float NdotV, float roughness);
float G_Smith(float NdotV, float NdotL, float roughness);
float3 F_Schlick(float cosTheta, float3 F0);

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    float4 worldPos = uniforms.modelMatrix * float4(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = uniforms.projectionMatrix * uniforms.viewMatrix * worldPos;
    out.normal = normalize((uniforms.modelMatrix * float4(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]]) {
    float3 N = normalize(in.normal);
    float3 V = normalize(uniforms.cameraPos - in.worldPos);
    float3 L = normalize(-uniforms.lightDir);
    float3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float3 F0 = mix(float3(0.04), uniforms.albedoColor, uniforms.metallic);

    float3 F = F_Schlick(VdotH, F0);
    float D = D_GGX(NdotH, uniforms.roughness);
    float G = G_Smith(NdotV, NdotL, uniforms.roughness);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    float3 kD = (1.0 - F) * (1.0 - uniforms.metallic);
    float3 diffuse = kD * uniforms.albedoColor / 3.14159265;

    float3 radiance = (diffuse + specular) * NdotL * uniforms.lightIntensity;
    radiance = pow(radiance, float3(1.0 / 2.2));
    return float4(radiance, 1.0);
}

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
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
