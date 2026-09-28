#version 330 core
precision highp float;
uniform vec3 uSunDir;
uniform float uTurb;
uniform vec2 uRes;
uniform mat4 uInvVP;
out vec4 o;

void main() {
    vec2 uv = gl_FragCoord.xy / max(uRes, vec2(1.0));
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 w = uInvVP * clip;
    vec3 V = abs(w.w) < 1e-6 ? vec3(0.0, 1.0, 0.0) : normalize(w.xyz / w.w);
    vec3 S = length(uSunDir) < 1e-4 ? normalize(vec3(0.45, 0.75, 0.35)) : normalize(uSunDir);
    vec3 zenith = vec3(0.102, 0.227, 0.416);
    vec3 horizon = vec3(0.529, 0.808, 0.922);
    float h = clamp(pow(max(V.y, 0.0), 0.45), 0.0, 1.0);
    vec3 c = mix(horizon, zenith, h);
    float ct = clamp(dot(V, S), -1.0, 1.0);
    float glow = smoothstep(0.985, 0.999, ct);
    float sun = smoothstep(0.994, 0.9994, ct);
    c += vec3(1.0, 0.92, 0.55) * glow * 0.85;
    c += vec3(1.0, 0.98, 0.82) * sun * 6.0;
    if (V.y < 0.0) c = mix(horizon * 0.35, c, 0.2);
    vec2 q = uv;
    float vig = smoothstep(1.15, 0.35, length(q - 0.5));
    c *= mix(0.88, 1.0, vig);
    o = vec4(c, 1.0);
}
