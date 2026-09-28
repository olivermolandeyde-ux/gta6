#version 330 core
layout(location = 0) in vec2 aUv;
uniform mat4 uVP;
uniform float uChunk;
uniform float uOx;
uniform float uOz;
out vec3 vWorld;
out vec3 vN;
out float vH;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vn(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1, 0)), f.x),
               mix(hash21(i + vec2(0, 1)), hash21(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 6; i++) {
        v += a * vn(p);
        p = p * 2.07 + vec2(17.1, 9.7);
        a *= 0.5;
    }
    return v;
}
float ht(vec2 xz) {
    float continent = fbm(xz * 0.0022);
    float rolling = fbm(xz * 0.008);
    float n = vn(xz * 0.0031);
    float ridge = 1.0 - abs(n * 2.0 - 1.0);
    ridge *= ridge;
    float h = 6.0 + rolling * 42.0 + ridge * 280.0 + ridge * ridge * 360.0;
    h *= smoothstep(0.22, 0.58, continent);
    return h + 3.0;
}

void main() {
    vec2 xz = vec2(uOx, uOz) + aUv * uChunk;
    float h = ht(xz);
    vec3 wp = vec3(xz.x, h, xz.y);
    float e = 2.0;
    vec3 n = normalize(vec3(ht(xz) - ht(xz + vec2(e, 0.0)), e, ht(xz) - ht(xz + vec2(0.0, e))));
    vWorld = wp;
    vWorld.y = h;
    vN = n;
    vH = vWorld.y;
    gl_Position = uVP * vec4(vWorld, 1.0);
}
