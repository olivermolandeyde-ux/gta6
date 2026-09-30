#version 330 core
in vec2 UV;
uniform sampler2D uAlbedo;
uniform int uAlphaMask;
uniform float uAlphaCut;
void main() {
    float a = texture(uAlbedo, UV).a;
    if (a < 0.5) {
        discard;
    }
}
