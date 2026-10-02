#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 7) in vec4 aColor;
layout(location = 3) in vec4 iM0;
layout(location = 4) in vec4 iM1;
layout(location = 5) in vec4 iM2;
layout(location = 6) in vec4 iM3;
layout(location = 8) in float iWheelAngle; // radians, per instance, 0 for everything but wheels
uniform mat4 view;
uniform mat4 projection;
uniform mat4 uLightVP;
// Wheel spin: zero for every primitive that is not a wheel, so trees, lamps and car
// bodies pay nothing. A wheel rotates about its own axle, in model space, so that one
// angle per instance serves every wheel primitive of the car (they are one rigid axle).
uniform int   uWheelCount;    // 0 = not a wheel primitive
uniform vec3  uWheelCenter[4]; // model space, one per wheel in this primitive
uniform vec3  uWheelAxis;      // unit, model space axle axis
uniform float uWheelRoll;      // +-1, so forward travel spins the wheels forward
out vec3 FragPos;
out vec3 Normal;
out vec2 UV;
out vec4 LightPos;
out vec4 VertColor;
void main() {
    mat4 model = mat4(iM0, iM1, iM2, iM3);
    vec3 p = aPos;
    vec3 n = aNormal;
    if (uWheelCount > 0) {
        vec3 c = uWheelCenter[0];
        float best = distance(aPos, uWheelCenter[0]);
        for (int i = 1; i < 4; ++i) {
            if (i >= uWheelCount) {
                break;
            }
            float d = distance(aPos, uWheelCenter[i]);
            if (d < best) {
                best = d;
                c    = uWheelCenter[i];
            }
        }
        float a  = iWheelAngle * uWheelRoll;
        float cs = cos(a);
        float sn = sin(a);
        vec3  v  = p - c;
        vec3  ax = uWheelAxis;
        p = c + v * cs + cross(ax, v) * sn + ax * (dot(ax, v) * (1.0 - cs));
        n = n * cs + cross(ax, n) * sn + ax * (dot(ax, n) * (1.0 - cs));
    }
    vec4 wp = model * vec4(p, 1.0);
    FragPos = wp.xyz;
    Normal = normalize(mat3(model) * n);
    UV = aUV;
    VertColor = aColor;
    LightPos = uLightVP * wp;
    gl_Position = projection * view * wp;
}
