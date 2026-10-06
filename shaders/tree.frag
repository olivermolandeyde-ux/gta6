#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
in vec4 LightPos;
in vec4 VertColor;
uniform vec3 lightDir;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform vec2 uRes;
uniform sampler2D uAlbedo;
uniform sampler2D uEmissive;
uniform sampler2D uShadow;
uniform int uAlphaMask;
uniform float uAlphaCut;
uniform float uNightGlow;
uniform int uUseTexture;
uniform vec3 uSolidColor;
out vec4 FragColor;

float shadow_at() {
    vec3 p = LightPos.xyz / max(LightPos.w, 1e-4);
    p = p * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) {
        return 1.0;
    }
    float bias = 0.0035;
    float vis = 0.0;
    vec2 texel = 1.0 / vec2(1024.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float d = texture(uShadow, p.xy + vec2(x, y) * texel).r;
            vis += (p.z - bias > d) ? 0.42 : 1.0;
        }
    }
    return vis / 9.0;
}

void main() {
    vec4 albedo = texture(uAlbedo, UV);
    // Honor material alphaMode: OPAQUE (uAlphaMask==0) never discards, so opaque
    // walls cannot get see-through holes from low texture alpha. MASK discards
    // at the material cutoff (trees/leaves/glass).
    if (uAlphaMask > 0 && albedo.a < uAlphaCut) {
        discard;
    }
    if (albedo.r > 0.85 && albedo.b > 0.80 && albedo.g < 0.28) {
        discard;
    }
    vec4 tex = albedo * VertColor;
    tex.rgb = max(tex.rgb, vec3(0.04));
    vec3 emit = texture(uEmissive, UV).rgb;
    float em = max(max(emit.r, emit.g), emit.b);
    vec3 gold = vec3(1.0, 0.843, 0.0);
    vec3 nightEmit = emit;
    if (uNightGlow > 0.001) {
        if (em > 0.04) {
            nightEmit = gold * max(em, 0.65);
        } else {
            float glass = smoothstep(0.55, 0.90, tex.r) * smoothstep(0.40, 0.85, tex.g) *
                          (1.0 - smoothstep(0.25, 0.55, tex.b));
            nightEmit = gold * glass;
        }
    }
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightDir);
    float ndl = max(dot(N, L), 0.0);
    float sh = shadow_at();
    vec3 ambient = vec3(0.690, 0.769, 0.871) * 0.55;
    vec3 sunCol = vec3(1.0, 0.973, 0.863) * 1.15;
    vec3 lighting = ambient + sunCol * ndl * sh;
    vec3 color = lighting * tex.rgb + mix(emit * 1.8, nightEmit * 7.0, uNightGlow);
    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 400.0, 0.0) * 0.0022);
    color = mix(color, uFogColor, clamp(fog, 0.0, 0.88));
    color = mix(color, vec3(dot(color, vec3(0.33))), 0.08);
    vec2 q = gl_FragCoord.xy / max(uRes, vec2(1.0));
    float vig = smoothstep(1.15, 0.35, length(q - 0.5));
    color *= mix(0.85, 1.0, vig);
    FragColor = vec4(color, 1.0);
}
