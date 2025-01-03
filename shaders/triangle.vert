#version 450

layout(binding = 0) uniform UBO {
    mat4 m;
    mat4 v;
    mat4 p;
} ubo;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.p * ubo.v * ubo.m * vec4(aPos, 0.0, 1.0);
    fragColor = aColor;
}
