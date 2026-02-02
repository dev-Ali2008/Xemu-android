package og.xaniteog

import android.content.Context
import android.graphics.*
import android.opengl.GLES30
import android.opengl.GLUtils
import android.opengl.Matrix
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.IntBuffer
import java.nio.ShortBuffer
import kotlin.math.*
import kotlin.system.measureTimeMillis

class XboxGPU(
    private val memory: XboxMemory,
    private val renderer: XboxRenderer,
    private val mmio: XboxMMIO? = null,
    private val context: Context? = null
) {
    companion object {
        private const val TAG = "XboxGPU"
        
        const val FRAME_WIDTH = 640
        const val FRAME_HEIGHT = 480
        const val FRAME_SIZE = FRAME_WIDTH * FRAME_HEIGHT * 4
        const val VRAM_SIZE = 64 * 1024 * 1024
        
        const val FRAMEBUFFER_ADDR = 0xFD000000.toInt()
        const val TEXTURE_MEMORY_ADDR = 0xF0000000.toInt()
        const val VERTEX_MEMORY_ADDR = 0xF1000000.toInt()
        const val COMMAND_MEMORY_ADDR = 0xF2000000.toInt()
        
        const val NV_PMC_BASE = 0xFD000000.toInt()
        const val NV_PFB_BASE = 0xFD001000.toInt()
        const val NV_PCRTC_BASE = 0xFD002000.toInt()
        const val NV_PRAMDAC_BASE = 0xFD003000.toInt()
        const val NV_PRMCIO_BASE = 0xFD004000.toInt()
        const val NV_PRMVIO_BASE = 0xFD005000.toInt()
        const val NV_PFB_BOOT_0 = NV_PFB_BASE + 0x0000
        const val NV_PFB_CFG0 = NV_PFB_BASE + 0x0200
        const val NV_PFB_CFG1 = NV_PFB_BASE + 0x0204
        
        const val NV_PCRTC_INTR_0 = NV_PCRTC_BASE + 0x0100
        const val NV_PCRTC_INTR_EN_0 = NV_PCRTC_BASE + 0x0104
        const val NV_PCRTC_START = NV_PCRTC_BASE + 0x0800
        const val NV_PCRTC_RASTER = NV_PCRTC_BASE + 0x0900
        
        const val NV_PGRAPH_BASE = 0xFD007000.toInt()
        const val NV_PGRAPH_INTR = NV_PGRAPH_BASE + 0x0100
        const val NV_PGRAPH_INTR_EN = NV_PGRAPH_BASE + 0x0104
        const val NV_PGRAPH_CTX_CONTROL = NV_PGRAPH_BASE + 0x0140
        const val NV_PGRAPH_CTX_USER = NV_PGRAPH_BASE + 0x0144
        const val NV_PGRAPH_CTX_SWITCH1 = NV_PGRAPH_BASE + 0x0148
        const val NV_PGRAPH_CTX_SWITCH2 = NV_PGRAPH_BASE + 0x014C
        const val NV_PGRAPH_CTX_SWITCH3 = NV_PGRAPH_BASE + 0x0150
        const val NV_PGRAPH_CTX_SWITCH4 = NV_PGRAPH_BASE + 0x0154
        const val NV_PGRAPH_CTX_SWITCH5 = NV_PGRAPH_BASE + 0x0158
        const val NV_PGRAPH_BUMP0 = NV_PGRAPH_BASE + 0x0800
        const val NV_PGRAPH_BUMP1 = NV_PGRAPH_BASE + 0x0804
        
        const val NV_PIXEL_FORMAT_ARGB8 = 0x00000000
        const val NV_PIXEL_FORMAT_RGB565 = 0x00000002
        const val NV_PIXEL_FORMAT_ARGB1555 = 0x00000003
        const val NV_PIXEL_FORMAT_Y8 = 0x00000004
        const val NV_PIXEL_FORMAT_A8 = 0x00000005
        const val NV_PIXEL_FORMAT_A1R5G5B5 = 0x00000006
        const val NV_PIXEL_FORMAT_X1R5G5B5 = 0x00000007
        
        const val NV_TEXTURE_FORMAT_L8 = 0x00000000
        const val NV_TEXTURE_FORMAT_A8L8 = 0x00000001
        const val NV_TEXTURE_FORMAT_A1R5G5B5 = 0x00000002
        const val NV_TEXTURE_FORMAT_A4R4G4B4 = 0x00000003
        const val NV_TEXTURE_FORMAT_R5G6B5 = 0x00000004
        const val NV_TEXTURE_FORMAT_A8R8G8B8 = 0x00000005
        const val NV_TEXTURE_FORMAT_X8R8G8B8 = 0x00000006
        const val NV_TEXTURE_FORMAT_DXT1 = 0x00000007
        const val NV_TEXTURE_FORMAT_DXT3 = 0x00000008
        const val NV_TEXTURE_FORMAT_DXT5 = 0x00000009
        
        private const val GL_VERTEX_SHADER_CODE = """
            #version 300 es
            precision highp float;
            
            layout(location = 0) in vec4 aPosition;
            layout(location = 1) in vec4 aColor;
            layout(location = 2) in vec2 aTexCoord;
            layout(location = 3) in vec3 aNormal;
            
            uniform mat4 uMVPMatrix;
            uniform mat4 uModelMatrix;
            uniform mat4 uViewMatrix;
            uniform mat4 uProjectionMatrix;
            uniform mat4 uTextureMatrix;
            
            out vec4 vColor;
            out vec2 vTexCoord;
            out vec3 vNormal;
            out vec3 vFragPos;
            out vec3 vViewPos;
            out float vFogFactor;
            
            uniform vec3 uViewPosition;
            uniform float uFogStart;
            uniform float uFogEnd;
            uniform float uFogDensity;
            
            void main() {
                vColor = aColor;
                vTexCoord = (uTextureMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
                vNormal = mat3(uModelMatrix) * aNormal;
                vFragPos = vec3(uModelMatrix * vec4(aPosition.xyz, 1.0));
                vViewPos = uViewPosition;
                
                float distance = length(vFragPos - uViewPosition);
                vFogFactor = clamp((uFogEnd - distance) / (uFogEnd - uFogStart), 0.0, 1.0);
                vFogFactor = 1.0 - exp(-uFogDensity * distance);
                
                gl_Position = uMVPMatrix * vec4(aPosition.xyz, 1.0);
            }
        """
        
        private const val GL_FRAGMENT_SHADER_CODE = """
            #version 300 es
            precision highp float;
            
            in vec4 vColor;
            in vec2 vTexCoord;
            in vec3 vNormal;
            in vec3 vFragPos;
            in vec3 vViewPos;
            in float vFogFactor;
            
            out vec4 fragColor;
            
            uniform sampler2D uTexture;
            uniform sampler2D uTexture1;
            uniform sampler2D uTexture2;
            uniform sampler2D uTexture3;
            
            uniform bool uUseTexture;
            uniform bool uUseLighting;
            uniform bool uUseFog;
            uniform vec4 uFogColor;
            
            uniform vec4 uMaterialAmbient;
            uniform vec4 uMaterialDiffuse;
            uniform vec4 uMaterialSpecular;
            uniform float uMaterialShininess;
            
            const int MAX_LIGHTS = 8;
            uniform int uLightCount;
            uniform vec3 uLightPosition[MAX_LIGHTS];
            uniform vec3 uLightDirection[MAX_LIGHTS];
            uniform vec4 uLightAmbient[MAX_LIGHTS];
            uniform vec4 uLightDiffuse[MAX_LIGHTS];
            uniform vec4 uLightSpecular[MAX_LIGHTS];
            uniform float uLightConstant[MAX_LIGHTS];
            uniform float uLightLinear[MAX_LIGHTS];
            uniform float uLightQuadratic[MAX_LIGHTS];
            uniform float uLightCutoff[MAX_LIGHTS];
            uniform float uLightExponent[MAX_LIGHTS];
            uniform int uLightType[MAX_LIGHTS];
            
            uniform bool uAlphaTest;
            uniform int uAlphaFunc;
            uniform float uAlphaRef;
            
            uniform bool uBlendEnabled;
            uniform int uBlendSrc;
            uniform int uBlendDst;
            
            uniform bool uDepthTest;
            uniform bool uDepthWrite;
            uniform int uDepthFunc;
            
            vec4 calculateLighting(vec3 normal, vec3 fragPos, vec3 viewDir) {
                vec4 result = vec4(0.0, 0.0, 0.0, 1.0);
                
                for(int i = 0; i < uLightCount; i++) {
                    if(i >= MAX_LIGHTS) break;
                    
                    vec3 lightDir;
                    float attenuation = 1.0;
                    
                    if(uLightType[i] == 1) {
                        lightDir = normalize(-uLightDirection[i]);
                    } else {
                        vec3 lightVec = uLightPosition[i] - fragPos;
                        float distance = length(lightVec);
                        lightDir = normalize(lightVec);
                        
                        attenuation = 1.0 / (uLightConstant[i] + 
                                           uLightLinear[i] * distance + 
                                           uLightQuadratic[i] * distance * distance);
                        
                        if(uLightType[i] == 2) {
                            float theta = dot(lightDir, normalize(-uLightDirection[i]));
                            if(theta > uLightCutoff[i]) {
                                attenuation *= pow(theta, uLightExponent[i]);
                            } else {
                                attenuation = 0.0;
                            }
                        }
                    }
                    
                    vec4 ambient = uLightAmbient[i] * uMaterialAmbient;
                    
                    float diff = max(dot(normal, lightDir), 0.0);
                    vec4 diffuse = uLightDiffuse[i] * (diff * uMaterialDiffuse);
                    
                    vec3 reflectDir = reflect(-lightDir, normal);
                    float spec = pow(max(dot(viewDir, reflectDir), 0.0), uMaterialShininess);
                    vec4 specular = uLightSpecular[i] * (spec * uMaterialSpecular);
                    
                    result += (ambient + diffuse + specular) * attenuation;
                }
                
                return result;
            }
            
            void main() {
                vec4 texColor = vColor;
                
                if(uUseTexture) {
                    texColor = texture(uTexture, vTexCoord);
                }
                
                if(uAlphaTest) {
                    float alpha = texColor.a;
                    bool discardFrag = false;
                    
                    switch(uAlphaFunc) {
                        case 0:
                            discardFrag = true;
                            break;
                        case 1:
                            discardFrag = alpha < uAlphaRef;
                            break;
                        case 2:
                            discardFrag = abs(alpha - uAlphaRef) < 0.001;
                            break;
                        case 3:
                            discardFrag = alpha <= uAlphaRef;
                            break;
                        case 4:
                            discardFrag = alpha > uAlphaRef;
                            break;
                        case 5:
                            discardFrag = abs(alpha - uAlphaRef) >= 0.001;
                            break;
                        case 6:
                            discardFrag = alpha >= uAlphaRef;
                            break;
                        case 7:
                            discardFrag = false;
                            break;
                    }
                    
                    if(discardFrag) discard;
                }
                
                if(uUseLighting) {
                    vec3 norm = normalize(vNormal);
                    vec3 viewDir = normalize(vViewPos - vFragPos);
                    vec4 lighting = calculateLighting(norm, vFragPos, viewDir);
                    texColor *= lighting;
                }
                
                if(uUseFog) {
                    texColor = mix(uFogColor, texColor, vFogFactor);
                }
                
                fragColor = texColor;
            }
        """
        
        private const val GL_MAX_VERTEX_ATTRIBS = 16
        private const val GL_MAX_TEXTURE_UNITS = 32
        private const val GL_MAX_TEXTURE_SIZE = 4096
        
        private const val STATS_WINDOW_SIZE = 60
    }

    private var glProgram: Int = 0
    private var glFrameTexture: Int = 0
    private var glRenderTexture: Int = 0
    private var glDepthTexture: Int = 0
    private var glFramebuffer: Int = 0
    private var glDepthbuffer: Int = 0
    
    private var glVAOs = IntArray(2)
    private var glVBOs = IntArray(4)
    private var glEBOs = IntArray(2)
    
    private var glUniformLocations = mutableMapOf<String, Int>()
    private var glTextureUnits = IntArray(8) { -1 }
    
    private var glInitialized = false
    private var glContextValid = false
    private var glSupportsNPOT = false
    private var glMaxAnisotropy = 1.0f
    
    private var glFrameData: ByteBuffer? = null
    private var glVertexData: FloatBuffer? = null
    private var glIndexData: ShortBuffer? = null
    
    private var glMVPMatrix = FloatArray(16)
    private var glModelMatrix = FloatArray(16)
    private var glViewMatrix = FloatArray(16)
    private var glProjectionMatrix = FloatArray(16)
    private var glTextureMatrix = FloatArray(16)
    
    private var frameTimes = FloatArray(STATS_WINDOW_SIZE)
    private var drawCallTimes = LongArray(STATS_WINDOW_SIZE)
    private var triangleTimes = LongArray(STATS_WINDOW_SIZE)
    private var statsIndex = 0
    private var gpuLoad = 0.0f
    private var vramUsage = 0L
    private var commandQueueLength = 0
    private var shaderCompileTime = 0L
    
    private var glCurrentFBO = 0
    private var glCurrentProgram = 0
    private var glCurrentVAO = 0
    private var glActiveTexture = 0
    private var glViewport = IntArray(4)
    private var glScissor = IntArray(4)
    private var glClearColor = FloatArray(4)
    private var glClearDepth = 1.0f
    private var glClearStencil = 0
    
    private val shaderCache = mutableMapOf<String, Int>()
    private val programCache = mutableMapOf<String, Int>()
    
    data class PipelineState(
        var program: Int = 0,
        var blendEnable: Boolean = false,
        var blendSrc: Int = GLES30.GL_ONE,
        var blendDst: Int = GLES30.GL_ZERO,
        var depthTest: Boolean = true,
        var depthFunc: Int = GLES30.GL_LESS,
        var depthWrite: Boolean = true,
        var cullFace: Boolean = true,
        var cullMode: Int = GLES30.GL_BACK,
        var frontFace: Int = GLES30.GL_CCW,
        var polygonMode: Int = GLES30.GL_FILL,
        var lineWidth: Float = 1.0f,
        var scissorTest: Boolean = false,
        var stencilTest: Boolean = false
    )
    
    private var currentPipeline = PipelineState()
    private val pipelineCache = mutableMapOf<Int, PipelineState>()
    
    data class GLTexture(
        val id: Int,
        var width: Int,
        var height: Int,
        var format: Int,
        var internalFormat: Int,
        var type: Int,
        var mipmaps: Int,
        var anisotropic: Float,
        var wrapS: Int,
        var wrapT: Int,
        var minFilter: Int,
        var magFilter: Int,
        var data: ByteBuffer? = null,
        var boundUnit: Int = -1
    )
    
    private val glTextures = mutableMapOf<Int, GLTexture>()
    private var nextGLTextureId = 1
    private var textureMemoryPool = ByteBuffer.allocateDirect(32 * 1024 * 1024)
        .order(ByteOrder.nativeOrder())
    
    data class GLBuffer(
        val id: Int,
        val target: Int,
        var size: Int,
        var usage: Int,
        var data: ByteBuffer? = null
    )
    
    private val glBuffers = mutableMapOf<Int, GLBuffer>()
    private var nextGLBufferId = 1
    
    data class GLVertexArray(
        val id: Int,
        var buffers: Map<Int, GLBuffer>,
        var attributes: Map<Int, VertexAttribute>
    )
    
    data class VertexAttribute(
        var index: Int,
        var size: Int,
        var type: Int,
        var normalized: Boolean,
        var stride: Int,
        var offset: Int,
        var buffer: Int
    )
    
    private val glVertexArrays = mutableMapOf<Int, GLVertexArray>()
    private var nextGLVertexArrayId = 1
    
    private var glTimeQuery: Int = 0
    private var glPrimitiveQuery: Int = 0
    
    private val frameBuffers = Array(3) {
        ByteBuffer.allocateDirect(FRAME_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    }
    private var currentBufferIndex = 0
    private var displayBufferIndex = 1
    private var renderBufferIndex = 2
    
    private val commandQueue = ArrayDeque<GPUCommand>()
    private val immediateCommandQueue = ArrayDeque<GPUCommand>()
    private var commandProcessorRunning = false
    private var commandThread: Thread? = null
    private val commandLock = Any()
    
    private val dmaChannels = Array(8) { DMAChannel() }
    private var dmaInterrupt = 0
    private var dmaControl = 0
    
    data class DMAChannel(
        var source: Int = 0,
        var dest: Int = 0,
        var size: Int = 0,
        var control: Int = 0,
        var status: Int = 0,
        var current: Int = 0,
        var active: Boolean = false
    )
    
    private var tessellationEnabled = false
    private var geometryShaderEnabled = false
    private var computeShaderEnabled = false
    private var transformFeedbackEnabled = false
    private var instancedRendering = false
    private var multiDrawIndirect = false
    
    private var msaaSamples = 1
    private var msaaFramebuffer: Int = 0
    private var msaaColorBuffer: Int = 0
    private var msaaDepthBuffer: Int = 0
    
    private var shadowMapSize = 1024
    private var shadowMapFBO: Int = 0
    private var shadowMapTexture: Int = 0
    private var shadowBias = 0.005f
    
    private var postProcessingEnabled = false
    private var bloomEnabled = false
    private var ssaoEnabled = false
    private var fxaaEnabled = false
    private var motionBlurEnabled = false
    private var dofEnabled = false
    
    private var skyboxTexture: Int = 0
    private var reflectionProbeTexture: Int = 0
    private var irradianceMap: Int = 0
    private var prefilterMap: Int = 0
    private var brdfLUT: Int = 0
    
    private var displayEnabled = true
    private var vsyncEnabled = true
    private var frameCounter = 0L
    private var drawCalls = 0L
    private var trianglesProcessed = 0L
    private var textureUploads = 0L
    private var vertexCount = 0L
    private var clearColor = 0xFF000000.toInt()
    private var pixelFormat = NV_PIXEL_FORMAT_ARGB8
    private var backBufferAddr = FRAMEBUFFER_ADDR
    private var frontBufferAddr = FRAMEBUFFER_ADDR + FRAME_SIZE
    
    private val pgraphState = PGraphState()
    private var pgraphIntr = 0
    private var pgraphIntrEnabled = 0
    
    private val textureMemory = ByteBuffer.allocateDirect(32 * 1024 * 1024).order(ByteOrder.LITTLE_ENDIAN)
    private val textures = mutableMapOf<Int, TextureObject>()
    private var nextTextureId = 1
    
    private val vertexBuffer = mutableListOf<Vertex>()
    private val transformBuffer = mutableListOf<Vertex>()
    private val clipBuffer = mutableListOf<Vertex>()
    
    private var viewport = Rect(0, 0, FRAME_WIDTH, FRAME_HEIGHT)
    private var scissor = Rect(0, 0, FRAME_WIDTH, FRAME_HEIGHT)
    private var scissorEnabled = false
    
    private var cullMode = CullMode.NONE
    private var blendEnabled = false
    private var depthTestEnabled = false
    private var depthWriteEnabled = false
    private var alphaTestEnabled = false
    private var alphaRef = 0
    private var alphaFunc = CompareFunc.ALWAYS
    
    private var modelViewMatrix = Matrix4f()
    private var projectionMatrix = Matrix4f()
    private var textureMatrix = Matrix4f()
    
    private var lastFrameTime = System.nanoTime()
    private var fps = 0.0
    private var frameTime = 0.0
    
    private var fogEnabled = false
    private var fogStart = 0.0f
    private var fogEnd = 1.0f
    private var fogColor = 0xFFFFFFFF.toInt()
    private var fogDensity = 1.0f
    
    private var lightingEnabled = false
    private val lights = Array(8) { Light() }
    private var currentLight = 0
    
    private var materialAmbient = floatArrayOf(0.2f, 0.2f, 0.2f, 1.0f)
    private var materialDiffuse = floatArrayOf(0.8f, 0.8f, 0.8f, 1.0f)
    private var materialSpecular = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f)
    private var materialEmission = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f)
    private var materialShininess = 0.0f
    
    data class Vertex(
        var x: Float = 0.0f,
        var y: Float = 0.0f,
        var z: Float = 0.0f,
        var w: Float = 1.0f,
        var color: Int = 0xFFFFFFFF.toInt(),
        var u: Float = 0.0f,
        var v: Float = 0.0f,
        var normal: FloatArray = floatArrayOf(0.0f, 0.0f, 1.0f),
        var tangent: FloatArray = floatArrayOf(1.0f, 0.0f, 0.0f),
        var bitangent: FloatArray = floatArrayOf(0.0f, 1.0f, 0.0f),
        var fog: Float = 1.0f,
        var pointSize: Float = 1.0f
    )
    
    data class TextureObject(
        val id: Int,
        val width: Int,
        val height: Int,
        val format: Int,
        val mipmaps: Int,
        val addr: Int,
        val pitch: Int,
        var data: ByteBuffer? = null,
        var glTextureId: Int = -1
    )
    
    data class Light(
        var enabled: Boolean = false,
        var position: FloatArray = floatArrayOf(0.0f, 0.0f, 1.0f, 0.0f),
        var ambient: FloatArray = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f),
        var diffuse: FloatArray = floatArrayOf(1.0f, 1.0f, 1.0f, 1.0f),
        var specular: FloatArray = floatArrayOf(1.0f, 1.0f, 1.0f, 1.0f),
        var spotDirection: FloatArray = floatArrayOf(0.0f, 0.0f, -1.0f),
        var spotExponent: Float = 0.0f,
        var spotCutoff: Float = 180.0f,
        var constantAttenuation: Float = 1.0f,
        var linearAttenuation: Float = 0.0f,
        var quadraticAttenuation: Float = 0.0f
    )
    
    data class Matrix4f(
        var m00: Float = 1.0f, var m01: Float = 0.0f, var m02: Float = 0.0f, var m03: Float = 0.0f,
        var m10: Float = 0.0f, var m11: Float = 1.0f, var m12: Float = 0.0f, var m13: Float = 0.0f,
        var m20: Float = 0.0f, var m21: Float = 0.0f, var m22: Float = 1.0f, var m23: Float = 0.0f,
        var m30: Float = 0.0f, var m31: Float = 0.0f, var m32: Float = 0.0f, var m33: Float = 1.0f
    ) {
        fun multiply(v: FloatArray, result: FloatArray = FloatArray(4)): FloatArray {
            result[0] = m00 * v[0] + m01 * v[1] + m02 * v[2] + m03 * v[3]
            result[1] = m10 * v[0] + m11 * v[1] + m12 * v[2] + m13 * v[3]
            result[2] = m20 * v[0] + m21 * v[1] + m22 * v[2] + m23 * v[3]
            result[3] = m30 * v[0] + m31 * v[1] + m32 * v[2] + m33 * v[3]
            return result
        }
        
        fun multiply(other: Matrix4f): Matrix4f {
            return Matrix4f(
                m00 * other.m00 + m01 * other.m10 + m02 * other.m20 + m03 * other.m30,
                m00 * other.m01 + m01 * other.m11 + m02 * other.m21 + m03 * other.m31,
                m00 * other.m02 + m01 * other.m12 + m02 * other.m22 + m03 * other.m32,
                m00 * other.m03 + m01 * other.m13 + m02 * other.m23 + m03 * other.m33,
                m10 * other.m00 + m11 * other.m10 + m12 * other.m20 + m13 * other.m30,
                m10 * other.m01 + m11 * other.m11 + m12 * other.m21 + m13 * other.m31,
                m10 * other.m02 + m11 * other.m12 + m12 * other.m22 + m13 * other.m32,
                m10 * other.m03 + m11 * other.m13 + m12 * other.m23 + m13 * other.m33,
                m20 * other.m00 + m21 * other.m10 + m22 * other.m20 + m23 * other.m30,
                m20 * other.m01 + m21 * other.m11 + m22 * other.m21 + m23 * other.m31,
                m20 * other.m02 + m21 * other.m12 + m22 * other.m22 + m23 * other.m32,
                m20 * other.m03 + m21 * other.m13 + m22 * other.m23 + m23 * other.m33,
                m30 * other.m00 + m31 * other.m10 + m32 * other.m20 + m33 * other.m30,
                m30 * other.m01 + m31 * other.m11 + m32 * other.m21 + m33 * other.m31,
                m30 * other.m02 + m31 * other.m12 + m32 * other.m22 + m33 * other.m32,
                m30 * other.m03 + m31 * other.m13 + m32 * other.m23 + m33 * other.m33
            )
        }
        
        fun toFloatArray(): FloatArray {
            return floatArrayOf(
                m00, m01, m02, m03,
                m10, m11, m12, m13,
                m20, m21, m22, m23,
                m30, m31, m32, m33
            )
        }
    }
    
    enum class CullMode {
        NONE, FRONT, BACK, FRONT_AND_BACK
    }
    
    enum class CompareFunc {
        NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS
    }
    
    enum class BlendFunc {
        ZERO, ONE, SRC_COLOR, ONE_MINUS_SRC_COLOR, DST_COLOR,
        ONE_MINUS_DST_COLOR, SRC_ALPHA, ONE_MINUS_SRC_ALPHA,
        DST_ALPHA, ONE_MINUS_DST_ALPHA, SRC_ALPHA_SATURATE
    }
    
    enum class PrimitiveType {
        POINTS, LINES, LINE_STRIP, TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, QUADS,
        LINE_LOOP, POLYGON, QUAD_STRIP
    }
    
    data class GPUCommand(
        val type: Int,
        val params: LongArray = LongArray(0),
        val timestamp: Long = System.nanoTime(),
        val priority: Int = 0
    )
    
    data class PGraphState(
        var ctxControl: Int = 0,
        var ctxUser: Int = 0,
        var ctxSwitch: IntArray = IntArray(5),
        var buffer: Int = 0,
        var method: Int = 0,
        var data: IntArray = IntArray(0x1000),
        var dataPos: Int = 0
    )
    
    private var currentPrimitiveType: PrimitiveType = PrimitiveType.TRIANGLES
    private val currentVertices = mutableListOf<Vertex>()
    private var primitiveStarted = false
    
    private fun initializeOpenGL(): Boolean {
        return try {
            Log.d(TAG, "Initializing OpenGL ES 3.0 with advanced features...")
            
            val extensions = GLES30.glGetString(GLES30.GL_EXTENSIONS)
            glSupportsNPOT = extensions?.contains("GL_OES_texture_npot") ?: false
            
            val maxTextures = IntArray(1)
            GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_IMAGE_UNITS, maxTextures, 0)
            Log.d(TAG, "Max texture units: ${maxTextures[0]}")
            
            val maxVertexAttribs = IntArray(1)
            GLES30.glGetIntegerv(GLES30.GL_MAX_VERTEX_ATTRIBS, maxVertexAttribs, 0)
            Log.d(TAG, "Max vertex attributes: ${maxVertexAttribs[0]}")
            
            Matrix.setIdentityM(glModelMatrix, 0)
            Matrix.setIdentityM(glViewMatrix, 0)
            Matrix.setIdentityM(glProjectionMatrix, 0)
            Matrix.setIdentityM(glTextureMatrix, 0)
            Matrix.setIdentityM(glMVPMatrix, 0)
            
            shaderCompileTime = measureTimeMillis {
                glProgram = createShaderProgram(GL_VERTEX_SHADER_CODE, GL_FRAGMENT_SHADER_CODE)
                if (glProgram == 0) {
                    throw RuntimeException("Failed to compile shader program")
                }
                
                glUniformLocations["uMVPMatrix"] = GLES30.glGetUniformLocation(glProgram, "uMVPMatrix")
                glUniformLocations["uModelMatrix"] = GLES30.glGetUniformLocation(glProgram, "uModelMatrix")
                glUniformLocations["uViewMatrix"] = GLES30.glGetUniformLocation(glProgram, "uViewMatrix")
                glUniformLocations["uProjectionMatrix"] = GLES30.glGetUniformLocation(glProgram, "uProjectionMatrix")
                glUniformLocations["uTextureMatrix"] = GLES30.glGetUniformLocation(glProgram, "uTextureMatrix")
                
                listOf("uTexture", "uUseTexture", "uUseLighting", "uUseFog", "uFogColor",
                    "uMaterialAmbient", "uMaterialDiffuse", "uMaterialSpecular", "uMaterialShininess",
                    "uViewPosition", "uFogStart", "uFogEnd", "uFogDensity", "uLightCount").forEach {
                    glUniformLocations[it] = GLES30.glGetUniformLocation(glProgram, it)
                }
            }
            
            Log.d(TAG, "Shader compilation time: ${shaderCompileTime}ms")
            
            val textureIds = IntArray(3)
            GLES30.glGenTextures(3, textureIds, 0)
            glFrameTexture = textureIds[0]
            glRenderTexture = textureIds[1]
            glDepthTexture = textureIds[2]
            
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glFrameTexture)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
            
            glFrameData = ByteBuffer.allocateDirect(FRAME_SIZE).order(ByteOrder.nativeOrder())
            
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8,
                FRAME_WIDTH, FRAME_HEIGHT, 0,
                GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE,
                glFrameData
            )
            
            val fboIds = IntArray(2)
            GLES30.glGenFramebuffers(2, fboIds, 0)
            glFramebuffer = fboIds[0]
            msaaFramebuffer = fboIds[1]
            
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, glFramebuffer)
            
            GLES30.glFramebufferTexture2D(
                GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
                GLES30.GL_TEXTURE_2D, glRenderTexture, 0
            )
            
            GLES30.glFramebufferTexture2D(
                GLES30.GL_FRAMEBUFFER, GLES30.GL_DEPTH_ATTACHMENT,
                GLES30.GL_TEXTURE_2D, glDepthTexture, 0
            )
            
            val status = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER)
            if (status != GLES30.GL_FRAMEBUFFER_COMPLETE) {
                Log.e(TAG, "Framebuffer not complete: $status")
                return false
            }
            
            GLES30.glGenVertexArrays(2, glVAOs, 0)
            GLES30.glGenBuffers(4, glVBOs, 0)
            GLES30.glGenBuffers(2, glEBOs, 0)
            
            GLES30.glBindVertexArray(glVAOs[0])
            
            val quadVertices = floatArrayOf(
                -1.0f,  1.0f,  0.0f, 1.0f,
                -1.0f, -1.0f,  0.0f, 0.0f,
                 1.0f, -1.0f,  1.0f, 0.0f,
                
                -1.0f,  1.0f,  0.0f, 1.0f,
                 1.0f, -1.0f,  1.0f, 0.0f,
                 1.0f,  1.0f,  1.0f, 1.0f
            )
            
            val quadBuffer = ByteBuffer.allocateDirect(quadVertices.size * 4)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer()
                .put(quadVertices)
            quadBuffer.position(0)
            
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, glVBOs[0])
            GLES30.glBufferData(
                GLES30.GL_ARRAY_BUFFER,
                quadVertices.size * 4,
                quadBuffer,
                GLES30.GL_STATIC_DRAW
            )
            
            GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 4 * 4, 0)
            GLES30.glEnableVertexAttribArray(0)
            
            GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 4 * 4, 2 * 4)
            GLES30.glEnableVertexAttribArray(1)
            
            val queryIds = IntArray(2)
            GLES30.glGenQueries(2, queryIds, 0)
            glTimeQuery = queryIds[0]
            glPrimitiveQuery = queryIds[1]
            
            currentPipeline.program = glProgram
            currentPipeline.blendEnable = false
            currentPipeline.depthTest = true
            currentPipeline.depthFunc = GLES30.GL_LESS
            currentPipeline.cullFace = true
            currentPipeline.cullMode = GLES30.GL_BACK
            
            applyPipelineState(currentPipeline)
            
            frameTimes.fill(16.67f)
            drawCallTimes.fill(0)
            triangleTimes.fill(0)
            
            glInitialized = true
            glContextValid = true
            
            Log.d(TAG, "OpenGL ES 3.0 initialization complete with advanced features")
            Log.d(TAG, "GPU Features: NPOT=${glSupportsNPOT}, MaxTexUnits=${maxTextures[0]}")
            
            true
        } catch (e: Exception) {
            Log.e(TAG, "OpenGL ES 3.0 initialization failed: ${e.message}", e)
            glInitialized = false
            false
        }
    }
    
    private fun createShaderProgram(vertexCode: String, fragmentCode: String): Int {
        val vertexShader = compileShader(GLES30.GL_VERTEX_SHADER, vertexCode)
        if (vertexShader == 0) return 0
        
        val fragmentShader = compileShader(GLES30.GL_FRAGMENT_SHADER, fragmentCode)
        if (fragmentShader == 0) {
            GLES30.glDeleteShader(vertexShader)
            return 0
        }
        
        val program = GLES30.glCreateProgram()
        GLES30.glAttachShader(program, vertexShader)
        GLES30.glAttachShader(program, fragmentShader)
        GLES30.glLinkProgram(program)
        
        val linkStatus = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, linkStatus, 0)
        
        if (linkStatus[0] == 0) {
            val infoLog = GLES30.glGetProgramInfoLog(program)
            Log.e(TAG, "Program link failed:\n$infoLog")
            GLES30.glDeleteProgram(program)
            GLES30.glDeleteShader(vertexShader)
            GLES30.glDeleteShader(fragmentShader)
            return 0
        }
        
        GLES30.glDeleteShader(vertexShader)
        GLES30.glDeleteShader(fragmentShader)
        
        return program
    }
    
    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES30.glCreateShader(type)
        GLES30.glShaderSource(shader, source)
        GLES30.glCompileShader(shader)
        
        val compileStatus = IntArray(1)
        GLES30.glGetShaderiv(shader, type, compileStatus, 0)
        
        if (compileStatus[0] == 0) {
            val infoLog = GLES30.glGetShaderInfoLog(shader)
            Log.e(TAG, "Shader compilation failed:\n$infoLog")
            GLES30.glDeleteShader(shader)
            return 0
        }
        
        return shader
    }
    
    private fun applyPipelineState(state: PipelineState) {
        if (state.blendEnable) {
            GLES30.glEnable(GLES30.GL_BLEND)
            GLES30.glBlendFunc(state.blendSrc, state.blendDst)
        } else {
            GLES30.glDisable(GLES30.GL_BLEND)
        }
        
        if (state.depthTest) {
            GLES30.glEnable(GLES30.GL_DEPTH_TEST)
            GLES30.glDepthFunc(state.depthFunc)
        } else {
            GLES30.glDisable(GLES30.GL_DEPTH_TEST)
        }
        
        GLES30.glDepthMask(state.depthWrite)
        
        if (state.cullFace) {
            GLES30.glEnable(GLES30.GL_CULL_FACE)
            GLES30.glCullFace(state.cullMode)
            GLES30.glFrontFace(state.frontFace)
        } else {
            GLES30.glDisable(GLES30.GL_CULL_FACE)
        }
        
        if (state.scissorTest) {
            GLES30.glEnable(GLES30.GL_SCISSOR_TEST)
        } else {
            GLES30.glDisable(GLES30.GL_SCISSOR_TEST)
        }
        
        GLES30.glLineWidth(state.lineWidth)
        
        if (state.program != glCurrentProgram) {
            GLES30.glUseProgram(state.program)
            glCurrentProgram = state.program
        }
    }
    
    fun present() {
        if (!glInitialized) {
            if (!initializeOpenGL()) {
                fallbackPresent()
                return
            }
        }
        
        val startTime = System.nanoTime()
        
        try {
            GLES30.glBeginQuery(GLES30.GL_TIME_ELAPSED, glTimeQuery)
            
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, glFramebuffer)
            GLES30.glViewport(0, 0, FRAME_WIDTH, FRAME_HEIGHT)
            
            GLES30.glClearColor(
                ((clearColor shr 16) and 0xFF) / 255.0f,
                ((clearColor shr 8) and 0xFF) / 255.0f,
                (clearColor and 0xFF) / 255.0f,
                ((clearColor shr 24) and 0xFF) / 255.0f
            )
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
            
            updateFrameTexture()
            
            GLES30.glUseProgram(glProgram)
            GLES30.glBindVertexArray(glVAOs[0])
            
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glFrameTexture)
            
            GLES30.glUniform1i(glUniformLocations["uTexture"]!!, 0)
            GLES30.glUniform1i(glUniformLocations["uUseTexture"]!!, 1)
            
            val identityMatrix = FloatArray(16)
            Matrix.setIdentityM(identityMatrix, 0)
            
            GLES30.glUniformMatrix4fv(glUniformLocations["uMVPMatrix"], 1, false, identityMatrix, 0)
            GLES30.glUniformMatrix4fv(glUniformLocations["uModelMatrix"], 1, false, identityMatrix, 0)
            GLES30.glUniformMatrix4fv(glUniformLocations["uViewMatrix"], 1, false, identityMatrix, 0)
            GLES30.glUniformMatrix4fv(glUniformLocations["uProjectionMatrix"], 1, false, identityMatrix, 0)
            GLES30.glUniformMatrix4fv(glUniformLocations["uTextureMatrix"], 1, false, identityMatrix, 0)
            
            GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 6)
            
            GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, 0)
            GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, glFramebuffer)
            
            GLES30.glBlitFramebuffer(
                0, 0, FRAME_WIDTH, FRAME_HEIGHT,
                0, 0, FRAME_WIDTH, FRAME_HEIGHT,
                GLES30.GL_COLOR_BUFFER_BIT, GLES30.GL_LINEAR
            )
            
            GLES30.glEndQuery(GLES30.GL_TIME_ELAPSED)
            
            val gpuTimeAvailable = IntArray(1)
            GLES30.glGetQueryObjectuiv(glTimeQuery, GLES30.GL_QUERY_RESULT_AVAILABLE, gpuTimeAvailable, 0)
            
            if (gpuTimeAvailable[0] != 0) {
                val gpuTime = LongArray(1)
                GLES30.glGetQueryObjectuiv(glTimeQuery, GLES30.GL_QUERY_RESULT, gpuTime, 0)
                val gpuTimeMs = gpuTime[0] / 1_000_000.0
                
                gpuLoad = (gpuTimeMs / 16.67).toFloat()
                frameTimes[statsIndex] = gpuTimeMs.toFloat()
                statsIndex = (statsIndex + 1) % STATS_WINDOW_SIZE
            }
            
            renderer.onFrameRendered(glRenderTexture, FRAME_WIDTH, FRAME_HEIGHT)
            
            swapBuffers()
            frameCounter++
            
            val endTime = System.nanoTime()
            val frameTime = (endTime - startTime) / 1_000_000.0
            
            if (frameCounter % 60 == 0L) {
                logPerformanceStats()
            }
            
        } catch (e: Exception) {
            Log.e(TAG, "Error in OpenGL present: ${e.message}", e)
            fallbackPresent()
        }
    }
    
    private fun updateFrameTexture() {
        val currentFrameBuffer = frameBuffers[displayBufferIndex]
        
        glFrameData?.clear()
        currentFrameBuffer.position(0)
        
        while (currentFrameBuffer.hasRemaining() && glFrameData?.hasRemaining() == true) {
            val argb = currentFrameBuffer.int
            
            glFrameData?.put((argb and 0xFF).toByte())
            glFrameData?.put(((argb shr 8) and 0xFF).toByte())
            glFrameData?.put(((argb shr 16) and 0xFF).toByte())
            glFrameData?.put(((argb shr 24) and 0xFF).toByte())
        }
        
        glFrameData?.position(0)
        
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glFrameTexture)
        GLES30.glTexSubImage2D(
            GLES30.GL_TEXTURE_2D, 0,
            0, 0, FRAME_WIDTH, FRAME_HEIGHT,
            GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE,
            glFrameData
        )
    }
    
    private fun fallbackPresent() {
        val bitmap = Bitmap.createBitmap(FRAME_WIDTH, FRAME_HEIGHT, Bitmap.Config.ARGB_8888)
        frameBuffers[displayBufferIndex].position(0)
        bitmap.copyPixelsFromBuffer(frameBuffers[displayBufferIndex])
        renderer.draw(bitmap)
        frameCounter++
    }
    
    private fun swapBuffers() {
        displayBufferIndex = renderBufferIndex
        renderBufferIndex = (renderBufferIndex + 1) % 3
    }
    
    private fun logPerformanceStats() {
        val avgFrameTime = frameTimes.average()
        val fps = if (avgFrameTime > 0) 1000.0 / avgFrameTime else 0.0
        
        val stats = """
            |=== GPU Performance Stats ===
            |FPS: ${"%.1f".format(fps)}
            |Frame Time: ${"%.2f".format(avgFrameTime)} ms
            |GPU Load: ${"%.1f".format(gpuLoad * 100)}%
            |Draw Calls: $drawCalls
            |Triangles: $trianglesProcessed
            |Textures: ${textures.size}
            |VRAM Usage: ${vramUsage / 1024 / 1024} MB
            |Command Queue: $commandQueueLength
            |Shader Compile: ${shaderCompileTime}ms
            |=============================
        """.trimMargin()
        
        Log.d(TAG, stats)
    }
    
    fun createGLTexture(width: Int, height: Int, format: Int, data: ByteBuffer? = null): Int {
        val textureId = nextGLTextureId++
        
        val glTexture = GLTexture(
            id = textureId,
            width = width,
            height = height,
            format = when(format) {
                NV_TEXTURE_FORMAT_A8R8G8B8 -> GLES30.GL_RGBA
                NV_TEXTURE_FORMAT_R5G6B5 -> GLES30.GL_RGB
                NV_TEXTURE_FORMAT_L8 -> GLES30.GL_LUMINANCE
                else -> GLES30.GL_RGBA
            },
            internalFormat = when(format) {
                NV_TEXTURE_FORMAT_A8R8G8B8 -> GLES30.GL_RGBA8
                NV_TEXTURE_FORMAT_R5G6B5 -> GLES30.GL_RGB565
                NV_TEXTURE_FORMAT_L8 -> GLES30.GL_LUMINANCE8
                else -> GLES30.GL_RGBA8
            },
            type = when(format) {
                NV_TEXTURE_FORMAT_R5G6B5 -> GLES30.GL_UNSIGNED_SHORT_5_6_5
                else -> GLES30.GL_UNSIGNED_BYTE
            },
            mipmaps = 1,
            anisotropic = 1.0f,
            wrapS = GLES30.GL_REPEAT,
            wrapT = GLES30.GL_REPEAT,
            minFilter = GLES30.GL_LINEAR,
            magFilter = GLES30.GL_LINEAR,
            data = data,
            boundUnit = -1
        )
        
        val glIds = IntArray(1)
        GLES30.glGenTextures(1, glIds, 0)
        glTexture.id = glIds[0]
        
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glTexture.id)
        
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, glTexture.wrapS)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, glTexture.wrapT)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, glTexture.minFilter)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, glTexture.magFilter)
        
        if (data != null) {
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D, 0, glTexture.internalFormat,
                width, height, 0,
                glTexture.format, glTexture.type,
                data
            )
            
            if (glTexture.mipmaps > 1) {
                GLES30.glGenerateMipmap(GLES30.GL_TEXTURE_2D)
            }
        } else {
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D, 0, glTexture.internalFormat,
                width, height, 0,
                glTexture.format, glTexture.type,
                null
            )
        }
        
        glTextures[textureId] = glTexture
        textureUploads++
        
        return textureId
    }
    
    fun bindTexture(textureId: Int, unit: Int) {
        val texture = glTextures[textureId] ?: return
        
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture.id)
        
        texture.boundUnit = unit
        glTextureUnits[unit] = textureId
    }
    
    fun processDMA() {
        for (channel in dmaChannels) {
            if (channel.active && channel.size > 0) {
                val transferSize = min(channel.size - channel.current, 1024)
                
                if (transferSize > 0) {
                    val data = memory.read(channel.source + channel.current, transferSize)
                    
                    when {
                        channel.dest in FRAMEBUFFER_ADDR..FRAMEBUFFER_ADDR + FRAME_SIZE * 3 -> {
                            writeToMemory(channel.dest + channel.current, data)
                        }
                        channel.dest in TEXTURE_MEMORY_ADDR..TEXTURE_MEMORY_ADDR + textureMemoryPool.capacity() -> {
                            val offset = channel.dest - TEXTURE_MEMORY_ADDR + channel.current
                            textureMemoryPool.position(offset)
                            textureMemoryPool.put(data)
                        }
                        else -> {
                            memory.write(channel.dest + channel.current, data)
                        }
                    }
                    
                    channel.current += transferSize
                    
                    if (channel.current >= channel.size) {
                        channel.active = false
                        channel.current = 0
                        
                        if (channel.control and 0x1 != 0) {
                            dmaInterrupt = dmaInterrupt or (1 shl (channel.control shr 8))
                        }
                    }
                }
            }
        }
    }
    
    fun startCommandProcessor() {
        if (commandProcessorRunning) return
        
        commandProcessorRunning = true
        commandThread = Thread {
            while (commandProcessorRunning) {
                synchronized(commandLock) {
                    if (commandQueue.isNotEmpty()) {
                        val cmd = commandQueue.removeFirst()
                        processGPUCommand(cmd)
                        commandQueueLength = commandQueue.size
                    }
                }
                
                while (immediateCommandQueue.isNotEmpty()) {
                    val cmd = immediateCommandQueue.removeFirst()
                    processGPUCommand(cmd)
                }
                
                Thread.yield()
            }
        }.apply {
            priority = Thread.MAX_PRIORITY
            name = "GPU-Command-Processor"
            start()
        }
        
        Log.d(TAG, "GPU Command Processor started")
    }
    
    fun stopCommandProcessor() {
        commandProcessorRunning = false
        commandThread?.join(1000)
        commandThread = null
        Log.d(TAG, "GPU Command Processor stopped")
    }
    
    private fun processGPUCommand(cmd: GPUCommand) {
        when (cmd.type) {
            0x1000 -> cmdClear(cmd.params.map { it.toInt() }.toIntArray())
            0x1001 -> cmdDrawPrimitive(cmd.params.map { it.toInt() }.toIntArray())
            0x1002 -> cmdSwapBuffers()
            0x1003 -> cmdSetMatrix(cmd.params.map { it.toInt() }.toIntArray())
            
            0x2000 -> {
                val channel = (cmd.params[0].toInt() and 0x7)
                dmaChannels[channel].source = cmd.params[1].toInt()
                dmaChannels[channel].dest = cmd.params[2].toInt()
                dmaChannels[channel].size = cmd.params[3].toInt()
                dmaChannels[channel].control = cmd.params[4].toInt()
                dmaChannels[channel].active = true
            }
            
            0x3000 -> {
                val width = cmd.params[0].toInt()
                val height = cmd.params[1].toInt()
                val format = cmd.params[2].toInt()
                createGLTexture(width, height, format)
            }
            
            0x3001 -> {
                val textureId = cmd.params[0].toInt()
                val unit = cmd.params[1].toInt()
                bindTexture(textureId, unit)
            }
            
            0x4000 -> {
                val state = cmd.params[0].toInt()
                val value = cmd.params[1].toInt()
                setRenderState(state, value)
            }
            
            0x4001 -> {
                val x = cmd.params[0].toInt()
                val y = cmd.params[1].toInt()
                val width = cmd.params[2].toInt()
                val height = cmd.params[3].toInt()
                setViewport(x, y, width, height)
            }
            
            else -> {
                Log.w(TAG, "Unknown GPU command: 0x${cmd.type.toString(16)}")
            }
        }
        
        drawCalls++
    }
    
    fun writePixel(x: Int, y: Int, color: Int) {
        if (x in 0 until FRAME_WIDTH && y in 0 until FRAME_HEIGHT) {
            val offset = (y * FRAME_WIDTH + x) * 4
            frameBuffers[currentBufferIndex].putInt(offset, color)
        }
    }
    
    fun readPixel(x: Int, y: Int): Int {
        if (x in 0 until FRAME_WIDTH && y in 0 until FRAME_HEIGHT) {
            val offset = (y * FRAME_WIDTH + x) * 4
            return frameBuffers[currentBufferIndex].getInt(offset)
        }
        return 0
    }
    
    fun clearBuffers() {
        frameBuffers.forEach { buffer ->
            buffer.position(0)
            for (i in 0 until FRAME_WIDTH * FRAME_HEIGHT) {
                buffer.putInt(clearColor)
            }
            buffer.position(0)
        }
        
        if (glInitialized) {
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
        }
    }
    
    fun setViewport(x: Int, y: Int, width: Int, height: Int) {
        GLES30.glViewport(x, y, width, height)
        glViewport[0] = x
        glViewport[1] = y
        glViewport[2] = width
        glViewport[3] = height
    }
    
    fun setRenderState(state: Int, value: Int) {
        when (state) {
            0 -> {
                currentPipeline.blendEnable = value != 0
                applyPipelineState(currentPipeline)
            }
            1 -> {
                currentPipeline.depthTest = value != 0
                applyPipelineState(currentPipeline)
            }
            2 -> {
                currentPipeline.cullFace = value != 0
                applyPipelineState(currentPipeline)
            }
            3 -> {
                currentPipeline.scissorTest = value != 0
                applyPipelineState(currentPipeline)
            }
        }
    }
    
    fun writeToMemory(addr: Int, data: ByteArray) {
        when {
            addr in FRAMEBUFFER_ADDR until FRAMEBUFFER_ADDR + FRAME_SIZE * 3 -> {
                val bufferIndex = (addr - FRAMEBUFFER_ADDR) / FRAME_SIZE
                val offset = (addr - FRAMEBUFFER_ADDR) % FRAME_SIZE
                
                if (bufferIndex in 0..2 && offset + data.size <= FRAME_SIZE) {
                    frameBuffers[bufferIndex].position(offset)
                    frameBuffers[bufferIndex].put(data)
                }
            }
            
            addr in TEXTURE_MEMORY_ADDR until TEXTURE_MEMORY_ADDR + textureMemoryPool.capacity() -> {
                val offset = addr - TEXTURE_MEMORY_ADDR
                textureMemoryPool.position(offset)
                textureMemoryPool.put(data)
            }
        }
    }
    
    fun readFromMemory(addr: Int, size: Int): ByteArray {
        val data = ByteArray(size)
        
        when {
            addr in FRAMEBUFFER_ADDR until FRAMEBUFFER_ADDR + FRAME_SIZE * 3 -> {
                val bufferIndex = (addr - FRAMEBUFFER_ADDR) / FRAME_SIZE
                val offset = (addr - FRAMEBUFFER_ADDR) % FRAME_SIZE
                
                if (bufferIndex in 0..2 && offset + size <= FRAME_SIZE) {
                    frameBuffers[bufferIndex].position(offset)
                    frameBuffers[bufferIndex].get(data)
                }
            }
            
            addr in TEXTURE_MEMORY_ADDR until TEXTURE_MEMORY_ADDR + textureMemoryPool.capacity() -> {
                val offset = addr - TEXTURE_MEMORY_ADDR
                textureMemoryPool.position(offset)
                textureMemoryPool.get(data)
            }
        }
        
        return data
    }
    
    fun writeRegister(address: Int, value: Int) {
        when (address) {
            NV_PCRTC_START -> {
                backBufferAddr = value
                Log.d(TAG, "Framebuffer address set to 0x${value.toString(16)}")
            }
            
            NV_PGRAPH_INTR_EN -> {
            }
            
            else -> {
                commandQueue.add(GPUCommand(address, longArrayOf(value.toLong())))
                commandQueueLength = commandQueue.size
            }
        }
    }
    
    fun readRegister(address: Int): Int {
        return when (address) {
            NV_PCRTC_RASTER -> {
                (frameCounter % FRAME_HEIGHT).toInt()
            }
            
            NV_PGRAPH_INTR -> {
                val intr = dmaInterrupt
                dmaInterrupt = 0
                intr
            }
            
            else -> 0
        }
    }
    
    fun getPerformanceInfo(): Map<String, String> {
        val avgFrameTime = frameTimes.average()
        val fps = if (avgFrameTime > 0) 1000.0 / avgFrameTime else 0.0
        
        return mapOf(
            "FPS" to "%.1f".format(fps),
            "Frame Time" to "%.2f ms".format(avgFrameTime),
            "GPU Load" to "${(gpuLoad * 100).toInt()}%",
            "Triangles" to trianglesProcessed.toString(),
            "Draw Calls" to drawCalls.toString(),
            "Textures" to "${glTextures.size}/${textures.size}",
            "VRAM Used" to "${vramUsage / 1024 / 1024}MB",
            "Command Queue" to commandQueueLength.toString(),
            "OpenGL" to if (glInitialized) "3.0 Active" else "Fallback"
        )
    }
    
    fun resetStats() {
        drawCalls = 0
        trianglesProcessed = 0
        textureUploads = 0
        frameCounter = 0
        frameTimes.fill(16.67f)
        statsIndex = 0
        gpuLoad = 0.0f
    }
    
    fun shutdown() {
        stopCommandProcessor()
        
        if (glInitialized) {
            GLES30.glDeleteProgram(glProgram)
            GLES30.glDeleteTextures(3, intArrayOf(glFrameTexture, glRenderTexture, glDepthTexture), 0)
            GLES30.glDeleteFramebuffers(2, intArrayOf(glFramebuffer, msaaFramebuffer), 0)
            GLES30.glDeleteVertexArrays(2, glVAOs, 0)
            GLES30.glDeleteBuffers(4, glVBOs, 0)
            GLES30.glDeleteBuffers(2, glEBOs, 0)
            GLES30.glDeleteQueries(2, intArrayOf(glTimeQuery, glPrimitiveQuery), 0)
            
            glTextures.values.forEach { texture ->
                GLES30.glDeleteTextures(1, intArrayOf(texture.id), 0)
            }
        }
        
        textures.clear()
        glTextures.clear()
        commandQueue.clear()
        immediateCommandQueue.clear()
        
        frameBuffers.forEach { it.clear() }
        textureMemoryPool.clear()
        
        glInitialized = false
        glContextValid = false
        
        Log.d(TAG, "NV2A GPU with OpenGL ES 3.0 shutdown complete")
    }
    
    init {
        Log.d(TAG, "Initializing NV2A GPU with OpenGL ES 3.0 support...")
        
        clearBuffers()
        
        if (context != null) {
            Thread {
                initializeOpenGL()
            }.start()
        }
        
        startCommandProcessor()
        
        Log.d(TAG, "GPU initialized with ${VRAM_SIZE / 1024 / 1024}MB VRAM, OpenGL ES 3.0: $glInitialized")
    }
    
    private fun cmdClear(params: IntArray) {
        clearBuffers()
    }
    
    private fun cmdDrawPrimitive(params: IntArray) {
        if (params.size >= 6) {
            val primitiveType = params[0]
            val vertexCount = params[1]
            
            val vertices = mutableListOf<Vertex>()
            
            for (i in 0 until vertexCount) {
                val base = 2 + i * 4
                if (base + 3 < params.size) {
                    val x = Float.fromBits(params[base])
                    val y = Float.fromBits(params[base + 1])
                    val z = Float.fromBits(params[base + 2])
                    val color = params[base + 3]
                    
                    vertices.add(Vertex(x, y, z, 1.0f, color))
                }
            }
            
            when (primitiveType) {
                0 -> {
                    vertices.forEach { v ->
                        val x = v.x.toInt()
                        val y = v.y.toInt()
                        if (x in 0 until FRAME_WIDTH && y in 0 until FRAME_HEIGHT) {
                            writePixel(x, y, v.color)
                        }
                    }
                }
                3 -> {
                    for (i in 0 until vertices.size step 3) {
                        if (i + 2 < vertices.size) {
                            drawTriangle(vertices[i], vertices[i + 1], vertices[i + 2])
                            trianglesProcessed++
                        }
                    }
                }
            }
            
            drawCalls++
        }
    }
    
    private fun cmdSetMatrix(params: IntArray) {
        if (params.size >= 17) {
            val matrixType = params[0]
            val matrixData = params.sliceArray(1..16)
            
            val matrix = Matrix4f(
                Float.fromBits(matrixData[0]),
                Float.fromBits(matrixData[1]),
                Float.fromBits(matrixData[2]),
                Float.fromBits(matrixData[3]),
                Float.fromBits(matrixData[4]),
                Float.fromBits(matrixData[5]),
                Float.fromBits(matrixData[6]),
                Float.fromBits(matrixData[7]),
                Float.fromBits(matrixData[8]),
                Float.fromBits(matrixData[9]),
                Float.fromBits(matrixData[10]),
                Float.fromBits(matrixData[11]),
                Float.fromBits(matrixData[12]),
                Float.fromBits(matrixData[13]),
                Float.fromBits(matrixData[14]),
                Float.fromBits(matrixData[15])
            )
            
            when (matrixType) {
                0 -> modelViewMatrix = matrix
                1 -> projectionMatrix = matrix
                2 -> textureMatrix = matrix
            }
            
            Log.d(TAG, "Matrix set: type=$matrixType")
        }
    }
    
    private fun drawTriangle(v1: Vertex, v2: Vertex, v3: Vertex) {
        if (cullMode != CullMode.NONE) {
            val area = edgeFunction(v1, v2, v3)
            val cullFront = area >= 0
            val cullBack = area < 0
            
            if ((cullMode == CullMode.FRONT && cullFront) ||
                (cullMode == CullMode.BACK && cullBack) ||
                (cullMode == CullMode.FRONT_AND_BACK)) {
                return
            }
        }
        
        val minX = max(0, floor(min(v1.x, min(v2.x, v3.x))).toInt())
        val minY = max(0, floor(min(v1.y, min(v2.y, v3.y))).toInt())
        val maxX = min(FRAME_WIDTH - 1, ceil(max(v1.x, max(v2.x, v3.x))).toInt())
        val maxY = min(FRAME_HEIGHT - 1, ceil(max(v1.y, max(v2.y, v3.y))).toInt())
        
        val area = edgeFunction(v1, v2, v3)
        if (area == 0.0f) return
        
        val invArea = 1.0f / area
        
        for (y in minY..maxY) {
            for (x in minX..maxX) {
                if (!isInsideScissor(x, y)) continue
                
                val w1 = edgeFunction(v2, v3, x.toFloat(), y.toFloat())
                val w2 = edgeFunction(v3, v1, x.toFloat(), y.toFloat())
                val w3 = edgeFunction(v1, v2, x.toFloat(), y.toFloat())
                
                if (w1 >= 0 && w2 >= 0 && w3 >= 0) {
                    val b1 = w1 * invArea
                    val b2 = w2 * invArea
                    val b3 = w3 * invArea
                    
                    val color = interpolateColor(v1.color, v2.color, v3.color, b1, b2, b3)
                    
                    writePixel(x, y, color)
                }
            }
        }
    }
    
    private fun edgeFunction(a: Vertex, b: Vertex, c: Vertex): Float {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
    }
    
    private fun edgeFunction(a: Vertex, b: Vertex, px: Float, py: Float): Float {
        return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)
    }
    
    private fun interpolateColor(
        c1: Int, c2: Int, c3: Int,
        b1: Float, b2: Float, b3: Float
    ): Int {
        val a1 = (c1 shr 24) and 0xFF
        val r1 = (c1 shr 16) and 0xFF
        val g1 = (c1 shr 8) and 0xFF
        val b1c = c1 and 0xFF
        
        val a2 = (c2 shr 24) and 0xFF
        val r2 = (c2 shr 16) and 0xFF
        val g2 = (c2 shr 8) and 0xFF
        val b2c = c2 and 0xFF
        
        val a3 = (c3 shr 24) and 0xFF
        val r3 = (c3 shr 16) and 0xFF
        val g3 = (c3 shr 8) and 0xFF
        val b3c = c3 and 0xFF
        
        val a = (a1 * b1 + a2 * b2 + a3 * b3).toInt()
        val r = (r1 * b1 + r2 * b2 + r3 * b3).toInt()
        val g = (g1 * b1 + g2 * b2 + g3 * b3).toInt()
        val b = (b1c * b1 + b2c * b2 + b3c * b3).toInt()
        
        return (a shl 24) or (r shl 16) or (g shl 8) or b
    }
    
    private fun isInsideScissor(x: Int, y: Int): Boolean {
        if (!scissorEnabled) return true
        return scissor.contains(x, y)
    }
    
    fun createTexture(width: Int, height: Int, format: Int = NV_TEXTURE_FORMAT_A8R8G8B8): Int {
        val textureId = nextTextureId++
        
        val glTextureId = createGLTexture(width, height, format)
        
        val texture = TextureObject(
            id = textureId,
            width = width,
            height = height,
            format = format,
            mipmaps = 1,
            addr = -1,
            pitch = width * 4,
            data = null,
            glTextureId = glTextureId
        )
        
        textures[textureId] = texture
        return textureId
    }
    
    fun uploadTextureData(textureId: Int, data: ByteArray, offset: Int = 0) {
        val texture = textures[textureId] ?: return
        
        textureMemory.position(texture.addr)
        textureMemory.put(data, offset, minOf(data.size - offset, textureMemory.remaining()))
        
        val localData = ByteBuffer.allocateDirect(data.size).order(ByteOrder.LITTLE_ENDIAN)
        localData.put(data)
        localData.position(0)
        texture.data = localData
    }
    
    fun setScissor(x: Int, y: Int, width: Int, height: Int) {
        scissor = Rect(
            x.coerceIn(0, FRAME_WIDTH),
            y.coerceIn(0, FRAME_HEIGHT),
            (x + width).coerceIn(0, FRAME_WIDTH),
            (y + height).coerceIn(0, FRAME_HEIGHT)
        )
        Log.d(TAG, "Scissor set: $scissor")
    }
    
    fun enableScissor(enable: Boolean) {
        scissorEnabled = enable
        Log.d(TAG, "Scissor ${if (enable) "enabled" else "disabled"}")
    }
    
    private fun setIdentityMatrix(matrix: Matrix4f) {
        matrix.m00 = 1.0f; matrix.m01 = 0.0f; matrix.m02 = 0.0f; matrix.m03 = 0.0f
        matrix.m10 = 0.0f; matrix.m11 = 1.0f; matrix.m12 = 0.0f; matrix.m13 = 0.0f
        matrix.m20 = 0.0f; matrix.m21 = 0.0f; matrix.m22 = 1.0f; matrix.m23 = 0.0f
        matrix.m30 = 0.0f; matrix.m31 = 0.0f; matrix.m32 = 0.0f; matrix.m33 = 1.0f
    }
    
    fun setOrthographicProjection(left: Float, right: Float, bottom: Float, top: Float, near: Float, far: Float) {
        projectionMatrix.m00 = 2.0f / (right - left)
        projectionMatrix.m03 = -(right + left) / (right - left)
        projectionMatrix.m11 = 2.0f / (top - bottom)
        projectionMatrix.m13 = -(top + bottom) / (top - bottom)
        projectionMatrix.m22 = -2.0f / (far - near)
        projectionMatrix.m23 = -(far + near) / (far - near)
        projectionMatrix.m33 = 1.0f
    }
    
    fun setPerspectiveProjection(fovy: Float, aspect: Float, near: Float, far: Float) {
        val f = 1.0f / tan(fovy * 0.5f)
        
        projectionMatrix.m00 = f / aspect
        projectionMatrix.m11 = f
        projectionMatrix.m22 = (far + near) / (near - far)
        projectionMatrix.m23 = (2.0f * far * near) / (near - far)
        projectionMatrix.m32 = -1.0f
        projectionMatrix.m33 = 0.0f
    }
    
    fun step() {
        if (!displayEnabled) return
        
        processCommands()
        
        val currentTime = System.nanoTime()
        val delta = (currentTime - lastFrameTime) / 1_000_000.0
        
        if (delta >= 16.667) {
            if (vsyncEnabled) {
                present()
                frameCounter++
                
                fps = 1000.0 / delta
                frameTime = delta
                lastFrameTime = currentTime
            }
        }
    }
    
    private fun processCommands() {
        while (commandQueue.isNotEmpty()) {
            val cmd = commandQueue.removeFirst()
            executeCommand(cmd)
        }
    }
    
    private fun executeCommand(cmd: GPUCommand) {
        when (cmd.type) {
            in 0x0000..0x0FFF -> executeMethod(cmd.type, cmd.params.map { it.toInt() }.toIntArray())
            
            0x1000 -> cmdClear(cmd.params.map { it.toInt() }.toIntArray())
            0x1001 -> cmdDrawPrimitive(cmd.params.map { it.toInt() }.toIntArray())
            0x1002 -> cmdSwapBuffers()
            0x1003 -> cmdSetMatrix(cmd.params.map { it.toInt() }.toIntArray())
            0x1004 -> cmdSetTexture(cmd.params.map { it.toInt() }.toIntArray())
            0x1005 -> cmdSetRenderState(cmd.params.map { it.toInt() }.toIntArray())
            
            else -> Log.w(TAG, "Unknown GPU command: 0x${cmd.type.toString(16)}")
        }
    }
    
    private fun executeMethod(method: Int, params: IntArray) {
        when (method) {
            NV_PGRAPH_CTX_CONTROL -> {
                pgraphState.ctxControl = params[0]
            }
            NV_PGRAPH_CTX_USER -> {
                pgraphState.ctxUser = params[0]
            }
            NV_PGRAPH_CTX_SWITCH1 -> {
                pgraphState.ctxSwitch[0] = params[0]
            }
            NV_PGRAPH_CTX_SWITCH2 -> {
                pgraphState.ctxSwitch[1] = params[0]
            }
            NV_PGRAPH_CTX_SWITCH3 -> {
                pgraphState.ctxSwitch[2] = params[0]
            }
            NV_PGRAPH_CTX_SWITCH4 -> {
                pgraphState.ctxSwitch[3] = params[0]
            }
            NV_PGRAPH_CTX_SWITCH5 -> {
                pgraphState.ctxSwitch[4] = params[0]
            }
            NV_PGRAPH_BUMP0 -> {
                pgraphState.buffer = params[0]
            }
            NV_PGRAPH_BUMP1 -> {
                pgraphState.method = params[0]
                pgraphState.dataPos = 0
            }
            
            else -> {
                if (pgraphState.method != 0 && params.isNotEmpty()) {
                    for (param in params) {
                        if (pgraphState.dataPos < pgraphState.data.size) {
                            pgraphState.data[pgraphState.dataPos++] = param
                        }
                    }
                    
                    if (pgraphState.dataPos >= getMethodDataSize(pgraphState.method)) {
                        processMethodData(pgraphState.method, pgraphState.data, pgraphState.dataPos)
                        pgraphState.dataPos = 0
                    }
                }
            }
        }
    }
    
    private fun getMethodDataSize(method: Int): Int {
        return when (method) {
            0x0180, 0x0184, 0x0188, 0x018C -> 4
            0x0300 -> 1
            0x0304 -> 1
            0x0310 -> 1
            0x0314 -> 1
            0x0318 -> 1
            0x0400 -> 16
            0x0404 -> 16
            0x0408 -> 16
            0x0410 -> 4
            0x0414 -> 4
            else -> 1
        }
    }
    
    private fun processMethodData(method: Int, data: IntArray, size: Int) {
        when (method) {
            0x0300 -> {
                val primitiveType = data[0] and 0xF
                beginPrimitive(primitiveType)
            }
            
            0x0304 -> {
                endPrimitive()
            }
            
            0x0310 -> {
                if (size >= 4) {
                    val x = Float.fromBits(data[0])
                    val y = Float.fromBits(data[1])
                    val z = Float.fromBits(data[2])
                    val w = Float.fromBits(data[3])
                    
                    addVertex(x, y, z, w)
                }
            }
            
            0x0400 -> {
                if (size >= 16) {
                    modelViewMatrix = Matrix4f(
                        Float.fromBits(data[0]),
                        Float.fromBits(data[1]),
                        Float.fromBits(data[2]),
                        Float.fromBits(data[3]),
                        Float.fromBits(data[4]),
                        Float.fromBits(data[5]),
                        Float.fromBits(data[6]),
                        Float.fromBits(data[7]),
                        Float.fromBits(data[8]),
                        Float.fromBits(data[9]),
                        Float.fromBits(data[10]),
                        Float.fromBits(data[11]),
                        Float.fromBits(data[12]),
                        Float.fromBits(data[13]),
                        Float.fromBits(data[14]),
                        Float.fromBits(data[15])
                    )
                }
            }
            
            0x0404 -> {
                if (size >= 16) {
                    projectionMatrix = Matrix4f(
                        Float.fromBits(data[0]),
                        Float.fromBits(data[1]),
                        Float.fromBits(data[2]),
                        Float.fromBits(data[3]),
                        Float.fromBits(data[4]),
                        Float.fromBits(data[5]),
                        Float.fromBits(data[6]),
                        Float.fromBits(data[7]),
                        Float.fromBits(data[8]),
                        Float.fromBits(data[9]),
                        Float.fromBits(data[10]),
                        Float.fromBits(data[11]),
                        Float.fromBits(data[12]),
                        Float.fromBits(data[13]),
                        Float.fromBits(data[14]),
                        Float.fromBits(data[15])
                    )
                }
            }
            
            0x0410 -> {
                if (size >= 4) {
                    val x = data[0] and 0xFFFF
                    val y = (data[0] shr 16) and 0xFFFF
                    val width = data[1] and 0xFFFF
                    val height = (data[1] shr 16) and 0xFFFF
                    setViewport(x, y, width, height)
                }
            }
        }
    }
    
    private fun beginPrimitive(type: Int) {
        currentVertices.clear()
        
        currentPrimitiveType = when (type) {
            0 -> PrimitiveType.POINTS
            1 -> PrimitiveType.LINES
            2 -> PrimitiveType.LINE_STRIP
            3 -> PrimitiveType.TRIANGLES
            4 -> PrimitiveType.TRIANGLE_STRIP
            5 -> PrimitiveType.TRIANGLE_FAN
            6 -> PrimitiveType.QUADS
            else -> PrimitiveType.TRIANGLES
        }
        
        primitiveStarted = true
        Log.v(TAG, "Begin primitive: $currentPrimitiveType")
    }
    
    private fun endPrimitive() {
        if (!primitiveStarted) return
        
        flushPrimitive()
        primitiveStarted = false
    }
    
    private fun addVertex(x: Float, y: Float, z: Float, w: Float) {
        if (!primitiveStarted) return
        
        val vertex = Vertex(x, y, z, w)
        applyTransformations(vertex)
        currentVertices.add(vertex)
        
        when (currentPrimitiveType) {
            PrimitiveType.POINTS -> {
                if (currentVertices.size >= 1) {
                    flushPrimitive()
                }
            }
            PrimitiveType.LINES -> {
                if (currentVertices.size >= 2) {
                    flushPrimitive()
                }
            }
            PrimitiveType.TRIANGLES -> {
                if (currentVertices.size >= 3) {
                    flushPrimitive()
                }
            }
            PrimitiveType.QUADS -> {
                if (currentVertices.size >= 4) {
                    flushPrimitive()
                }
            }
            else -> {
                if (currentVertices.size >= 3) {
                    flushPrimitive()
                }
            }
        }
    }
    
    private fun flushPrimitive() {
        when (currentPrimitiveType) {
            PrimitiveType.POINTS -> drawPoints()
            PrimitiveType.LINES -> drawLines()
            PrimitiveType.LINE_STRIP -> drawLineStrip()
            PrimitiveType.TRIANGLES -> drawTriangles()
            PrimitiveType.TRIANGLE_STRIP -> drawTriangleStrip()
            PrimitiveType.TRIANGLE_FAN -> drawTriangleFan()
            PrimitiveType.QUADS -> drawQuads()
        }
        
        currentVertices.clear()
        drawCalls++
    }
    
    private fun drawPoints() {
        for (vertex in currentVertices) {
            val x = vertex.x.toInt()
            val y = vertex.y.toInt()
            
            if (x in 0 until FRAME_WIDTH && y in 0 until FRAME_HEIGHT) {
                if (isInsideScissor(x, y)) {
                    writePixel(x, y, vertex.color)
                }
            }
        }
    }
    
    private fun drawLines() {
        for (i in 0 until currentVertices.size step 2) {
            if (i + 1 < currentVertices.size) {
                val v1 = currentVertices[i]
                val v2 = currentVertices[i + 1]
                
                drawLine(
                    v1.x.toInt(), v1.y.toInt(),
                    v2.x.toInt(), v2.y.toInt(),
                    v1.color
                )
            }
        }
    }
    
    private fun drawLineStrip() {
        for (i in 0 until currentVertices.size - 1) {
            val v1 = currentVertices[i]
            val v2 = currentVertices[i + 1]
            
            drawLine(
                v1.x.toInt(), v1.y.toInt(),
                v2.x.toInt(), v2.y.toInt(),
                v1.color
            )
        }
    }
    
    private fun drawTriangles() {
        for (i in 0 until currentVertices.size step 3) {
            if (i + 2 < currentVertices.size) {
                val v1 = currentVertices[i]
                val v2 = currentVertices[i + 1]
                val v3 = currentVertices[i + 2]
                
                drawTriangle(v1, v2, v3)
                trianglesProcessed++
            }
        }
    }
    
    private fun drawTriangleStrip() {
        for (i in 0 until currentVertices.size - 2) {
            val v1 = currentVertices[i]
            val v2 = currentVertices[i + 1]
            val v3 = currentVertices[i + 2]
            
            if (i % 2 == 0) {
                drawTriangle(v1, v2, v3)
            } else {
                drawTriangle(v2, v1, v3)
            }
            
            trianglesProcessed++
        }
    }
    
    private fun drawTriangleFan() {
        if (currentVertices.size < 3) return
        
        val center = currentVertices[0]
        
        for (i in 1 until currentVertices.size - 1) {
            val v1 = center
            val v2 = currentVertices[i]
            val v3 = currentVertices[i + 1]
            
            drawTriangle(v1, v2, v3)
            trianglesProcessed++
        }
    }
    
    private fun drawQuads() {
        for (i in 0 until currentVertices.size step 4) {
            if (i + 3 < currentVertices.size) {
                val v1 = currentVertices[i]
                val v2 = currentVertices[i + 1]
                val v3 = currentVertices[i + 2]
                val v4 = currentVertices[i + 3]
                
                drawTriangle(v1, v2, v3)
                drawTriangle(v1, v3, v4)
                trianglesProcessed += 2
            }
        }
    }
    
    private fun drawLine(x1: Int, y1: Int, x2: Int, y2: Int, color: Int) {
        var currentX = x1
        var currentY = y1
        val x2Final = x2
        val y2Final = y2
        
        val dx = abs(x2Final - currentX)
        val dy = abs(y2Final - currentY)
        val sx = if (currentX < x2Final) 1 else -1
        val sy = if (currentY < y2Final) 1 else -1
        var err = dx - dy
        
        while (true) {
            if (currentX in 0 until FRAME_WIDTH && currentY in 0 until FRAME_HEIGHT) {
                if (isInsideScissor(currentX, currentY)) {
                    writePixel(currentX, currentY, color)
                }
            }
            
            if (currentX == x2Final && currentY == y2Final) break
            
            val e2 = err * 2
            if (e2 > -dy) {
                err -= dy
                currentX += sx
            }
            if (e2 < dx) {
                err += dx
                currentY += sy
            }
        }
    }
    
    private fun applyTransformations(vertex: Vertex) {
        val modelViewPos = floatArrayOf(vertex.x, vertex.y, vertex.z, vertex.w)
        val transformed = modelViewMatrix.multiply(modelViewPos)
        
        val projected = projectionMatrix.multiply(transformed)
        
        if (projected[3] != 0.0f) {
            vertex.x = projected[0] / projected[3]
            vertex.y = projected[1] / projected[3]
            vertex.z = projected[2] / projected[3]
            vertex.w = 1.0f / projected[3]
        }
        
        vertex.x = viewport.left + (vertex.x + 1.0f) * viewport.width() / 2.0f
        vertex.y = viewport.top + (1.0f - vertex.y) * viewport.height() / 2.0f
        
        if (lightingEnabled) {
            vertex.color = calculateLighting(vertex)
        }
        
        if (fogEnabled) {
            vertex.color = applyFog(vertex)
        }
    }
    
    private fun calculateLighting(vertex: Vertex): Int {
        var r = (vertex.color shr 16) and 0xFF
        var g = (vertex.color shr 8) and 0xFF
        var b = vertex.color and 0xFF
        
        if (lights.any { it.enabled }) {
            val lightIntensity = 0.8f
            r = (r * lightIntensity).toInt()
            g = (g * lightIntensity).toInt()
            b = (b * lightIntensity).toInt()
        }
        
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }
    
    private fun applyFog(vertex: Vertex): Int {
        val fogFactor = vertex.fog.coerceIn(0.0f, 1.0f)
        
        val a1 = (vertex.color shr 24) and 0xFF
        val r1 = (vertex.color shr 16) and 0xFF
        val g1 = (vertex.color shr 8) and 0xFF
        val b1 = vertex.color and 0xFF
        
        val a2 = (fogColor shr 24) and 0xFF
        val r2 = (fogColor shr 16) and 0xFF
        val g2 = (fogColor shr 8) and 0xFF
        val b2 = fogColor and 0xFF
        
        val a = (a1 + (a2 - a1) * fogFactor).toInt()
        val r = (r1 + (r2 - r1) * fogFactor).toInt()
        val g = (g1 + (g2 - g1) * fogFactor).toInt()
        val b = (b1 + (b2 - b1) * fogFactor).toInt()
        
        return (a shl 24) or (r shl 16) or (g shl 8) or b
    }
    
    private fun cmdSetTexture(params: IntArray) {
        if (params.size >= 2) {
            val stage = params[0]
            val textureId = params[1]
            
            Log.d(TAG, "Set texture stage $stage to texture $textureId")
        }
    }
    
    private fun cmdSetRenderState(params: IntArray) {
        if (params.size >= 2) {
            val state = params[0]
            val value = params[1]
            
            when (state) {
                0 -> {
                    cullMode = when (value) {
                        0 -> CullMode.NONE
                        1 -> CullMode.FRONT
                        2 -> CullMode.BACK
                        3 -> CullMode.FRONT_AND_BACK
                        else -> CullMode.BACK
                    }
                }
                
                1 -> {
                    blendEnabled = value != 0
                }
                
                2 -> {
                    depthTestEnabled = value != 0
                }
                
                3 -> {
                    alphaTestEnabled = value != 0
                }
                
                4 -> {
                    fogEnabled = value != 0
                }
                
                5 -> {
                    lightingEnabled = value != 0
                }
            }
            
            Log.d(TAG, "Render state set: state=$state, value=$value")
        }
    }
    
    private fun submitMethod(address: Int, value: Int) {
        when (address) {
            NV_PGRAPH_BUMP0 -> {
                commandQueue.add(GPUCommand(value, longArrayOf()))
            }
            NV_PGRAPH_BUMP1 -> {
                commandQueue.add(GPUCommand(value, longArrayOf()))
            }
            else -> {
                if (address >= NV_PGRAPH_BUMP1 + 4) {
                    val lastCmd = commandQueue.lastOrNull()
                    if (lastCmd != null) {
                        commandQueue[commandQueue.size - 1] = lastCmd.copy(
                            params = lastCmd.params + value.toLong()
                        )
                    }
                }
            }
        }
    }
    
    fun setTexture(stage: Int, textureId: Int) {
        Log.d(TAG, "Set texture stage $stage to texture $textureId")
    }
    
    fun setTransform(matrixType: Int, matrix: Matrix4f) {
        when (matrixType) {
            0 -> modelViewMatrix = matrix
            1 -> projectionMatrix = matrix
            2 -> textureMatrix = matrix
        }
    }
    
    fun drawIndexedPrimitive(
        primitiveType: PrimitiveType,
        baseVertexIndex: Int,
        minVertexIndex: Int,
        numVertices: Int,
        startIndex: Int,
        primCount: Int
    ) {
        Log.d(TAG, "Draw indexed primitive: $primitiveType, vertices: $numVertices")
        drawCalls++
    }
    
    private fun initializeGPU() {
        clearBuffers()
        
        textureMemory.position(0)
        for (i in 0 until textureMemory.capacity()) {
            textureMemory.put(0)
        }
        textureMemory.position(0)
        
        setIdentityMatrix(modelViewMatrix)
        setIdentityMatrix(projectionMatrix)
        setIdentityMatrix(textureMatrix)
        
        setOrthographicProjection(0.0f, FRAME_WIDTH.toFloat(), FRAME_HEIGHT.toFloat(), 0.0f, -1.0f, 1.0f)
        
        lights.forEach { light ->
            light.enabled = false
        }
        
        lights[0].enabled = true
        lights[0].position[3] = 0.0f
        lights[0].diffuse = floatArrayOf(1.0f, 1.0f, 1.0f, 1.0f)
        lights[0].ambient = floatArrayOf(0.2f, 0.2f, 0.2f, 1.0f)
    }
    
    private fun initializeDefaultState() {
        viewport = Rect(0, 0, FRAME_WIDTH, FRAME_HEIGHT)
        scissor = Rect(0, 0, FRAME_WIDTH, FRAME_HEIGHT)
        scissorEnabled = false
        
        cullMode = CullMode.BACK
        blendEnabled = false
        depthTestEnabled = false
        depthWriteEnabled = true
        alphaTestEnabled = false
        alphaRef = 128
        alphaFunc = CompareFunc.GREATER
        
        fogEnabled = false
        fogStart = 0.0f
        fogEnd = 1.0f
        fogColor = 0xFFFFFFFF.toInt()
        fogDensity = 1.0f
        
        lightingEnabled = false
        materialShininess = 0.0f
    }
}