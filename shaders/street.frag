#version 330 core
in vec2 UV;
out vec4 FragColor;
void main() {
    vec3 asphalt = vec3(0.12, 0.12, 0.13);
    vec3 sidewalk = vec3(0.42, 0.41, 0.38);
    vec3 paint = vec3(0.92, 0.86, 0.35);
    float edge = step(0.88, abs(UV.x * 2.0 - 1.0));
    float dash = step(0.45, fract(UV.y * 8.0)) * (1.0 - step(0.04, abs(UV.x - 0.5)));
    vec3 c = mix(asphalt, sidewalk, edge);
    c = mix(c, paint, dash);
    FragColor = vec4(c, 1.0);
}
