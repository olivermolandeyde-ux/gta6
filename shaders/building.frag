#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 UV;

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

out vec4 FragColor;

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 brick_albedo(vec3 base, float fl) {
    vec2 br = vec2(UV.x * 14.0, UV.y * max(fl, 2.0) * 3.2);
    if (mod(floor(br.y), 2.0) > 0.5) br.x += 0.5;
    vec2 f = fract(br);
    float mortar = 1.0 - step(0.10, f.x) * step(0.14, f.y);
    vec3 brick = base * vec3(1.15, 0.55, 0.42);
    return mix(brick, base * 0.55, mortar);
}

vec3 concrete_albedo(vec3 base) {
    float n = hash12(floor(UV * 48.0));
    return base * (0.88 + 0.18 * n);
}

vec3 glass_albedo(vec3 base, float ndl) {
    vec3 g = mix(vec3(0.22, 0.32, 0.42), vec3(0.55, 0.66, 0.78), ndl * 0.5 + 0.2);
    return mix(g, base, 0.25);
}

void main() {
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightDir);
    float ndl = max(dot(N, L), 0.0);
    float wrap = 0.50 + 0.50 * ndl * (1.0 - roughness * 0.25);
    float fl = max(floors, 2.0);
    float isWall = 1.0 - step(0.55, abs(N.y));

    vec3 base = albedo;
    if (district == 0) {
        base = glass_albedo(albedo, ndl);
    } else if (district == 1 || district == 4) {
        base = concrete_albedo(albedo);
    } else {
        base = brick_albedo(albedo, fl);
    }

    vec3 lit = max(base * wrap * lightColor, base * 0.45);

    float cols = 6.0;
    if (district == 0) cols = 12.0;
    if (district == 1) cols = 9.0;
    if (windowStyle == 1) cols = 14.0;
    if (windowStyle == 2) cols = 3.5;
    vec2 cell = vec2(UV.x * cols, UV.y * fl);
    vec2 cf = fract(cell);
    float pane = step(0.20, cf.x) * step(cf.x, 0.80) * step(0.26, cf.y) * step(cf.y, 0.84);
    if (windowStyle == 1) {
        pane = step(0.30, cf.x) * step(cf.x, 0.52) * isWall;
    }
    if (windowStyle == 2) {
        pane *= step(0.42, hash12(floor(cell)));
    }
    float shopBand = 0.0;
    if (district == 5 || district == 1) {
        shopBand = 1.0;
    }
    float storefront = step(UV.y * fl, 1.15) * isWall * shopBand;
    float grime = pow(1.0 - UV.y, 1.6) * 0.28 * isWall;

    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = (1.0 - smoothstep(5.5, 7.5, time_of_day)) * step(time_of_day, 12.0);
    float night = max(dusk, dawn);
    vec3 windowCol = mix(vec3(0.16, 0.18, 0.22), vec3(1.0, 0.88, 0.50), night * 0.85);

    vec3 color = mix(lit, windowCol, pane * isWall * (1.0 - storefront) * 0.70);
    color = mix(color, vec3(0.12, 0.14, 0.16), storefront * 0.70);
    color *= (1.0 - grime);
    color += albedo * emissionBoost * night;
    if (N.y > 0.6) {
        color = concrete_albedo(vec3(0.36, 0.36, 0.37)) * wrap;
    }

    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 300.0, 0.0) * 0.0028);
    color = mix(color, uFogColor, clamp(fog, 0.0, 0.92));
    FragColor = vec4(color, 1.0);
}
