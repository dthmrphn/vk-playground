#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragCoord;

layout(binding = 1) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(texture(texSampler, fragCoord).rgb * fragColor * 10.0, 1.0);
}
