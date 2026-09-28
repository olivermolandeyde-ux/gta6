#version 330 core
precision highp float;
uniform vec3 uSunDir;
uniform float uTurb;
uniform vec2 uRes;
uniform mat4 uInvVP;
out vec4 o;

vec3 skyColor(vec3 V, vec3 S, float turb) {
    float ct = clamp(dot(V, S), -1.0, 1.0);
    float ct2 = ct * ct;
    vec3 zenith = vec3(0.102, 0.227, 0.416);
    vec3 horizon = vec3(0.529, 0.808, 0.922);
    vec3 c = mix(horizon, zenith, clamp(pow(max(V.y, 0.0), 0.55), 0.0, 1.0));
    float ray = 0.75 * (1.0 + ct2);
    float g = 0.76;
    float mie = (1.0 - g * g) / max(pow(1.0 + g * g - 2.0 * g * ct, 1.5), 1e-4);
    c += vec3(0.55, 0.62, 0.75) * ray * 0.08 * max(turb, 2.0) * 0.15;
    c += vec3(1.0, 0.92, 0.70) * mie * 0.04;
    float sun = smoothstep(0.9994, 0.99985, ct);
    float glow = smoothstep(0.997, 0.9995, ct);
    c += vec3(1.0, 0.95, 0.75) * glow * 0.55;
    c += vec3(1.0, 0.98, 0.85) * sun * 3.5;
    if (V.y < 0.0) c = mix(horizon * 0.25, c, 0.15);
    return c;
}

void main() {
    vec3 blue = vec3(0.53, 0.81, 0.92);
    vec2 uv = gl_FragCoord.xy / max(uRes, vec2(1.0));
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 w = uInvVP * clip;
    if (abs(w.w) < 1e-6) {
        o = vec4(blue, 1.0);
        return;
    }
    vec3 V = normalize(w.xyz / w.w);
    vec3 S = length(uSunDir) < 1e-4 ? normalize(vec3(0.5, 0.8, 0.3)) : normalize(uSunDir);
    vec3 c = skyColor(V, S, uTurb);
    o = vec4(c, 1.0);
}
