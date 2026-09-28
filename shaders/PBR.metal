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

// Packed to match engine::MetalPbrUniforms (256 bytes).
struct Uniforms {
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float3   cameraPos;
    float    _pad0;
    float3   lightDir;
    float    lightIntensity;
    float3   albedoColor;
    float    roughness;
    float    metallic;
    float    time_s;
    float2   _pad1;
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

    float3 albedo = uniforms.albedoColor;
    // Concrete grout on dielectric surfaces (ground). Metals keep a clean coat.
    float2 g = abs(fract(in.uv) - float2(0.5));
    float grout = smoothstep(0.47, 0.50, max(g.x, g.y));
    albedo *= mix(1.0, 0.78, grout * (1.0 - uniforms.metallic));

    float3 F0 = mix(float3(0.04), albedo, uniforms.metallic);

    float3 F = F_Schlick(VdotH, F0);
    float D = D_GGX(NdotH, uniforms.roughness);
    float G = G_Smith(NdotV, NdotL, uniforms.roughness);

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    float3 kD = (1.0 - F) * (1.0 - uniforms.metallic);
    float3 diffuse = kD * albedo / 3.14159265;

    // Tight analytic highlight so the metallic cube reads a moving sun glint.
    float shininess = mix(16.0, 256.0, 1.0 - uniforms.roughness);
    float3 specHighlight = F * pow(NdotH, shininess) * NdotL;

    float3 radiance = (diffuse + specular) * NdotL * uniforms.lightIntensity;
    radiance += specHighlight * uniforms.lightIntensity * (0.25 + 0.55 * uniforms.metallic);

    float3 ambient = float3(0.05, 0.07, 0.1);
    float hemi = 0.55 + 0.45 * max(N.y, 0.0);
    radiance += ambient * mix(albedo, F0, uniforms.metallic) * hemi;

    radiance = pow(max(radiance, float3(0.0)), float3(1.0 / 2.2));
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
