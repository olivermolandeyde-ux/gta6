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

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 N = normalize(Normal);
    float ndl = max(dot(N, normalize(lightDir)), 0.0);
    vec3 ambient = 0.22 * lightColor;
    vec3 diffuse = ndl * lightColor;

    float fl = max(floors, 3.0);
    vec3 base = albedo;
    float isWall = 1.0 - step(0.55, abs(N.y));

    // Masonry / glass / corrugated by district.
    vec2 brick = vec2(UV.x * 10.0, UV.y * fl * 2.4);
    if (mod(floor(brick.y), 2.0) > 0.5) brick.x += 0.5;
    vec2 bf = fract(brick);
    float mortar = step(0.10, bf.x) * step(0.14, bf.y);
    vec3 brickCol = mix(albedo * 0.55, albedo, mortar);

    float corrug = 0.85 + 0.15 * sin(UV.x * 70.0);
    vec3 industrial = albedo * corrug;

    vec3 glass = mix(vec3(0.18, 0.22, 0.28), vec3(0.45, 0.52, 0.58), ndl);
    vec3 facade = brickCol;
    if (district == 0) facade = mix(glass, albedo * 0.7, 0.35);
    if (district == 2) facade = industrial;
    facade = mix(albedo * 0.75, facade, isWall);

    // Recessed window cells (skip roof/ground slab).
    float cols = district == 0 ? 12.0 : 7.0;
    vec2 cell = vec2(UV.x * cols, UV.y * fl);
    vec2 cf = fract(cell);
    float frame = step(0.16, cf.x) * step(cf.x, 0.84) * step(0.22, cf.y) * step(cf.y, 0.88);
    float pane = step(0.22, cf.x) * step(cf.x, 0.78) * step(0.30, cf.y) * step(cf.y, 0.82);
    float storey = UV.y * fl;
    float storefront = step(storey, 1.05) * isWall;
    float lit = hash12(floor(cell) + vec2(float(district), 3.1));

    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = (1.0 - smoothstep(5.5, 7.5, time_of_day)) * step(time_of_day, 12.0);
    float night = max(dusk, dawn);

    vec3 windowWell = vec3(0.05, 0.055, 0.07);
    vec3 paneCol = mix(vec3(0.12, 0.14, 0.16), vec3(0.20, 0.22, 0.24), ndl * 0.4);
    vec3 nightGlow = vec3(1.0, 0.86, 0.45) * step(0.35, lit) * night;
    vec3 win = mix(windowWell, paneCol + nightGlow * 0.9, pane);

    vec3 shopGlass = vec3(0.10, 0.11, 0.12) + nightGlow * 0.5;
    vec3 shopFrame = albedo * 0.45;
    vec3 shop = mix(shopFrame, shopGlass, step(0.08, UV.x) * step(UV.x, 0.92) * step(0.08, UV.y));

    vec3 color = facade;
    color = mix(color, win, frame * isWall * (1.0 - storefront) * (district == 2 ? 0.35 : 1.0));
    color = mix(color, shop, storefront * 0.85);
    color = (ambient + diffuse * (1.0 - roughness * 0.45)) * color;
    color += nightGlow * pane * isWall * 0.35;
    color += albedo * emissionBoost * night;

    // Roof gravel.
    if (N.y > 0.6) {
        float grit = 0.85 + 0.15 * hash12(FragPos.xz * 0.35);
        color = (ambient + diffuse) * mix(albedo * 0.55, vec3(0.28, 0.28, 0.29), 0.5) * grit;
    }

    FragColor = vec4(color, 1.0);
}
