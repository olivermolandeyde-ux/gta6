#version 330 core
uniform mat4 view;
uniform mat4 projection;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec2 uSize;
uniform float uRot;   // 0..3 : 0/90/180/270 deg UV rotation (per-billboard variety)
uniform float uFlip;  // 0/1  : mirror U (per-billboard variety)
uniform vec3 uCamPos;
out vec2 UV;
out vec2 CloudUV;
out vec3 ViewDir; // world-space view direction (for a true horizon fade)
void main() {
    vec2 c = vec2((gl_VertexID == 1 || gl_VertexID == 2 || gl_VertexID == 4) ? 1.0 : -1.0,
                  (gl_VertexID >= 2 && gl_VertexID != 3) ? 1.0 : -1.0);
    UV = c * 0.5 + 0.5;
    vec2 uv = UV - 0.5;
    if (uFlip > 0.5) {
        uv.x = -uv.x;
    }
    float ang = uRot * 1.5707963;
    float cs = cos(ang);
    float sn = sin(ang);
    uv = vec2(uv.x * cs - uv.y * sn, uv.x * sn + uv.y * cs);
    CloudUV = uv + 0.5;
    vec3 pos = uCenter + uRight * c.x * uSize.x + uUp * c.y * uSize.y;
    ViewDir = pos - uCamPos;
    gl_Position = projection * view * vec4(pos, 1.0);
}
