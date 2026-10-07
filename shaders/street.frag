#version 330 core
in vec2 UV;
in vec3 FragPos;
in vec4 LightPos;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform vec2 uRes;
uniform sampler2D uShadow;
uniform sampler2D uAsphaltTex;
uniform vec3 uAsphaltTint;
uniform int uHasAsphaltTex;
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
    // NOTE: no sidewalk band here on purpose — the sidewalk is its own textured
    // ring mesh. Road quads paint asphalt + centre dash + edge lines only, so no
    // sidewalk band can ever cross an intersection or a crosswalk.
    // Textured asphalt (world/4.0 m tiles) under the markings; flat legacy gray
    // when the PNG is missing. Noise/wet variation applies on top either way.
    vec3 asphalt = vec3(0.251, 0.251, 0.251);
    if (uHasAsphaltTex == 1) {
        asphalt = texture(uAsphaltTex, FragPos.xz / 4.0).rgb * uAsphaltTint;
    }
    vec3 yellow = vec3(1.0, 0.843, 0.0);
    vec3 white = vec3(1.0, 1.0, 1.0);
    float ax = abs(UV.x - 0.5);

    float n = hash12(floor(FragPos.xz * 2.2));
    asphalt *= (0.92 + 0.16 * n);
    float wet = step(0.86, hash12(floor(FragPos.xz * 0.09)));
    asphalt *= mix(1.0, 0.72, wet);

    float along_m = UV.y * 10.0;
    float dash = step(0.0, 3.0 - mod(along_m, 6.0));
    float centerDash = (1.0 - step(0.012, abs(UV.x - 0.5))) * dash;
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
    float edgeLine = (1.0 - step(0.008, abs(ax - 0.355)));
    edgeLine *= (1.0 - max(bandZ, bandX));

    // Baked zebra: the old geometry bars painted from world position
    // (setback 11.0, barlen 4.5, thick 0.9, pitch 1.8), so road + crosswalk
    // are ONE surface and no depth conflict is possible. Stripe/zone edges
    // are fwidth anti-aliased; far away the pattern dissolves into its mean
    // coverage (0.9/1.8 = 0.5) instead of moire. The |across| < 9.7 clamp
    // keeps paint off the sidewalk (curb at 10).
    float zebra = 0.0;
    {
        vec2 lp = mod(FragPos.xz + 60.0, 120.0) - 60.0; // signed dist to centreline
        // N/S arms: zone along z, stripes across x.
        float az = abs(lp.y);
        float fwz = fwidth(az) + 1e-4;
        float zoneNS = smoothstep(11.0 - fwz, 11.0 + fwz, az)
                     * (1.0 - smoothstep(15.5 - fwz, 15.5 + fwz, az));
        float fwx = fwidth(lp.x) + 1e-4;
        float stripeNS = 1.0 - smoothstep(0.9 - fwx, 0.9 + fwx, mod(lp.x + 0.9, 1.8));
        stripeNS = mix(0.5, stripeNS, 1.0 - smoothstep(0.45, 0.9, fwx));
        float acrossNS = 1.0 - smoothstep(9.7 - fwx, 9.7 + fwx, abs(lp.x));
        // E/W arms: mirrored (zone along x, stripes across z).
        float ax = abs(lp.x);
        float fwx2 = fwidth(ax) + 1e-4;
        float zoneEW = smoothstep(11.0 - fwx2, 11.0 + fwx2, ax)
                     * (1.0 - smoothstep(15.5 - fwx2, 15.5 + fwx2, ax));
        float fwy = fwidth(lp.y) + 1e-4;
        float stripeEW = 1.0 - smoothstep(0.9 - fwy, 0.9 + fwy, mod(lp.y + 0.9, 1.8));
        stripeEW = mix(0.5, stripeEW, 1.0 - smoothstep(0.45, 0.9, fwy));
        float acrossEW = 1.0 - smoothstep(9.7 - fwy, 9.7 + fwy, abs(lp.y));
        zebra = max(zoneNS * stripeNS * acrossNS, zoneEW * stripeEW * acrossEW);
    }

    vec3 c = asphalt;
    c = mix(c, yellow, centerDash);
    c = mix(c, white, edgeLine);
    c = mix(c, vec3(0.94), zebra);
    c *= (0.55 + 0.45 * shadow_at());

    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 400.0, 0.0) * 0.0022);
    c = mix(c, uFogColor, clamp(fog, 0.0, 0.88));
    vec2 q = gl_FragCoord.xy / max(uRes, vec2(1.0));
    float vig = smoothstep(1.15, 0.35, length(q - 0.5));
    c *= mix(0.85, 1.0, vig);
    FragColor = vec4(c, 1.0);
}
