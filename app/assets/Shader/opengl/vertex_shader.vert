#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;

    vec4 fogColor;
    vec4 fogCoeffs; 
    vec4 viewport;
    vec4 viewportDepth;
    vec4 blendColor;
    vec4 alphaRef;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in vec3 inBinormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBinormal;
layout(location = 6) out float fragFogFactor;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    vec4 clipPos = ubo.proj * viewPos;

    vec4 viewportPos = clipPos;
    viewportPos.x = viewportPos.x * ubo.viewport.z + ubo.viewport.x;
    viewportPos.y = viewportPos.y * ubo.viewport.w + ubo.viewport.y;

    float depth = (clipPos.z - ubo.viewportDepth.x) / (ubo.viewportDepth.y - ubo.viewportDepth.x);
    viewportPos.z = depth * 2.0 - 1.0;

    gl_Position = viewportPos;

    fragTexCoord = inTexCoord;

    fragColor = inColor * ubo.blendColor;

    mat3 normalMatrix = transpose(inverse(mat3(ubo.model)));
    fragNormal = normalMatrix * inNormal;
    fragTangent = normalMatrix * inTangent;
    fragBinormal = normalMatrix * inBinormal;

    float fogStart = ubo.fogCoeffs.x;
    float fogEnd = ubo.fogCoeffs.y;
    float fogDensity = ubo.fogCoeffs.z;

    float distance = length(viewPos.xyz);
    if (distance <= fogStart) {
        fragFogFactor = 0.0;
    } else if (distance >= fogEnd) {
        fragFogFactor = 1.0;
    } else {
        fragFogFactor = (distance - fogStart) / (fogEnd - fogStart);
    }

    fragWorldPos = worldPos.xyz;
}
