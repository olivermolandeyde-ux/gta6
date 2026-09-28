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

out vec4 FragColor;

void main() {
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightDir);
    float ndl = max(dot(N, L), 0.0);
    // Hard floor so facades cannot go black even if the sun is below the horizon.
    float wrap = 0.55 + 0.45 * ndl;
    vec3 lit = albedo * wrap * lightColor;
    lit = max(lit, albedo * 0.50);

    float fl = max(floors, 3.0);
    float isWall = 1.0 - step(0.55, abs(N.y));
    float cols = district == 0 ? 10.0 : 6.0;
    vec2 cell = vec2(UV.x * cols, UV.y * fl);
    vec2 cf = fract(cell);
    float pane = step(0.22, cf.x) * step(cf.x, 0.78) * step(0.28, cf.y) * step(cf.y, 0.82);
    float storefront = step(UV.y * fl, 1.05) * isWall;

    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = (1.0 - smoothstep(5.5, 7.5, time_of_day)) * step(time_of_day, 12.0);
    float night = max(dusk, dawn);

    vec3 windowCol = mix(vec3(0.18, 0.20, 0.24), vec3(1.0, 0.88, 0.50), night * 0.85);
    vec3 color = mix(lit, windowCol, pane * isWall * (1.0 - storefront) * 0.65);
    color = mix(color, vec3(0.16, 0.17, 0.18), storefront * 0.45);
    color += albedo * emissionBoost * night;

    if (N.y > 0.6) {
        color = max(albedo * 0.45, vec3(0.32, 0.32, 0.33)) * wrap;
    }
    FragColor = vec4(color, 1.0);
    (void)roughness;
}
