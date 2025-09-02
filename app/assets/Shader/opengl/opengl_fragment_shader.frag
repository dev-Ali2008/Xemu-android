#version 320 es

precision highp float;

in vec2 fragTexCoord;
in vec4 fragColor;
in float fragFog;

layout(location = 0) out vec4 outColor;

uniform sampler2D texSampler;
uniform bool useTexture;
uniform bool useFog;
uniform bool useAlphaBlend;

void main() {
    vec4 finalColor;

    if (useTexture) {

        vec2 texSize = textureSize(texSampler, 0);
        vec2 texCoord = fragTexCoord * texSize;
        vec2 texCoordFloor = floor(texCoord);
        vec2 texCoordFrac = texCoord - texCoordFloor;


        vec4 c00 = texture(texSampler, (texCoordFloor + vec2(0.0, 0.0)) / texSize);
        vec4 c10 = texture(texSampler, (texCoordFloor + vec2(1.0, 0.0)) / texSize);
        vec4 c01 = texture(texSampler, (texCoordFloor + vec2(0.0, 1.0)) / texSize);
        vec4 c11 = texture(texSampler, (texCoordFloor + vec2(1.0, 1.0)) / texSize);

        vec4 c0 = mix(c00, c10, texCoordFrac.x);
        vec4 c1 = mix(c01, c11, texCoordFrac.x);
        finalColor = mix(c0, c1, texCoordFrac.y);
    } else {
        finalColor = vec4(1.0);
    }


    finalColor *= fragColor;


    if (useAlphaBlend && finalColor.a < 1.0) {
        finalColor.rgb *= finalColor.a;
    }


    if (useFog && fragFog > 0.0) {
        vec4 fogColor = vec4(0.5, 0.5, 0.5, 1.0); 
        finalColor = mix(finalColor, fogColor, fragFog);
    }

    outColor = finalColor;
}
