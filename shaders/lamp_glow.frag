#version 330 core
in vec2 UV;
uniform float uGlow;
out vec4 FragColor;
void main() {
    float d = length(UV);
    float a = smoothstep(1.0, 0.12, d) * uGlow * 0.62;
    if (a < 0.012) {
        discard;
    }
    FragColor = vec4(1.0, 0.843, 0.0, a);
}
