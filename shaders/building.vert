#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 uLightVP;
uniform vec3 uInvScale;

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;
out vec4 LightPos;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = normalize(vec3(aNormal.x * uInvScale.x, aNormal.y * uInvScale.y, aNormal.z * uInvScale.z));
    UV = aUV;
    LightPos = uLightVP * vec4(FragPos, 1.0);
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
