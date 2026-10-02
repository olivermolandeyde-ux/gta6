#include <metal_stdlib>
using namespace metal;

struct CloudUniforms {
    float4x4 invViewProj;
    float3   cameraPos;
    float    time;
    float3   sunDir;
    float    cloudCoverage;
    float3   sunColor;
    float    layerAltitude;
    float    layerThickness;
    float2   resolution;
    float    _pad;
};

float hash(float3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise3D(float3 x) {
    float3 i = floor(x);
    float3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + float3(0, 0, 0)), hash(i + float3(1, 0, 0)), f.x),
                   mix(hash(i + float3(0, 1, 0)), hash(i + float3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + float3(0, 0, 1)), hash(i + float3(1, 0, 1)), f.x),
                   mix(hash(i + float3(0, 1, 1)), hash(i + float3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(float3 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise3D(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

vertex float4 cloud_vertex(uint vertexID [[vertex_id]]) {
    float2 pos = float2((vertexID << 1) & 2, vertexID & 2);
    return float4(pos * 2.0 - 1.0, 0.999, 1.0);
}

fragment float4 cloud_fragment(float4 position [[position]],
                               constant CloudUniforms& uniforms [[buffer(0)]]) {
    float2 res = uniforms.resolution.x > 1.0 ? uniforms.resolution : float2(1280.0, 720.0);
    float2 uv = position.xy / res;
    float4 clipPos = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    float4 worldPos = uniforms.invViewProj * clipPos;
    float3 rayDir = normalize(worldPos.xyz / worldPos.w);

    if (abs(rayDir.y) < 0.001) {
        return float4(0.0);
    }
    float t = (uniforms.layerAltitude - uniforms.cameraPos.y) / rayDir.y;
    if (t < 0.0) {
        return float4(0.0);
    }

    float3 hitPos = uniforms.cameraPos + rayDir * t;

    float totalTransmittance = 1.0;
    float3 totalScattering = float3(0.0);
    float stepSize = uniforms.layerThickness / 16.0;

    for (int i = 0; i < 16; i++) {
        float3 samplePos = hitPos + rayDir * stepSize * float(i);
        samplePos.xz += uniforms.time * 10.0;

        float density = fbm(samplePos * 0.001);
        density = smoothstep(1.0 - uniforms.cloudCoverage, 1.0, density);

        if (density > 0.01) {
            float lightDist = stepSize * 4.0;
            float3 lightSamplePos = samplePos + uniforms.sunDir * lightDist;
            float lightDensity = fbm(lightSamplePos * 0.001);
            float transmittance = exp(-lightDensity * lightDist * 0.1);

            float3 scattering = uniforms.sunColor * density * transmittance * stepSize * 0.01;
            totalScattering += scattering * totalTransmittance;
            totalTransmittance *= exp(-density * stepSize * 0.1);

            if (totalTransmittance < 0.01) {
                break;
            }
        }
    }

    return float4(totalScattering, 1.0 - totalTransmittance);
}
