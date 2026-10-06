#version 330 core
in vec2 UV;
in vec3 FragPos;
in vec4 LightPos;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform vec2 uRes;
uniform sampler2D uShadow;
out vec4 FragColor;

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float shadow_at() {
    vec3 p = LightPos.xyz / max(LightPos.w, 1e-4);
    p = p * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) {
        return 1.0;
    }
    float bias = 0.003;
    float vis = 0.0;
    vec2 texel = 1.0 / vec2(1024.0);
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float d = texture(uShadow, p.xy + vec2(x, y) * texel).r;
            vis += (p.z - bias > d) ? 0.45 : 1.0;
        }
    }
    return vis / 25.0;
}

void main() {
    vec3 asphalt = vec3(0.251, 0.251, 0.251);
    vec3 sidewalk = vec3(0.827, 0.827, 0.827);
    vec3 curb = vec3(0.35, 0.35, 0.34);
    vec3 yellow = vec3(1.0, 0.843, 0.0);
    vec3 white = vec3(1.0, 1.0, 1.0);
    float ax = abs(UV.x - 0.5);
    float sidewalkMask = step(0.385, ax);
    float curbMask = step(0.365, ax) * (1.0 - sidewalkMask);

    float n = hash12(floor(FragPos.xz * 2.2));
    asphalt *= (0.92 + 0.16 * n);
    float wet = step(0.86, hash12(floor(FragPos.xz * 0.09)));
    asphalt *= mix(1.0, 0.72, wet);

    vec2 slab = fract(FragPos.xz / 2.0);
    float seam = 1.0 - step(0.04, min(slab.x, slab.y)) * step(min(slab.x, slab.y), 0.96);
    sidewalk = mix(sidewalk * (0.96 + 0.06 * n), sidewalk * 0.70, seam * sidewalkMask);

    float along_m = UV.y * 10.0;
    float dash = step(0.0, 3.0 - mod(along_m, 6.0));
    float centerDash = (1.0 - step(0.012, abs(UV.x - 0.5))) * dash * (1.0 - sidewalkMask);
    // Suppress the centre dash inside the junction box and the zebra bands
    // (bars span +-8.5 m across, 11..14.5 m out on every approach), so no
    // yellow pokes through the white stripes. Geometry contract: pitch 120,
    // road half-width 10.
    vec2 gmod = mod(FragPos.xz, 120.0);
    vec2 dd = min(gmod, vec2(120.0) - gmod);
    float inBox = (1.0 - step(11.0, dd.x)) * (1.0 - step(11.0, dd.y));
    float bandZ = (1.0 - step(9.5, dd.x)) * step(10.5, dd.y) * (1.0 - step(15.5, dd.y));
    float bandX = (1.0 - step(9.5, dd.y)) * step(10.5, dd.x) * (1.0 - step(15.5, dd.x));
    centerDash *= (1.0 - max(inBox, max(bandZ, bandX)));
    // Gap the dark curb/gutter ring where pedestrians cross (same zebra zones),
    // so the crossing reads sidewalk-to-sidewalk instead of over a gutter slash.
    // Asphalt shows through the gap; the white bars sit on top of it.
    float crossZ = (1.0 - step(10.0, dd.x)) * step(10.5, dd.y) * (1.0 - step(15.5, dd.y));
    float crossX = (1.0 - step(10.0, dd.y)) * step(10.5, dd.x) * (1.0 - step(15.5, dd.x));
    curbMask *= (1.0 - max(crossZ, crossX));
    float edgeLine = (1.0 - step(0.008, abs(ax - 0.355))) * (1.0 - sidewalkMask);

    vec3 c = mix(asphalt, curb, curbMask);
    c = mix(c, sidewalk, sidewalkMask);
    c = mix(c, yellow, centerDash);
    c = mix(c, white, edgeLine);
    c *= (0.55 + 0.45 * shadow_at());

    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 400.0, 0.0) * 0.0022);
    c = mix(c, uFogColor, clamp(fog, 0.0, 0.88));
    vec2 q = gl_FragCoord.xy / max(uRes, vec2(1.0));
    float vig = smoothstep(1.15, 0.35, length(q - 0.5));
    c *= mix(0.85, 1.0, vig);
    FragColor = vec4(c, 1.0);
}
