#version 450

layout(location = 0) in vec2 fragCoord;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D texSampler;

void main() {
    outColor = vec4(texture(texSampler, fragCoord).rgb, 1.0);
}
