#version 450

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D normalSampler;
layout(binding = 3) uniform sampler2D specularSampler;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBinormal;
layout(location = 6) in float fragFogFactor;

layout(location = 0) out vec4 outColor;

layout(binding = 4) uniform XboxUniforms {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 stencilRef;
    vec4 stencilMask;
    vec4 blendFactors; 
    vec4 colorMask; 
    vec4 logicOp;
    vec4 viewport;
    vec4 scissor;
} xbox;

void main() {

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 normalMap = texture(normalSampler, fragTexCoord).rgb * 2.0 - 1.0;
    vec4 specularMap = texture(specularSampler, fragTexCoord);

    vec4 finalColor = fragColor * texColor;

    float alpha = finalColor.a;
    float alphaRefValue = xbox.alphaRef.x;

    bool alphaTestPass = true;
    if (alpha < alphaRefValue) {
        alphaTestPass = false;
    }

    if (!alphaTestPass) {
        discard;
    }

    float stencilValue = 0.0; 
    float stencilRefValue = xbox.stencilRef.x;
    float stencilMaskValue = xbox.stencilMask.x;

    float maskedStencil = stencilValue * stencilMaskValue;
    float maskedRef = stencilRefValue * stencilMaskValue;

    bool stencilTestPass = true;
    if (maskedStencil < maskedRef) {
        stencilTestPass = false;
    }

    if (!stencilTestPass) {
        discard;
    }

    mat3 TBN = mat3(normalize(fragTangent), normalize(fragBinormal), normalize(fragNormal));
    vec3 normal = normalize(TBN * normalMap);

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = pow(max(dot(reflect(-lightDir, normal), normalize(fragWorldPos)), 0.0), 32.0);

    finalColor.rgb *= (diffuse * 0.7 + 0.3);
    finalColor.rgb += specular * specularMap.rgb * 0.5;

    if (fragFogFactor > 0.0) {
        finalColor.rgb = mix(finalColor.rgb, xbox.fogColor.rgb, fragFogFactor);
    }

    vec4 currentColor = vec4(0.0);
    if (xbox.colorMask.x < 0.5) finalColor.r = currentColor.r;
    if (xbox.colorMask.y < 0.5) finalColor.g = currentColor.g;
    if (xbox.colorMask.z < 0.5) finalColor.b = currentColor.b;
    if (xbox.colorMask.w < 0.5) finalColor.a = currentColor.a;

    float logicOp = xbox.logicOp.x;
    if (logicOp > 0.0) {

        switch (int(logicOp)) {
            case 1: 
                finalColor.rgb = finalColor.rgb * currentColor.rgb;
                break;
            case 2:
                finalColor.rgb = finalColor.rgb + currentColor.rgb - finalColor.rgb * currentColor.rgb;
                break;
            case 3: 
                finalColor.rgb = finalColor.rgb + currentColor.rgb - 2.0 * finalColor.rgb * currentColor.rgb;
                break;
            case 4:
                finalColor.rgb = 1.0 - finalColor.rgb;
                break;
        }
    }

    float srcBlend = xbox.blendFactors.x;
    float destBlend = xbox.blendFactors.y;

    if (srcBlend > 0.0 || destBlend > 0.0) {
        vec4 destColor = currentColor; 
        finalColor = finalColor * srcBlend + destColor * destBlend;
    }

    finalColor = clamp(finalColor, 0.0, 1.0);

    outColor = finalColor;
}
