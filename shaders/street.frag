#version 330 core
in vec2 UV;
in vec3 FragPos;
out vec4 FragColor;
void main() {
    vec3 asphalt = vec3(0.20, 0.20, 0.20);
    vec3 sidewalk = vec3(0.62, 0.61, 0.56);
    vec3 curb = vec3(0.45, 0.45, 0.43);
    vec3 yellow = vec3(0.95, 0.82, 0.10);
    vec3 white = vec3(0.92, 0.92, 0.90);
    float ax = abs(UV.x - 0.5);
    // 26 m total: 20 m asphalt + 3 m sidewalk each side → sidewalk at ax > 10/26.
    float sidewalkMask = step(0.385, ax);
    float curbMask = step(0.365, ax) * (1.0 - sidewalkMask);
    float asphaltNoise = 0.04 * fract(sin(dot(UV, vec2(12.9898, 78.233))) * 43758.5453);
    asphalt *= (0.92 + asphaltNoise);
    if (UV.y < 0.0) {
        float stripe = step(0.45, fract(UV.x * 7.0));
        FragColor = vec4(mix(asphalt, white, stripe), 1.0);
        return;
    }
    float centerDash = (1.0 - step(0.014, abs(UV.x - 0.5))) * step(0.42, fract(UV.y * 5.0));
    float lane = (1.0 - step(0.010, abs(ax - 0.12))) * 0.55;
    vec3 c = mix(asphalt, curb, curbMask);
    c = mix(c, sidewalk, sidewalkMask);
    c = mix(c, yellow, centerDash);
    c = mix(c, white, lane * (1.0 - sidewalkMask));
    FragColor = vec4(c, 1.0);
}
