#version 320 es

precision highp float;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inFog;

layout(std140, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fogColor;
    float fogStart;
    float fogEnd;
    vec2 padding;
} ubo;

out vec2 fragTexCoord;
out vec4 fragColor;
out float fragFog;

void main() {

    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    gl_Position = ubo.proj * viewPos;


    float fogDistance = length(viewPos.xyz);
    fragFog = clamp((fogDistance - ubo.fogStart) / (ubo.fogEnd - ubo.fogStart), 0.0, 1.0);

    fragTexCoord = inTexCoord;
    fragColor = inColor;
}
