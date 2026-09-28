#version 330 core
precision highp float;
in vec3 vWorld;
in vec3 vN;
in float vH;
out vec4 o;

void main() {
    float height = vWorld.y;
    vec3 wild;
    if (height < 100.0) {
        wild = vec3(0.22, 0.40, 0.18);
    } else if (height < 400.0) {
        wild = vec3(0.45, 0.32, 0.18);
    } else {
        wild = vec3(0.86, 0.86, 0.88);
    }
    vec2 d = min(vWorld.xz, vec2(2400.0) - vWorld.xz);
    float urban = smoothstep(-220.0, 0.0, min(d.x, d.y));
    vec3 city = vec3(0.34, 0.34, 0.32);
    o = vec4(mix(wild, city, urban), 1.0);
}
