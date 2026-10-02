#version 330 core
in vec2 UV;
uniform sampler2D uAlbedo;
uniform int uAlphaMask;
uniform float uAlphaCut;
void main() {
    vec4 albedo = texture(uAlbedo, UV);
    if (albedo.a < 0.5) {
        discard;
    }
    if (albedo.r > 0.85 && albedo.b > 0.80 && albedo.g < 0.28) {
        discard;
    }
}
