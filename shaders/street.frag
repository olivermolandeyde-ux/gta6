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
    float sidewalkMask = step(0.36, ax);
    float curbMask = step(0.33, ax) * (1.0 - sidewalkMask);
    float centerDash = (1.0 - step(0.014, abs(UV.x - 0.5))) * step(0.42, fract(UV.y * 5.0));
    float lane = (1.0 - step(0.010, abs(ax - 0.12))) * 0.55;
    vec3 c = mix(asphalt, curb, curbMask);
    c = mix(c, sidewalk, sidewalkMask);
    c = mix(c, yellow, centerDash);
    c = mix(c, white, lane * (1.0 - sidewalkMask));
    FragColor = vec4(c, 1.0);
}
