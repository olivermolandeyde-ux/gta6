#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 view;
uniform mat4 projection;
out vec2 UV;
out vec3 FragPos;
void main() {
    UV = aUV;
    FragPos = aPos;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
