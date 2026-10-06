#version 330 core
in vec2 UV;
in vec2 CloudUV;
in vec3 ViewDir;
uniform sampler2D uCloudTex;
uniform int uUseTex;    // 1 = sample clouds.png, 0 = procedural blob (kept as-is)
uniform int uHasAlpha;  // 1 = real alpha channel, 0 = luminance-as-alpha
uniform float uTimeOfDay;
out vec4 FragColor;
float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
void main() {
    float a;
    vec3 col;
    if (uUseTex == 1) {
        vec4 tex = texture(uCloudTex, CloudUV);
        if (uHasAlpha == 1) {
            a = tex.a;
            col = tex.rgb;
        } else {
            // Black-background map: luminance carries the puff, colour is white.
            float lum = dot(tex.rgb, vec3(0.299, 0.587, 0.114));
            a = lum;
            col = vec3(1.0);
        }
        // Smooth UV-border fade: no square billboard edge, ever.
        vec2 e = min(CloudUV, 1.0 - CloudUV);
        a *= smoothstep(0.0, 0.12, min(e.x, e.y));
        // Horizon fade on TRUE world elevation (not view space, which would
        // erase everything below the view centre on a pitched camera).
        float dist = max(length(ViewDir), 1.0);
        float elev = clamp(ViewDir.y / dist, -1.0, 1.0);
        a *= smoothstep(0.02, 0.15, elev);
    } else {
        // Procedural path, untouched.
        vec2 p = (UV - 0.5) * 2.0;
        float d = length(p);
        float blob = smoothstep(1.0, 0.15, d);
        blob *= 0.75 + 0.25 * hash12(floor(UV * 8.0));
        float lobes = smoothstep(0.85, 0.2, length(p - vec2(-0.25, 0.05)))
                    + smoothstep(0.7, 0.2, length(p - vec2(0.28, 0.0)));
        a = clamp(blob * 0.55 + lobes * 0.25, 0.0, 0.75);
        float dd = length((UV - 0.5) * 2.0);
        col = mix(vec3(0.78, 0.80, 0.84), vec3(1.0), clamp(1.0 - dd, 0.0, 1.0));
    }
    if (a < 0.02) discard;
    // Daylight tint: warm at dawn/dusk, white at midday.
    float warm = (1.0 - smoothstep(8.0, 11.0, uTimeOfDay)) + smoothstep(16.5, 19.5, uTimeOfDay);
    col = mix(col, col * vec3(1.15, 0.85, 0.70), clamp(warm, 0.0, 1.0) * 0.6);
    FragColor = vec4(col, a);
}
