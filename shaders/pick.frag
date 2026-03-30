#version 410 core
uniform vec3 uPickColor;
out vec4 outColor;
void main() {
    outColor = vec4(uPickColor, 1.0);
}
