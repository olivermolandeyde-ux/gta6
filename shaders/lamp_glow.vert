#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 iM0;
layout(location = 4) in vec4 iM1;
layout(location = 5) in vec4 iM2;
layout(location = 6) in vec4 iM3;
uniform mat4 view;
uniform mat4 projection;
out vec2 UV;
void main() {
    mat4 model = mat4(iM0, iM1, iM2, iM3);
    vec4 wp = model * vec4(aPos, 1.0);
    UV = aPos.xz;
    gl_Position = projection * view * wp;
}
