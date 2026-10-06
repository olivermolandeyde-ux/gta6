#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
in vec4 LightPos;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 albedo;
uniform float roughness;
uniform float time_of_day;
uniform float emissionBoost;
uniform float floors;
uniform int district;
uniform int windowStyle;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform vec2 uRes;
uniform sampler2D uAlbedo;
uniform sampler2D uNormalTex;
uniform sampler2D uShadow;
uniform int uUseTex;
uniform int uAlphaLeaf;
uniform int uFacade;

out vec4 FragColor;

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth deterministic value noise (bilinear, no speckle) for ground mottle.
float ground_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float shadow_at() {
    vec3 p = LightPos.xyz / max(LightPos.w, 1e-4);
    p = p * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) {
        return 1.0;
    }
    float bias = 0.0025;
    float vis = 0.0;
    vec2 texel = 1.0 / vec2(1024.0);
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float d = texture(uShadow, p.xy + vec2(x, y) * texel).r;
            vis += (p.z - bias > d) ? 0.42 : 1.0;
        }
    }
    return vis / 25.0;
}

void main() {
    vec4 tex = vec4(1.0);
    if (uUseTex == 1) {
        tex = texture(uAlbedo, UV * vec2(1.6, max(floors, 1.0) * 0.28));
    }
    if (uAlphaLeaf == 1) {
        tex = texture(uAlbedo, UV);
        if (tex.a < 0.28) discard;
        tex.rgb = mix(vec3(0.22, 0.55, 0.18), vec3(0.40, 0.70, 0.28), hash12(UV * 12.0));
    }
    // Lot-grass mottle (windowStyle 2 marks the lot draw; dead otherwise):
    // 2 smooth octaves over world xz (~7 m and ~20 m patches), 0.85..1.10,
    // plus a darker damp blotch layer, then ~10% desat toward gray-green.
    if (uFacade == 0 && uUseTex == 1 && windowStyle == 2) {
        float mottle = ground_noise(FragPos.xz / 7.0) * 0.65 + ground_noise(FragPos.xz / 20.0 + 13.7) * 0.35;
        mottle = 0.85 + 0.25 * mottle;
        float damp = smoothstep(0.62, 0.78, ground_noise(FragPos.xz / 23.0 + 7.3));
        mottle *= (1.0 - 0.12 * damp);
        tex.rgb *= mottle;
        float lum = dot(tex.rgb, vec3(0.33));
        tex.rgb = mix(tex.rgb, vec3(lum) * vec3(0.92, 1.0, 0.92), 0.10);
    }

    vec3 N = normalize(Normal);
    if (uUseTex == 1 && uAlphaLeaf == 0) {
        vec3 nm = texture(uNormalTex, UV * 2.5).xyz * 2.0 - 1.0;
        vec3 T = abs(N.y) > 0.85 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), N));
        vec3 B = cross(N, T);
        N = normalize(T * nm.x + B * nm.y + N * nm.z);
    }

    vec3 L = normalize(lightDir);
    float ndl = max(dot(N, L), 0.0);
    float ao = 0.78 + 0.22 * max(N.y, 0.0);
    float sh = shadow_at();
    vec3 ambient = vec3(0.690, 0.769, 0.871) * 0.38;
    vec3 sunCol = vec3(1.0, 0.973, 0.863) * 1.15;
    vec3 lighting = (ambient + sunCol * ndl * sh) * ao;

    vec3 base = albedo * tex.rgb * 1.45;
    if (district == 0 && uAlphaLeaf == 0) {
        base = mix(vec3(0.275, 0.510, 0.706), base, 0.45);
    }

    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = (1.0 - smoothstep(5.5, 7.5, time_of_day)) * step(time_of_day, 12.0);
    float night = max(dusk, dawn);
    vec3 color = lighting * base;
    if (uFacade == 1) {
        float fl = max(floors, 2.0);
        float isWall = 1.0 - step(0.55, abs(N.y));
        float cols = district == 0 ? 11.0 : 7.0;
        if (windowStyle == 1) cols = 13.0;
        vec2 cell = vec2(UV.x * cols, UV.y * fl);
        vec2 cf = fract(cell);
        float frame = step(0.10, cf.x) * step(cf.x, 0.90) * step(0.14, cf.y) * step(cf.y, 0.90);
        float pane = step(0.20, cf.x) * step(cf.x, 0.80) * step(0.26, cf.y) * step(cf.y, 0.82);
        if (windowStyle == 1) pane = step(0.32, cf.x) * step(cf.x, 0.48);
        if (windowStyle == 2) pane *= step(0.4, hash12(floor(cell)));
        float shopBand = (district == 5 || district == 1) ? 1.0 : 0.0;
        float storefront = step(UV.y * fl, 1.12) * isWall * shopBand;
        float grime = pow(clamp(1.0 - UV.y * fl / 2.2, 0.0, 1.0), 1.4) * 0.22 * isWall;
        vec3 glass = mix(vec3(0.275, 0.510, 0.706), vec3(0.75, 0.82, 0.90), ndl * 0.5);
        glass += vec3(1.0, 0.88, 0.45) * night * 0.7;
        color = mix(color, vec3(0.18, 0.18, 0.20), frame * isWall * (1.0 - pane) * 0.55);
        color = mix(color, glass, pane * isWall * (1.0 - storefront) * 0.8);
        color *= (1.0 - pane * isWall * 0.08);
        color = mix(color, vec3(0.12, 0.13, 0.14), storefront * 0.55);
        color *= (1.0 - grime);
    }
    color += albedo * emissionBoost * (0.5 + night * 1.4);

    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 400.0, 0.0) * 0.0022);
    color = mix(color, uFogColor, clamp(fog, 0.0, 0.88));
    color = mix(color, vec3(dot(color, vec3(0.33))), 0.10);
    vec2 q = gl_FragCoord.xy / max(uRes, vec2(1.0));
    float vig = smoothstep(1.15, 0.35, length(q - 0.5));
    color *= mix(0.85, 1.0, vig);
    FragColor = vec4(color, 1.0);
}
