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
        wild = vec3(0.18, 0.42, 0.16);
    } else if (height < 400.0) {
        wild = vec3(0.45, 0.32, 0.18);
    } else {
        wild = vec3(0.86, 0.86, 0.88);
    }
    float m = max(abs(vWorld.x - 960.0), abs(vWorld.z - 960.0));
    float urban = 1.0 - smoothstep(900.0, 1020.0, m);
    vec2 g = mod(vWorld.xz, 96.0);
    float street = max(max(step(g.x, 12.0), step(84.0, g.x)), max(step(g.y, 12.0), step(84.0, g.y)));
    vec3 lot = vec3(0.30, 0.30, 0.31);
    vec3 road = vec3(0.20, 0.20, 0.21);
    vec3 city = mix(lot, road, street);
    vec3 color = mix(wild, city, urban);
    o = vec4(color, 1.0);
}
