#include <metal_stdlib>
using namespace metal;

struct TerrainVertexIn {
    float3 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
};

struct TerrainVertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float2 uv;
    float  height;
};

struct TerrainUniforms {
    float4x4 viewProjection;
    float3   cameraPos;
    float    _pad0;
    float3   sunDir;
    float    _pad1;
    float3   sunColor;
    float    chunkOriginX;
    float    chunkOriginZ;
    float    chunkSize;
    float    heightScale;
    float    time_s;
};

float hash21(float2 p) {
    float3 p3 = fract(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float value_noise(float2 p) {
    float2 i = floor(p);
    float2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1, 0));
    float c = hash21(i + float2(0, 1));
    float d = hash21(i + float2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm2(float2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 6; ++i) {
        v += a * value_noise(p);
        p = p * 2.07 + float2(17.1, 9.7);
        a *= 0.5;
    }
    return v;
}

float sample_height(float2 xz) {
    float continent = fbm2(xz * 0.0022);
    float rolling   = fbm2(xz * 0.008);
    float n         = value_noise(xz * 0.0031);
    float ridge     = 1.0 - abs(n * 2.0 - 1.0);
    ridge = ridge * ridge;
    float h = 6.0 + rolling * 42.0 + ridge * 280.0 + ridge * ridge * 360.0;
    h *= smoothstep(0.22, 0.58, continent);
    h += 3.0;
    return h;
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

vertex TerrainVertexOut terrain_vertex(TerrainVertexIn in [[stage_in]],
                                       constant TerrainUniforms& uniforms [[buffer(1)]]) {
    TerrainVertexOut out;
    float2 xz = float2(uniforms.chunkOriginX, uniforms.chunkOriginZ) + in.uv * uniforms.chunkSize;
    float h = sample_height(xz) * (uniforms.heightScale > 0.0 ? uniforms.heightScale : 1.0);
    float3 wp = float3(xz.x, h, xz.y);
    const float eps = 1.6;
    float hx = sample_height(xz + float2(eps, 0.0));
    float hz = sample_height(xz + float2(0.0, eps));
    float3 n = normalize(float3(h - hx, eps, h - hz));
    out.worldPos = wp;
    out.worldPos.y = h;
    out.normal = n;
    out.uv = in.uv;
    out.height = out.worldPos.y;
    out.position = uniforms.viewProjection * float4(out.worldPos, 1.0);
    return out;
}

fragment float4 terrain_fragment(TerrainVertexOut in [[stage_in]],
                                 constant TerrainUniforms& uniforms [[buffer(1)]],
                                 texture2d<float> grassTex [[texture(0)]],
                                 texture2d<float> rockTex [[texture(1)]],
                                 texture2d<float> sandTex [[texture(2)]],
                                 texture2d<float> snowTex [[texture(3)]],
                                 texture2d<float> splatmap [[texture(4)]],
                                 sampler texSampler [[sampler(0)]]) {
    (void)in;
    (void)uniforms;
    (void)grassTex; (void)rockTex; (void)sandTex; (void)snowTex; (void)splatmap; (void)texSampler;
    return float4(0.0, 1.0, 0.0, 1.0);
}
