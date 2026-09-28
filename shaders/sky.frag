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
    vec3 betaR = vec3(5.5e-6, 13.0e-6, 22.4e-6);
    vec3 betaM = vec3(21e-6) * max(turb, 2.0);
    float ray = 0.75 * (1.0 + ct2);
    float g = 0.76;
    float mie = (1.0 - g * g) / max(pow(1.0 + g * g - 2.0 * g * ct, 1.5), 1e-4);
    vec3 c = (betaR * ray + betaM * mie) * 1000.0;
    c += vec3(1.0, 0.9, 0.7) * smoothstep(0.9995, 0.9999, ct) * 10.0;
    c *= mix(0.3, 1.0, pow(max(V.y, 0.0), 0.4));
    if (V.y < 0.0) c *= 0.15;
    return c;
}

void main() {
    vec3 blue = vec3(0.5, 0.7, 1.0);
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
    if (any(isnan(c)) || dot(c, c) < 1e-8) {
        o = vec4(blue, 1.0);
        return;
    }
    c = c / (c + vec3(1.0));
    c = pow(c, vec3(1.0 / 2.2));
    c = max(c, blue * 0.35);
    o = vec4(c, 1.0);
}
