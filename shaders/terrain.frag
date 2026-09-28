#version 330 core
precision highp float;
in vec3 vWorld;
in vec3 vN;
in float vH;
out vec4 o;

void main() {
    float height = vWorld.y;
    vec3 color;
    if (height < 100.0) {
        color = vec3(0.1, 1.0, 0.1);
    } else if (height < 400.0) {
        color = vec3(1.0, 0.5, 0.0);
    } else {
        color = vec3(1.0, 1.0, 1.0);
    }
    o = vec4(color, 1.0);
}
