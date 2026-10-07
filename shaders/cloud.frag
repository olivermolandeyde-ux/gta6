#version 330 core
in vec3 ViewDir;
uniform sampler2D uCloudTex;
uniform int uHasAlpha;   // 1 = real alpha channel, 0 = luminance-as-alpha
uniform float uTime;     // seconds, cloud drift
uniform float uTimeOfDay;
out vec4 FragColor;
void main() {
    vec3 dir = normalize(ViewDir);
    if (dir.y <= 0.0) {
        discard; // below horizon: leave the clear/sky colour alone
    }
    vec2 plan = dir.xz / max(dir.y, 1e-3);
    // Layer A: full scale, drifting slowly (~1.8 m/s at 900 m tiles).
    vec2 uvA = plan / 900.0 + vec2(0.002, 0.0008) * uTime;
    vec4 tA = texture(uCloudTex, uvA);
    float aA = (uHasAlpha == 1) ? tA.a : dot(tA.rgb, vec3(0.299, 0.587, 0.114));
    // Layer B: ~0.45x scale, UV rotated 90 deg + offset, slower drift, weaker.
    vec2 planB = vec2(-plan.y, plan.x) + vec2(0.37, 0.73);
    vec2 uvB = planB / (900.0 * 0.45) + vec2(0.002, 0.0008) * (uTime * 0.6);
    vec4 tB = texture(uCloudTex, uvB);
    float aB = (uHasAlpha == 1) ? tB.a : dot(tB.rgb, vec3(0.299, 0.587, 0.114));
    vec3 colA = (uHasAlpha == 1) ? tA.rgb : vec3(1.0);
    vec3 colB = (uHasAlpha == 1) ? tB.rgb : vec3(1.0);
    float a = max(aA, aB * 0.7);
    vec3 col = (aA >= aB * 0.7) ? colA : colB;
    // Horizon melt: dissolve into the sky colour instead of forming a wall.
    a *= smoothstep(0.02, 0.18, dir.y);
    if (a < 0.01) discard;
    // Daylight tint: warm at dawn/dusk, white at midday.
    float warm = (1.0 - smoothstep(8.0, 11.0, uTimeOfDay)) + smoothstep(16.5, 19.5, uTimeOfDay);
    col = mix(col, col * vec3(1.15, 0.85, 0.70), clamp(warm, 0.0, 1.0) * 0.6);
    FragColor = vec4(col, a);
}
