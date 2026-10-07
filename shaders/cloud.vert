#version 330 core
uniform vec3 uLook;
uniform vec3 uRight;
uniform vec3 uUpC;
uniform vec2 uTan; // (tanHalfH, tanHalfV)
out vec3 ViewDir;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    ViewDir = normalize(uLook + uRight * p.x * uTan.x + uUpC * p.y * uTan.y);
    gl_Position = vec4(p, 1.0, 1.0);
}
