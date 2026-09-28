#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 uLightVP;
out vec2 UV;
out vec3 FragPos;
out vec4 LightPos;
void main() {
    UV = aUV;
    FragPos = aPos;
    LightPos = uLightVP * vec4(aPos, 1.0);
    gl_Position = projection * view * vec4(aPos, 1.0);
}
