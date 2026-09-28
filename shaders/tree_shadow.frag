#version 330 core
in vec2 UV;
uniform sampler2D uAlbedo;
uniform int uAlphaMask;
uniform float uAlphaCut;
void main() {
    if (uAlphaMask == 1) {
        float a = texture(uAlbedo, UV).a;
        if (a < uAlphaCut) {
            discard;
        }
    }
}
