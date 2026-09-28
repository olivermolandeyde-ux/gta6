#include <metal_stdlib>
using namespace metal;

struct SkyUniforms {
    float3   sunDir;
    float    turbidity;
    float3   sunColor;
    float    _pad0;
    float4x4 invViewProj;
    float2   resolution;
    float2   _pad1;
};

float3 calculateSkyColor(float3 viewDir, float3 sunDir, float turbidity) {
    float cosTheta = dot(viewDir, sunDir);
    float cosTheta2 = cosTheta * cosTheta;

    float3 betaR = float3(5.5e-6, 13.0e-6, 22.4e-6);
    float3 betaM = float3(21e-6) * turbidity;

    float rayleighPhase = 0.75 * (1.0 + cosTheta2);
    float g = 0.76;
    float miePhase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5);

    float3 skyColor = (betaR * rayleighPhase + betaM * miePhase) * 1000.0;

    float sunDisk = smoothstep(0.9995, 0.9999, cosTheta);
    skyColor += float3(1.0, 0.9, 0.7) * sunDisk * 10.0;

    float horizonFactor = pow(max(viewDir.y, 0.0), 0.4);
    skyColor *= mix(0.3, 1.0, horizonFactor);

    if (viewDir.y < 0.0) {
        skyColor *= 0.15;
    }
    return skyColor;
}

vertex float4 sky_vertex(uint vertexID [[vertex_id]],
                         constant SkyUniforms& uniforms [[buffer(0)]]) {
    float2 pos = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(pos * 2.0 - 1.0, 1.0, 1.0);
}

fragment float4 sky_fragment(float4 position [[position]],
                             constant SkyUniforms& uniforms [[buffer(0)]]) {
    float2 res = uniforms.resolution.x > 1.0 ? uniforms.resolution : float2(1280.0, 720.0);
    float2 uv = position.xy / res;
    float4 clipPos = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    float4 worldPos = uniforms.invViewProj * clipPos;
    float3 viewDir = normalize(worldPos.xyz / worldPos.w);

    float3 skyColor = calculateSkyColor(viewDir, uniforms.sunDir, uniforms.turbidity);
    skyColor = skyColor / (skyColor + float3(1.0));
    skyColor = pow(skyColor, float3(1.0 / 2.2));
    return float4(skyColor, 1.0);
}
