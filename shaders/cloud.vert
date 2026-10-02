#version 330 core
uniform mat4 view;
uniform mat4 projection;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uSize;
out vec2 UV;
void main() {
    vec2 c = vec2((gl_VertexID == 1 || gl_VertexID == 2 || gl_VertexID == 4) ? 1.0 : -1.0,
                  (gl_VertexID >= 2 && gl_VertexID != 3) ? 1.0 : -1.0);
    UV = c * 0.5 + 0.5;
    vec3 pos = uCenter + uRight * c.x * uSize.x + uUp * c.y * uSize.y;
    gl_Position = projection * view * vec4(pos, 1.0);
}
