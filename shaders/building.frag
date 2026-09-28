#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 UV;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 albedo;
uniform float roughness;
uniform float time_of_day; // 0-24
uniform float emissionBoost;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, normalize(lightDir)), 0.0);
    vec3 diffuse = diff * lightColor;
    vec3 ambient = 0.18 * lightColor;

    float windowPattern = step(0.28, fract(UV.x * 10.0)) * step(0.32, fract(UV.y * 5.0));
    float dusk = smoothstep(18.0, 20.0, time_of_day);
    float dawn = 1.0 - smoothstep(5.5, 7.5, time_of_day);
    float nightFactor = max(dusk, dawn * step(time_of_day, 12.0));
    vec3 windowEmission = vec3(1.0, 0.9, 0.5) * windowPattern * nightFactor * 0.85;

    vec3 result = (ambient + diffuse * (1.0 - roughness * 0.4)) * albedo
                  + windowEmission + albedo * emissionBoost * nightFactor;
    FragColor = vec4(result, 1.0);
}
