#version 330 core
in vec2 UV;
out vec4 FragColor;
float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
void main() {
    vec2 p = (UV - 0.5) * 2.0;
    float d = length(p);
    float blob = smoothstep(1.0, 0.15, d);
    blob *= 0.75 + 0.25 * hash12(floor(UV * 8.0));
    float lobes = smoothstep(0.85, 0.2, length(p - vec2(-0.25, 0.05)))
                + smoothstep(0.7, 0.2, length(p - vec2(0.28, 0.0)));
    float a = clamp(blob * 0.55 + lobes * 0.25, 0.0, 0.75);
    if (a < 0.02) discard;
    vec3 col = mix(vec3(0.78, 0.80, 0.84), vec3(1.0), clamp(1.0 - d, 0.0, 1.0));
    FragColor = vec4(col, a);
}
