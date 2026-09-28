#version 330 core
in vec2 UV;
in vec3 FragPos;
out vec4 FragColor;
void main() {
    vec3 asphalt = vec3(0.20, 0.20, 0.20);
    vec3 sidewalk = vec3(0.55, 0.54, 0.50);
    vec3 curb = vec3(0.40, 0.40, 0.38);
    vec3 yellow = vec3(0.92, 0.78, 0.12);
    vec3 white = vec3(0.90, 0.90, 0.88);
    float ax = abs(UV.x - 0.5);
    float sidewalkMask = step(0.38, ax);
    float curbMask = step(0.36, ax) * (1.0 - sidewalkMask);
    float centerDash = (1.0 - step(0.012, abs(UV.x - 0.5))) * step(0.40, fract(UV.y * 6.0));
    float lane = (1.0 - step(0.008, abs(ax - 0.14))) * 0.35;
    vec3 c = mix(asphalt, curb, curbMask);
    c = mix(c, sidewalk, sidewalkMask);
    c = mix(c, yellow, centerDash);
    c = mix(c, white, lane * (1.0 - sidewalkMask));
    FragColor = vec4(c, 1.0);
}
