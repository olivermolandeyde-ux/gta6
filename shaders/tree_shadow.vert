#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
layout(location = 8) in float iWheelAngle; // same spin as the colour pass, so wheels keep rolling in the shadow
layout(location = 3) in vec4 iM0;
layout(location = 4) in vec4 iM1;
layout(location = 5) in vec4 iM2;
layout(location = 6) in vec4 iM3;
uniform mat4 uLightVP;
// Wheel spin, identical to tree.vert: a wheel is a solid of revolution, so its outline
// barely changes, but spokes and nuts in the shadow have to turn with the wheel.
uniform int   uWheelCount;
uniform vec3  uWheelCenter[4];
uniform vec3  uWheelAxis;
uniform float uWheelRoll;
out vec2 UV;
void main() {
    mat4 model = mat4(iM0, iM1, iM2, iM3);
    vec3 p = aPos;
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
    }
    UV = aUV;
    gl_Position = uLightVP * model * vec4(p, 1.0);
}

