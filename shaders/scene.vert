#version 410 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in float aScalar;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out float vScalar;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal   = normalize(uNormalMat * aNormal);
    vColor    = aColor;
    vScalar   = aScalar;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
