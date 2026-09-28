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
uniform sampler2D uAlbedo;
uniform sampler2D uNormalTex;
uniform sampler2D uShadow;
uniform int uUseTex;
uniform int uAlphaLeaf;

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
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float d = texture(uShadow, p.xy + vec2(x, y) * texel).r;
            vis += (p.z - bias > d) ? 0.35 : 1.0;
        }
    }
    return vis / 9.0;
}

void main() {
    vec4 tex = uUseTex == 1 ? texture(uAlbedo, UV * vec2(2.0, max(floors, 1.0) * 0.35)) : vec4(1.0);
    if (uAlphaLeaf == 1) {
        tex = texture(uAlbedo, UV);
        if (tex.a < 0.35) discard;
    }

    vec3 N = normalize(Normal);
    if (uUseTex == 1 && uAlphaLeaf == 0) {
        vec3 nm = texture(uNormalTex, UV * 3.0).xyz * 2.0 - 1.0;
        vec3 T = abs(N.y) > 0.85 ? vec3(1.0, 0.0, 0.0) : normalize(cross(vec3(0.0, 1.0, 0.0), N));
        vec3 B = cross(N, T);
        N = normalize(T * nm.x + B * nm.y + N * nm.z);
    }

    vec3 L = normalize(lightDir);
    float ndl = max(dot(N, L), 0.0);
    float ao = 0.65 + 0.35 * max(N.y, 0.0);
    float sh = shadow_at();
    float wrap = (0.42 + 0.58 * ndl * (1.0 - roughness * 0.2)) * ao * sh;

    vec3 base = albedo * tex.rgb;
    if (uUseTex == 0) {
        if (district == 0) {
            base = mix(vec3(0.22, 0.32, 0.42), albedo, 0.3);
        }
    }

    float fl = max(floors, 2.0);
    float isWall = 1.0 - step(0.55, abs(N.y));
    float cols = district == 0 ? 12.0 : 7.0;
    if (windowStyle == 1) cols = 14.0;
    vec2 cell = vec2(UV.x * cols, UV.y * fl);
    vec2 cf = fract(cell);
    float pane = step(0.18, cf.x) * step(cf.x, 0.82) * step(0.22, cf.y) * step(cf.y, 0.86);
    if (windowStyle == 1) pane = step(0.30, cf.x) * step(cf.x, 0.50);
    if (windowStyle == 2) pane *= step(0.4, hash12(floor(cell)));
    float recess = pane * isWall * 0.22;
    float shopBand = (district == 5 || district == 1) ? 1.0 : 0.0;
    float storefront = step(UV.y * fl, 1.12) * isWall * shopBand;
    float grime = pow(1.0 - UV.y, 1.5) * 0.30 * isWall;

    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = (1.0 - smoothstep(5.5, 7.5, time_of_day)) * step(time_of_day, 12.0);
    float night = max(dusk, dawn);
    vec3 glass = mix(vec3(0.18, 0.22, 0.28), vec3(0.55, 0.62, 0.70), ndl);
    glass += vec3(1.0, 0.88, 0.50) * night * 0.55;
    vec3 color = max(base * wrap * lightColor, base * 0.35);
    color = mix(color, glass, pane * isWall * (1.0 - storefront) * 0.75);
    color *= (1.0 - recess);
    color = mix(color, vec3(0.10, 0.12, 0.14), storefront * 0.65);
    color *= (1.0 - grime);
    color += albedo * emissionBoost * (0.4 + night * 1.2);

    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 300.0, 0.0) * 0.0028);
    color = mix(color, uFogColor, clamp(fog, 0.0, 0.92));
    FragColor = vec4(color, 1.0);
}
