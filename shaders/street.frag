#version 330 core
in vec2 UV;
in vec3 FragPos;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
out vec4 FragColor;

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec3 asphalt = vec3(0.20, 0.20, 0.20);
    vec3 sidewalk = vec3(0.62, 0.61, 0.56);
    vec3 curb = vec3(0.45, 0.45, 0.43);
    vec3 yellow = vec3(0.95, 0.82, 0.10);
    vec3 white = vec3(0.96, 0.96, 0.94);
    float ax = abs(UV.x - 0.5);
    float sidewalkMask = step(0.385, ax);
    float curbMask = step(0.365, ax) * (1.0 - sidewalkMask);

    float n = hash12(floor(FragPos.xz * 1.7));
    asphalt *= (0.86 + 0.22 * n);
    float wet = step(0.82, hash12(floor(FragPos.xz * 0.11)));
    asphalt *= mix(1.0, 0.65, wet);

    float crack = step(0.97, hash12(floor(UV * vec2(30.0, 8.0)))) * sidewalkMask;
    sidewalk *= (1.0 - crack * 0.35);

    // 3 m dash / 3 m gap. UV.y is metres/10.
    float along_m = UV.y * 10.0;
    float dash = step(0.0, 3.0 - mod(along_m, 6.0));
    float centerDash = (1.0 - step(0.012, abs(UV.x - 0.5))) * dash * (1.0 - sidewalkMask);
    float edgeLine = (1.0 - step(0.010, abs(ax - 0.355))) * (1.0 - sidewalkMask);

    vec3 c = mix(asphalt, curb, curbMask);
    c = mix(c, sidewalk, sidewalkMask);
    c = mix(c, yellow, centerDash);
    c = mix(c, white, edgeLine);

    float d = length(FragPos.xz - uCamPos.xz);
    float fog = 1.0 - exp(-max(d - 300.0, 0.0) * 0.0028);
    c = mix(c, uFogColor, clamp(fog, 0.0, 0.92));
    FragColor = vec4(c, 1.0);
}
