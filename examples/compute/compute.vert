#version 450

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aCoord;
layout(location = 0) out vec2 fragCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    fragCoord = aCoord;
}
