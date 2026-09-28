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

out vec4 FragColor;

void main() {
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightDir);
    float ndl = max(dot(N, L), 0.0);
    // Hard floor so facades cannot go black even if the sun is below the horizon.
    float wrap = 0.55 + 0.45 * ndl * (1.0 - roughness * 0.25);
    vec3 lit = albedo * wrap * lightColor;
    lit = max(lit, albedo * 0.50);

    float fl = max(floors, 2.0);
    float isWall = 1.0 - step(0.55, abs(N.y));
    float cols = district == 0 ? 12.0 : (district == 1 ? 8.0 : 5.0);
    if (windowStyle == 1) cols = 14.0;
    if (windowStyle == 2) cols = 3.5;
    vec2 cell = vec2(UV.x * cols, UV.y * fl);
    vec2 cf = fract(cell);
    float pane = step(0.20, cf.x) * step(cf.x, 0.80) * step(0.26, cf.y) * step(cf.y, 0.84);
    if (windowStyle == 1) {
        pane = step(0.28, cf.x) * step(cf.x, 0.55);
    }
    if (windowStyle == 2) {
        pane *= step(0.45, fract(cell.x * 3.1 + cell.y * 1.7));
    }
    float shopBand = 0.0;
    if (district == 5 || district == 1) {
        shopBand = 1.0;
    }
    float storefront = step(UV.y * fl, 1.15) * isWall * shopBand;
    float grime = (1.0 - UV.y) * 0.18 * isWall;

    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = (1.0 - smoothstep(5.5, 7.5, time_of_day)) * step(time_of_day, 12.0);
    float night = max(dusk, dawn);

    vec3 windowCol = mix(vec3(0.18, 0.20, 0.24), vec3(1.0, 0.88, 0.50), night * 0.85);
    vec3 color = mix(lit, windowCol, pane * isWall * (1.0 - storefront) * 0.65);
    color = mix(color, vec3(0.16, 0.17, 0.18), storefront * 0.45);
    color *= (1.0 - grime);
    color += albedo * emissionBoost * night;

    if (N.y > 0.6) {
        color = max(albedo * 0.45, vec3(0.32, 0.32, 0.33)) * wrap;
    }
    FragColor = vec4(color, 1.0);
}
