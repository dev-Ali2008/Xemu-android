package og.xaniteog

import android.content.Context
import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.opengl.Matrix
import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.ShortBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class XboxRenderer(
    private val context: Context,
    private val surfaceView: GLSurfaceView
) : GLSurfaceView.Renderer {

    companion object {
        private const val TAG = "XboxRenderer"
        
        // Display Modes
        const val MODE_640x320 = 0
        const val MODE_640x480 = 1
        const val MODE_640x576 = 2
        const val MODE_720x480 = 3
        const val MODE_720x576 = 4
        const val MODE_1280x720 = 5
        const val MODE_1920x1080 = 6
        
        // Aspect Ratios
        const val ASPECT_4_3 = 0
        const val ASPECT_16_9 = 1
        const val ASPECT_STRETCH = 2
        const val ASPECT_FILL = 3
        
        // Filter Types
        const val FILTER_NEAREST = 0
        const val FILTER_BILINEAR = 1
        const val FILTER_TRILINEAR = 2
        
        // VSync Modes
        const val VSYNC_OFF = 0
        const val VSYNC_ON = 1
        const val VSYNC_ADAPTIVE = 2
        
        // Color Spaces
        const val COLORSPACE_LINEAR = 0
        const val COLORSPACE_SRGB = 1
        
        // Display Flags
        const val FLAG_FULLSCREEN = 0x1
        const val FLAG_VSYNC = 0x2
        const val FLAG_TRIPLE_BUFFER = 0x4
        const val FLAG_HARDWARE_ACCEL = 0x8
        const val FLAG_DEBUG_OVERLAY = 0x10
        const val FLAG_FPS_COUNTER = 0x20
        const val FLAG_WIREFRAME = 0x40
        const val FLAG_SHOW_STATS = 0x80
        
        // Xbox Native Resolutions
        val RESOLUTIONS = mapOf(
            MODE_640x480 to Pair(640, 480),
            MODE_640x576 to Pair(640, 576),
            MODE_720x480 to Pair(720, 480),
            MODE_720x576 to Pair(720, 576),
            MODE_1280x720 to Pair(1280, 720),
            MODE_1920x1080 to Pair(1920, 1080)
        )
        
        // Shaders
        private const val VERTEX_SHADER = """
            #version 300 es
            precision highp float;
            
            layout(location = 0) in vec3 aPosition;
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
            
            void main() {
                vColor = aColor;
                vTexCoord = (uTextureMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
                vNormal = mat3(uModelMatrix) * aNormal;
                vFragPos = vec3(uModelMatrix * vec4(aPosition, 1.0));
                
                gl_Position = uMVPMatrix * vec4(aPosition, 1.0);
            }
        """
        
        private const val FRAGMENT_SHADER = """
            #version 300 es
            precision highp float;
            
            in vec4 vColor;
            in vec2 vTexCoord;
            in vec3 vNormal;
            in vec3 vFragPos;
            
            out vec4 fragColor;
            
            uniform sampler2D uTexture;
            uniform bool uUseTexture;
            uniform bool uUseLighting;
            
            uniform vec3 uViewPos;
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
            
            vec4 calculateLighting(vec3 normal, vec3 fragPos, vec3 viewDir) {
                vec4 result = vec4(0.0, 0.0, 0.0, 1.0);
                
                for(int i = 0; i < uLightCount; i++) {
                    if(i >= MAX_LIGHTS) break;
                    
                    vec3 lightDir = normalize(uLightPosition[i] - fragPos);
                    float distance = length(uLightPosition[i] - fragPos);
                    float attenuation = 1.0 / (uLightConstant[i] + 
                                             uLightLinear[i] * distance + 
                                             uLightQuadratic[i] * distance * distance);
                    
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
                
                if(uUseLighting) {
                    vec3 norm = normalize(vNormal);
                    vec3 viewDir = normalize(uViewPos - vFragPos);
                    vec4 lighting = calculateLighting(norm, vFragPos, viewDir);
                    texColor *= lighting;
                }
                
                fragColor = texColor;
            }
        """
        
        // Skybox Shaders
        private const val SKYBOX_VERTEX_SHADER = """
            #version 300 es
            precision highp float;
            
            layout(location = 0) in vec3 aPosition;
            
            out vec3 vTexCoord;
            
            uniform mat4 uProjection;
            uniform mat4 uView;
            
            void main() {
                vTexCoord = aPosition;
                vec4 pos = uProjection * uView * vec4(aPosition, 1.0);
                gl_Position = pos.xyww;
            }
        """
        
        private const val SKYBOX_FRAGMENT_SHADER = """
            #version 300 es
            precision highp float;
            
            in vec3 vTexCoord;
            out vec4 fragColor;
            
            uniform samplerCube uSkybox;
            
            void main() {
                fragColor = texture(uSkybox, vTexCoord);
            }
        """
    }

    // ===== Renderer Structures =====
    
    data class DisplayMode(
        val width: Int,
        val height: Int,
        val refreshRate: Int,
        val aspectRatio: Float,
        val isProgressive: Boolean,
        val isInterlaced: Boolean
    )
    
    data class Viewport(
        var x: Int,
        var y: Int,
        var width: Int,
        var height: Int,
        var minZ: Float = 0.0f,
        var maxZ: Float = 1.0f
    )
    
    data class ScissorRect(
        var left: Int,
        var top: Int,
        var right: Int,
        var bottom: Int
    ) {
        val width: Int get() = right - left
        val height: Int get() = bottom - top
        val isEmpty: Boolean get() = width <= 0 || height <= 0
    }
    
    data class RenderState(
        var alphaBlend: Boolean = false,
        var depthTest: Boolean = true,
        var depthWrite: Boolean = true,
        var cullMode: Int = GLES30.GL_BACK,
        var wireframe: Boolean = false,
        var fillMode: Int = GLES30.GL_FILL,
        var alphaTest: Boolean = false,
        var stencilTest: Boolean = false,
        var scissorTest: Boolean = false,
        var blendSrc: Int = GLES30.GL_SRC_ALPHA,
        var blendDst: Int = GLES30.GL_ONE_MINUS_SRC_ALPHA,
        var blendOp: Int = GLES30.GL_FUNC_ADD,
        var depthFunc: Int = GLES30.GL_LESS
    )
    
    data class Material(
        var ambient: FloatArray = floatArrayOf(0.2f, 0.2f, 0.2f, 1.0f),
        var diffuse: FloatArray = floatArrayOf(0.8f, 0.8f, 0.8f, 1.0f),
        var specular: FloatArray = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f),
        var shininess: Float = 32.0f,
        var textureId: Int = -1,
        var normalMapId: Int = -1,
        var specularMapId: Int = -1
    )
    
    data class Light(
        var enabled: Boolean = false,
        var type: Int = 0, // 0 = directional, 1 = point, 2 = spot
        var position: FloatArray = floatArrayOf(0.0f, 0.0f, 1.0f, 0.0f),
        var direction: FloatArray = floatArrayOf(0.0f, 0.0f, -1.0f),
        var ambient: FloatArray = floatArrayOf(0.1f, 0.1f, 0.1f, 1.0f),
        var diffuse: FloatArray = floatArrayOf(1.0f, 1.0f, 1.0f, 1.0f),
        var specular: FloatArray = floatArrayOf(1.0f, 1.0f, 1.0f, 1.0f),
        var constant: Float = 1.0f,
        var linear: Float = 0.09f,
        var quadratic: Float = 0.032f,
        var cutoff: Float = 12.5f,
        var outerCutoff: Float = 17.5f
    )
    
    data class Texture(
        val id: Int,
        val width: Int,
        val height: Int,
        val format: Int,
        val internalFormat: Int,
        val type: Int,
        var wrapS: Int = GLES30.GL_REPEAT,
        var wrapT: Int = GLES30.GL_REPEAT,
        var minFilter: Int = GLES30.GL_LINEAR_MIPMAP_LINEAR,
        var magFilter: Int = GLES30.GL_LINEAR,
        var anisotropy: Float = 1.0f,
        var mipmapLevels: Int = 0
    )
    
    data class Mesh(
        val vertices: FloatBuffer,
        val normals: FloatBuffer,
        val texCoords: FloatBuffer,
        val indices: ShortBuffer,
        val vertexCount: Int,
        val indexCount: Int,
        val vao: Int,
        val vbo: IntArray,
        val ebo: Int
    )
    
    data class RenderStats(
        var frames: Long = 0,
        var vertices: Long = 0,
        var triangles: Long = 0,
        var drawCalls: Long = 0,
        var textureSwitches: Long = 0,
        var shaderSwitches: Long = 0,
        var stateChanges: Long = 0,
        var bufferSwaps: Long = 0,
        var clearOperations: Long = 0,
        var frameTime: Double = 0.0,
        var fps: Double = 0.0,
        var cpuTime: Double = 0.0,
        var gpuTime: Double = 0.0
    )

    // ===== OpenGL State =====
    
    private var glProgram = 0
    private var skyboxProgram = 0
    private var glFrameTexture = 0
    private var glRenderTexture = 0
    private var glDepthTexture = 0
    private var glFramebuffer = 0
    private var glDepthbuffer = 0
    
    private var glVAOs = IntArray(2)
    private var glVBOs = IntArray(4)
    private var glEBOs = IntArray(2)
    
    private var glUniformLocations = mutableMapOf<String, Int>()
    private var glTextureUnits = IntArray(8) { -1 }
    
    private var glInitialized = false
    private var glSupportsNPOT = false
    private var glMaxAnisotropy = 1.0f
    
    // Frame buffer for Xbox output
    private val frameBuffer = ByteBuffer.allocateDirect(640 * 480 * 4)
        .order(ByteOrder.nativeOrder())
    
    // Matrices
    private val mvpMatrix = FloatArray(16)
    private val modelMatrix = FloatArray(16)
    private val viewMatrix = FloatArray(16)
    private val projectionMatrix = FloatArray(16)
    private val textureMatrix = FloatArray(16)
    
    // Display Configuration
    private var displayMode = MODE_640x480
    private var aspectRatio = ASPECT_4_3
    private var filterType = FILTER_BILINEAR
    private var vsyncMode = VSYNC_ON
    private var displayFlags = FLAG_VSYNC
    private var backgroundColor = floatArrayOf(0.0f, 0.0f, 0.0f, 1.0f)
    
    // Render State
    private val renderState = RenderState()
    private val defaultState = RenderState()
    
    // Viewport & Scissor
    private val viewport = Viewport(0, 0, 640, 480)
    private val scissor = ScissorRect(0, 0, 640, 480)
    
    // Textures
    private val textures = mutableMapOf<Int, Texture>()
    private var currentTextureId = 0
    private var boundTextures = IntArray(8) { -1 }
    
    // Materials
    private val materials = mutableMapOf<Int, Material>()
    private var currentMaterial: Material? = null
    
    // Lights
    private val lights = Array(8) { Light() }
    private var activeLights = 0
    
    // Meshes
    private val meshes = mutableMapOf<Int, Mesh>()
    
    // Skybox
    private var skyboxVAO = 0
    private var skyboxVBO = 0
    private var skyboxTexture = 0
    
    // Frame Timing
    private var lastFrameTime = System.nanoTime()
    private var frameStartTime = 0L
    private var frameEndTime = 0L
    private var targetFrameTime = 1000000000L / 60 // 60 FPS in nanoseconds
    private var frameCounter = 0L
    private var fpsTimer = 0L
    private var fps = 0.0
    
    // Performance Stats
    private val stats = RenderStats()
    
    // Debug
    private var showDebugOverlay = false
    private var showWireframe = false
    private var showStats = false
    
    // Camera
    private var cameraPosition = floatArrayOf(0.0f, 0.0f, 3.0f)
    private var cameraTarget = floatArrayOf(0.0f, 0.0f, 0.0f)
    private var cameraUp = floatArrayOf(0.0f, 1.0f, 0.0f)

    // ===== GLSurfaceView.Renderer Implementation =====
    
    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        Log.d(TAG, "OpenGL ES 3.0 Surface Created")
        
        // Initialize OpenGL
        initializeOpenGL()
        
        // Set initial state
        GLES30.glClearColor(backgroundColor[0], backgroundColor[1], backgroundColor[2], backgroundColor[3])
        GLES30.glEnable(GLES30.GL_DEPTH_TEST)
        GLES30.glDepthFunc(GLES30.GL_LESS)
        GLES30.glEnable(GLES30.GL_CULL_FACE)
        GLES30.glCullFace(GLES30.GL_BACK)
        
        // Create shaders
        glProgram = createShaderProgram(VERTEX_SHADER, FRAGMENT_SHADER)
        skyboxProgram = createShaderProgram(SKYBOX_VERTEX_SHADER, SKYBOX_FRAGMENT_SHADER)
        
        // Initialize uniforms
        initializeUniforms()
        
        // Create frame buffer texture
        createFrameBufferTexture()
        
        // Create skybox
        createSkybox()
        
        // Set up lighting
        initializeLights()
        
        glInitialized = true
        Log.d(TAG, "OpenGL ES 3.0 Initialization Complete")
    }
    
    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        Log.d(TAG, "Surface Changed: ${width}x${height}")
        
        // لا يمكن إعادة تعيين عرض وارتفاع surfaceView مباشرة - إزالة هذين السطرين
        // surfaceView.width = width
        // surfaceView.height = height
        
        GLES30.glViewport(0, 0, width, height)
        
        // Update viewport
        viewport.width = width
        viewport.height = height
        scissor.right = width
        scissor.bottom = height
        
        // Update projection matrix
        updateProjectionMatrix(width, height)
        
        // Update scaling
        updateScaling(width, height)
    }
    
    override fun onDrawFrame(gl: GL10?) {
        frameStartTime = System.nanoTime()
        
        // Clear buffers
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT or GLES30.GL_DEPTH_BUFFER_BIT)
        
        // Update matrices
        updateMatrices()
        
        // Render skybox first
        renderSkybox()
        
        // Render scene
        renderScene()
        
        // Render frame buffer texture (Xbox output)
        renderFrameBuffer()
        
        // Update stats
        frameEndTime = System.nanoTime()
        updateFrameStats()
        
        // Draw debug overlay if enabled
        if (showDebugOverlay || showStats) {
            renderDebugOverlay()
        }
    }

    // ===== Initialization Functions =====
    
    private fun initializeOpenGL() {
        // Check OpenGL ES 3.0 support
        val version = GLES30.glGetString(GLES30.GL_VERSION)
        val renderer = GLES30.glGetString(GLES30.GL_RENDERER)
        val extensions = GLES30.glGetString(GLES30.GL_EXTENSIONS)
        
        Log.d(TAG, "OpenGL Version: $version")
        Log.d(TAG, "Renderer: $renderer")
        
        glSupportsNPOT = extensions?.contains("GL_OES_texture_npot") ?: false
        
        // Get max anisotropy
        val maxAniso = FloatArray(1)
        // التحقق من وجود الامتداد قبل استخدامه
        GLES30.glGetFloatv(0x84FF, maxAniso, 0) // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
        glMaxAnisotropy = maxAniso[0]
        
        // Initialize matrices
        Matrix.setIdentityM(modelMatrix, 0)
        Matrix.setIdentityM(viewMatrix, 0)
        Matrix.setIdentityM(projectionMatrix, 0)
        Matrix.setIdentityM(textureMatrix, 0)
        Matrix.setIdentityM(mvpMatrix, 0)
    }
    
    private fun createShaderProgram(vertexSource: String, fragmentSource: String): Int {
        val vertexShader = compileShader(GLES30.GL_VERTEX_SHADER, vertexSource)
        if (vertexShader == 0) {
            Log.e(TAG, "Failed to compile vertex shader")
            return 0
        }
        
        val fragmentShader = compileShader(GLES30.GL_FRAGMENT_SHADER, fragmentSource)
        if (fragmentShader == 0) {
            Log.e(TAG, "Failed to compile fragment shader")
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
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, compileStatus, 0)
        
        if (compileStatus[0] == 0) {
            val infoLog = GLES30.glGetShaderInfoLog(shader)
            Log.e(TAG, "Shader compilation failed:\n$infoLog")
            GLES30.glDeleteShader(shader)
            return 0
        }
        
        return shader
    }
    
    private fun initializeUniforms() {
        // Main program uniforms
        GLES30.glUseProgram(glProgram)
        
        glUniformLocations["uMVPMatrix"] = GLES30.glGetUniformLocation(glProgram, "uMVPMatrix")
        glUniformLocations["uModelMatrix"] = GLES30.glGetUniformLocation(glProgram, "uModelMatrix")
        glUniformLocations["uViewMatrix"] = GLES30.glGetUniformLocation(glProgram, "uViewMatrix")
        glUniformLocations["uProjectionMatrix"] = GLES30.glGetUniformLocation(glProgram, "uProjectionMatrix")
        glUniformLocations["uTextureMatrix"] = GLES30.glGetUniformLocation(glProgram, "uTextureMatrix")
        glUniformLocations["uTexture"] = GLES30.glGetUniformLocation(glProgram, "uTexture")
        glUniformLocations["uUseTexture"] = GLES30.glGetUniformLocation(glProgram, "uUseTexture")
        glUniformLocations["uUseLighting"] = GLES30.glGetUniformLocation(glProgram, "uUseLighting")
        glUniformLocations["uViewPos"] = GLES30.glGetUniformLocation(glProgram, "uViewPos")
        glUniformLocations["uMaterialAmbient"] = GLES30.glGetUniformLocation(glProgram, "uMaterialAmbient")
        glUniformLocations["uMaterialDiffuse"] = GLES30.glGetUniformLocation(glProgram, "uMaterialDiffuse")
        glUniformLocations["uMaterialSpecular"] = GLES30.glGetUniformLocation(glProgram, "uMaterialSpecular")
        glUniformLocations["uMaterialShininess"] = GLES30.glGetUniformLocation(glProgram, "uMaterialShininess")
        glUniformLocations["uLightCount"] = GLES30.glGetUniformLocation(glProgram, "uLightCount")
        
        // Light uniforms
        for (i in 0 until 8) {
            glUniformLocations["uLightPosition[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightPosition[$i]")
            glUniformLocations["uLightDirection[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightDirection[$i]")
            glUniformLocations["uLightAmbient[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightAmbient[$i]")
            glUniformLocations["uLightDiffuse[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightDiffuse[$i]")
            glUniformLocations["uLightSpecular[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightSpecular[$i]")
            glUniformLocations["uLightConstant[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightConstant[$i]")
            glUniformLocations["uLightLinear[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightLinear[$i]")
            glUniformLocations["uLightQuadratic[$i]"] = GLES30.glGetUniformLocation(glProgram, "uLightQuadratic[$i]")
        }
        
        // Skybox program uniforms
        GLES30.glUseProgram(skyboxProgram)
        glUniformLocations["uSkybox"] = GLES30.glGetUniformLocation(skyboxProgram, "uSkybox")
        glUniformLocations["uProjection"] = GLES30.glGetUniformLocation(skyboxProgram, "uProjection")
        glUniformLocations["uView"] = GLES30.glGetUniformLocation(skyboxProgram, "uView")
    }
    
    private fun createFrameBufferTexture() {
        val textureIds = IntArray(3)
        GLES30.glGenTextures(3, textureIds, 0)
        glFrameTexture = textureIds[0]
        glRenderTexture = textureIds[1]
        glDepthTexture = textureIds[2]
        
        // Frame texture (for Xbox output)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glFrameTexture)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8,
            640, 480, 0,
            GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE,
            frameBuffer
        )
        
        // Create framebuffer
        val fboIds = IntArray(1)
        GLES30.glGenFramebuffers(1, fboIds, 0)
        glFramebuffer = fboIds[0]
        
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, glFramebuffer)
        
        // Attach render texture
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glRenderTexture)
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8,
            640, 480, 0,
            GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE,
            null
        )
        
        GLES30.glFramebufferTexture2D(
            GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
            GLES30.GL_TEXTURE_2D, glRenderTexture, 0
        )
        
        // Attach depth texture
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glDepthTexture)
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D, 0, GLES30.GL_DEPTH_COMPONENT24,
            640, 480, 0,
            GLES30.GL_DEPTH_COMPONENT, GLES30.GL_UNSIGNED_INT,
            null
        )
        
        GLES30.glFramebufferTexture2D(
            GLES30.GL_FRAMEBUFFER, GLES30.GL_DEPTH_ATTACHMENT,
            GLES30.GL_TEXTURE_2D, glDepthTexture, 0
        )
        
        val status = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER)
        if (status != GLES30.GL_FRAMEBUFFER_COMPLETE) {
            Log.e(TAG, "Framebuffer not complete: $status")
        }
        
        GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
    }
    
    private fun createSkybox() {
        val skyboxVertices = floatArrayOf(
            // Positions
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            
            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,
            
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            
            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,
            
            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,
            
            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        )
        
        val vertexBuffer = ByteBuffer.allocateDirect(skyboxVertices.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(skyboxVertices)
        vertexBuffer.position(0)
        
        val vaoIds = IntArray(1)
        val vboIds = IntArray(1)
        
        GLES30.glGenVertexArrays(1, vaoIds, 0)
        GLES30.glGenBuffers(1, vboIds, 0)
        
        skyboxVAO = vaoIds[0]
        skyboxVBO = vboIds[0]
        
        GLES30.glBindVertexArray(skyboxVAO)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, skyboxVBO)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, skyboxVertices.size * 4, vertexBuffer, GLES30.GL_STATIC_DRAW)
        
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 3 * 4, 0)
        
        GLES30.glBindVertexArray(0)
        
        // Create skybox texture (simplified - would normally load 6 images)
        val skyboxTexIds = IntArray(1)
        GLES30.glGenTextures(1, skyboxTexIds, 0)
        skyboxTexture = skyboxTexIds[0]
        
        GLES30.glBindTexture(GLES30.GL_TEXTURE_CUBE_MAP, skyboxTexture)
        
        // Set texture parameters
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_CUBE_MAP, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_CUBE_MAP, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_CUBE_MAP, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_CUBE_MAP, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_CUBE_MAP, GLES30.GL_TEXTURE_WRAP_R, GLES30.GL_CLAMP_TO_EDGE)
    }
    
    private fun initializeLights() {
        // Setup default directional light
        lights[0].enabled = true
        lights[0].type = 0 // Directional
        lights[0].direction = floatArrayOf(-0.2f, -1.0f, -0.3f)
        lights[0].ambient = floatArrayOf(0.05f, 0.05f, 0.05f, 1.0f)
        lights[0].diffuse = floatArrayOf(0.4f, 0.4f, 0.4f, 1.0f)
        lights[0].specular = floatArrayOf(0.5f, 0.5f, 0.5f, 1.0f)
        
        activeLights = 1
    }

    // ===== Rendering Functions =====
    
    private fun updateMatrices() {
        // Update view matrix
        Matrix.setLookAtM(
            viewMatrix, 0,
            cameraPosition[0], cameraPosition[1], cameraPosition[2],
            cameraTarget[0], cameraTarget[1], cameraTarget[2],
            cameraUp[0], cameraUp[1], cameraUp[2]
        )
        
        // Calculate MVP matrix
        Matrix.multiplyMM(mvpMatrix, 0, projectionMatrix, 0, viewMatrix, 0)
        Matrix.multiplyMM(mvpMatrix, 0, mvpMatrix, 0, modelMatrix, 0)
    }
    
    private fun renderSkybox() {
        // Disable depth writing for skybox
        GLES30.glDepthMask(false)
        
        GLES30.glUseProgram(skyboxProgram)
        
        // Remove translation from view matrix
        val skyboxView = FloatArray(16)
        System.arraycopy(viewMatrix, 0, skyboxView, 0, 16)
        skyboxView[12] = 0.0f
        skyboxView[13] = 0.0f
        skyboxView[14] = 0.0f
        
        val projectionLocation = glUniformLocations["uProjection"]
        val viewLocation = glUniformLocations["uView"]
        val skyboxLocation = glUniformLocations["uSkybox"]
        
        if (projectionLocation != null) {
            GLES30.glUniformMatrix4fv(projectionLocation, 1, false, projectionMatrix, 0)
        }
        if (viewLocation != null) {
            GLES30.glUniformMatrix4fv(viewLocation, 1, false, skyboxView, 0)
        }
        
        GLES30.glBindVertexArray(skyboxVAO)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_CUBE_MAP, skyboxTexture)
        if (skyboxLocation != null) {
            GLES30.glUniform1i(skyboxLocation, 0)
        }
        
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 36)
        
        GLES30.glBindVertexArray(0)
        GLES30.glDepthMask(true)
    }
    
    private fun renderScene() {
        GLES30.glUseProgram(glProgram)
        
        // Set matrices
        val mvpLocation = glUniformLocations["uMVPMatrix"]
        val modelLocation = glUniformLocations["uModelMatrix"]
        val viewLocation = glUniformLocations["uViewMatrix"]
        val projectionLocation = glUniformLocations["uProjectionMatrix"]
        val textureMatrixLocation = glUniformLocations["uTextureMatrix"]
        
        if (mvpLocation != null) {
            GLES30.glUniformMatrix4fv(mvpLocation, 1, false, mvpMatrix, 0)
        }
        if (modelLocation != null) {
            GLES30.glUniformMatrix4fv(modelLocation, 1, false, modelMatrix, 0)
        }
        if (viewLocation != null) {
            GLES30.glUniformMatrix4fv(viewLocation, 1, false, viewMatrix, 0)
        }
        if (projectionLocation != null) {
            GLES30.glUniformMatrix4fv(projectionLocation, 1, false, projectionMatrix, 0)
        }
        if (textureMatrixLocation != null) {
            GLES30.glUniformMatrix4fv(textureMatrixLocation, 1, false, textureMatrix, 0)
        }
        
        // Set camera position
        val viewPosLocation = glUniformLocations["uViewPos"]
        if (viewPosLocation != null) {
            GLES30.glUniform3f(viewPosLocation, 
                cameraPosition[0], cameraPosition[1], cameraPosition[2])
        }
        
        // Set material
        currentMaterial?.let { material ->
            val ambientLocation = glUniformLocations["uMaterialAmbient"]
            val diffuseLocation = glUniformLocations["uMaterialDiffuse"]
            val specularLocation = glUniformLocations["uMaterialSpecular"]
            val shininessLocation = glUniformLocations["uMaterialShininess"]
            val textureLocation = glUniformLocations["uTexture"]
            val useTextureLocation = glUniformLocations["uUseTexture"]
            
            if (ambientLocation != null) {
                GLES30.glUniform4f(ambientLocation,
                    material.ambient[0], material.ambient[1], material.ambient[2], material.ambient[3])
            }
            if (diffuseLocation != null) {
                GLES30.glUniform4f(diffuseLocation,
                    material.diffuse[0], material.diffuse[1], material.diffuse[2], material.diffuse[3])
            }
            if (specularLocation != null) {
                GLES30.glUniform4f(specularLocation,
                    material.specular[0], material.specular[1], material.specular[2], material.specular[3])
            }
            if (shininessLocation != null) {
                GLES30.glUniform1f(shininessLocation, material.shininess)
            }
            
            // Bind texture if available
            if (material.textureId != -1) {
                textures[material.textureId]?.let { texture ->
                    GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
                    GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture.id)
                    if (textureLocation != null) {
                        GLES30.glUniform1i(textureLocation, 0)
                    }
                    if (useTextureLocation != null) {
                        GLES30.glUniform1i(useTextureLocation, 1)
                    }
                }
            } else {
                if (useTextureLocation != null) {
                    GLES30.glUniform1i(useTextureLocation, 0)
                }
            }
        }
        
        // Set lighting
        val lightCountLocation = glUniformLocations["uLightCount"]
        val useLightingLocation = glUniformLocations["uUseLighting"]
        
        if (lightCountLocation != null) {
            GLES30.glUniform1i(lightCountLocation, activeLights)
        }
        if (useLightingLocation != null) {
            GLES30.glUniform1i(useLightingLocation, if (activeLights > 0) 1 else 0)
        }
        
        for (i in 0 until activeLights) {
            val light = lights[i]
            val posLocation = glUniformLocations["uLightPosition[$i]"]
            val dirLocation = glUniformLocations["uLightDirection[$i]"]
            val ambientLocation = glUniformLocations["uLightAmbient[$i]"]
            val diffuseLocation = glUniformLocations["uLightDiffuse[$i]"]
            val specularLocation = glUniformLocations["uLightSpecular[$i]"]
            val constantLocation = glUniformLocations["uLightConstant[$i]"]
            val linearLocation = glUniformLocations["uLightLinear[$i]"]
            val quadraticLocation = glUniformLocations["uLightQuadratic[$i]"]
            
            if (posLocation != null) {
                GLES30.glUniform3f(posLocation,
                    light.position[0], light.position[1], light.position[2])
            }
            if (dirLocation != null) {
                GLES30.glUniform3f(dirLocation,
                    light.direction[0], light.direction[1], light.direction[2])
            }
            if (ambientLocation != null) {
                GLES30.glUniform4f(ambientLocation,
                    light.ambient[0], light.ambient[1], light.ambient[2], light.ambient[3])
            }
            if (diffuseLocation != null) {
                GLES30.glUniform4f(diffuseLocation,
                    light.diffuse[0], light.diffuse[1], light.diffuse[2], light.diffuse[3])
            }
            if (specularLocation != null) {
                GLES30.glUniform4f(specularLocation,
                    light.specular[0], light.specular[1], light.specular[2], light.specular[3])
            }
            if (constantLocation != null) {
                GLES30.glUniform1f(constantLocation, light.constant)
            }
            if (linearLocation != null) {
                GLES30.glUniform1f(linearLocation, light.linear)
            }
            if (quadraticLocation != null) {
                GLES30.glUniform1f(quadraticLocation, light.quadratic)
            }
        }
        
        // Render all meshes
        meshes.values.forEach { mesh ->
            GLES30.glBindVertexArray(mesh.vao)
            GLES30.glDrawElements(GLES30.GL_TRIANGLES, mesh.indexCount, GLES30.GL_UNSIGNED_SHORT, 0)
            GLES30.glBindVertexArray(0)
            
            stats.drawCalls++
            stats.triangles += mesh.indexCount / 3
        }
    }
    
    private fun renderFrameBuffer() {
        // Switch to orthographic projection for 2D rendering
        val orthoMatrix = FloatArray(16)
        Matrix.orthoM(orthoMatrix, 0, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f)
        
        GLES30.glUseProgram(glProgram)
        val mvpLocation = glUniformLocations["uMVPMatrix"]
        if (mvpLocation != null) {
            GLES30.glUniformMatrix4fv(mvpLocation, 1, false, orthoMatrix, 0)
        }
        
        // Bind frame texture
        val textureLocation = glUniformLocations["uTexture"]
        val useTextureLocation = glUniformLocations["uUseTexture"]
        val useLightingLocation = glUniformLocations["uUseLighting"]
        
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glFrameTexture)
        if (textureLocation != null) {
            GLES30.glUniform1i(textureLocation, 0)
        }
        if (useTextureLocation != null) {
            GLES30.glUniform1i(useTextureLocation, 1)
        }
        if (useLightingLocation != null) {
            GLES30.glUniform1i(useLightingLocation, 0)
        }
        
        // Render fullscreen quad
        renderFullscreenQuad()
    }
    
    private fun renderFullscreenQuad() {
        val quadVertices = floatArrayOf(
            -1.0f,  1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f
        )
        
        val vertexBuffer = ByteBuffer.allocateDirect(quadVertices.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(quadVertices)
        vertexBuffer.position(0)
        
        val vaoIds = IntArray(1)
        val vboIds = IntArray(1)
        
        GLES30.glGenVertexArrays(1, vaoIds, 0)
        GLES30.glGenBuffers(1, vboIds, 0)
        
        val quadVAO = vaoIds[0]
        val quadVBO = vboIds[0]
        
        GLES30.glBindVertexArray(quadVAO)
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, quadVBO)
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, quadVertices.size * 4, vertexBuffer, GLES30.GL_STATIC_DRAW)
        
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 4 * 4, 0)
        
        GLES30.glEnableVertexAttribArray(1)
        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT, false, 4 * 4, 2 * 4)
        
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 6)
        
        GLES30.glBindVertexArray(0)
        GLES30.glDeleteVertexArrays(1, vaoIds, 0)
        GLES30.glDeleteBuffers(1, vboIds, 0)
    }

    // ===== Public API =====
    
    fun onFrameRendered(textureId: Int, width: Int, height: Int) {
        // This is called by XboxGPU when it has rendered a frame
        // Copy the texture data to our frame buffer
        updateFrameTexture(textureId, width, height)
    }
    
    private fun updateFrameTexture(textureId: Int, width: Int, height: Int) {
        // Bind the texture
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textureId)
        
        // Read texture data into frame buffer
        frameBuffer.position(0)
        GLES30.glReadPixels(0, 0, width, height, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, frameBuffer)
        
        // Update our frame texture
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, glFrameTexture)
        GLES30.glTexSubImage2D(
            GLES30.GL_TEXTURE_2D, 0,
            0, 0, width, height,
            GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE,
            frameBuffer
        )
    }
    
    fun setDisplayMode(mode: Int) {
        if (mode in RESOLUTIONS.keys) {
            displayMode = mode
            val (width, height) = RESOLUTIONS[mode]!!
            updateProjectionMatrix(width, height)
            Log.d(TAG, "Display mode changed to: ${width}x${height}")
        }
    }
    
    fun setAspectRatio(ratio: Int) {
        if (ratio in ASPECT_4_3..ASPECT_FILL) {
            aspectRatio = ratio
            updateScaling(surfaceView.width, surfaceView.height)
            Log.d(TAG, "Aspect ratio changed to: $ratio")
        }
    }
    
    fun setVsyncMode(mode: Int) {
        vsyncMode = mode
        surfaceView.setRenderMode(
            when (mode) {
                VSYNC_ON -> GLSurfaceView.RENDERMODE_WHEN_DIRTY
                VSYNC_ADAPTIVE -> GLSurfaceView.RENDERMODE_CONTINUOUSLY
                else -> GLSurfaceView.RENDERMODE_CONTINUOUSLY
            }
        )
        Log.d(TAG, "VSync mode changed to: $mode")
    }
    
    fun setFilterType(type: Int) {
        filterType = type
        updateTextureFiltering()
        Log.d(TAG, "Filter type changed to: $type")
    }
    
    fun setBackgroundColor(r: Float, g: Float, b: Float, a: Float) {
        backgroundColor = floatArrayOf(r, g, b, a)
        GLES30.glClearColor(r, g, b, a)
    }
    
    fun createMaterial(): Int {
        val materialId = materials.size
        val material = Material()
        materials[materialId] = material
        return materialId
    }
    
    fun setMaterialTexture(materialId: Int, textureId: Int) {
        materials[materialId]?.textureId = textureId
    }
    
    fun useMaterial(materialId: Int) {
        currentMaterial = materials[materialId]
    }
    
    fun createTexture(width: Int, height: Int, data: ByteBuffer? = null): Int {
        val textureId = ++currentTextureId
        
        val texIds = IntArray(1)
        GLES30.glGenTextures(1, texIds, 0)
        
        val texture = Texture(
            id = texIds[0],
            width = width,
            height = height,
            format = GLES30.GL_RGBA,
            internalFormat = GLES30.GL_RGBA8,
            type = GLES30.GL_UNSIGNED_BYTE
        )
        
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture.id)
        
        // Set default texture parameters
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, texture.wrapS)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, texture.wrapT)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, texture.minFilter)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, texture.magFilter)
        
        // Upload texture data
        if (data != null) {
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D, 0, texture.internalFormat,
                width, height, 0,
                texture.format, texture.type,
                data
            )
            
            if (texture.mipmapLevels > 0) {
                GLES30.glGenerateMipmap(GLES30.GL_TEXTURE_2D)
            }
        } else {
            GLES30.glTexImage2D(
                GLES30.GL_TEXTURE_2D, 0, texture.internalFormat,
                width, height, 0,
                texture.format, texture.type,
                null
            )
        }
        
        textures[textureId] = texture
        return textureId
    }
    
    fun createMesh(vertices: FloatArray, normals: FloatArray, texCoords: FloatArray, indices: ShortArray): Int {
        val meshId = meshes.size
        
        // Create buffers
        val vertexBuffer = ByteBuffer.allocateDirect(vertices.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(vertices)
        vertexBuffer.position(0)
        
        val normalBuffer = ByteBuffer.allocateDirect(normals.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(normals)
        normalBuffer.position(0)
        
        val texCoordBuffer = ByteBuffer.allocateDirect(texCoords.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(texCoords)
        texCoordBuffer.position(0)
        
        val indexBuffer = ByteBuffer.allocateDirect(indices.size * 2)
            .order(ByteOrder.nativeOrder())
            .asShortBuffer()
            .put(indices)
        indexBuffer.position(0)
        
        // Create OpenGL objects
        val vaoIds = IntArray(1)
        val vboIds = IntArray(3)
        val eboIds = IntArray(1)
        
        GLES30.glGenVertexArrays(1, vaoIds, 0)
        GLES30.glGenBuffers(3, vboIds, 0)
        GLES30.glGenBuffers(1, eboIds, 0)
        
        val vao = vaoIds[0]
        val vbo = vboIds
        val ebo = eboIds[0]
        
        GLES30.glBindVertexArray(vao)
        
        // Position attribute
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo[0])
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, vertices.size * 4, vertexBuffer, GLES30.GL_STATIC_DRAW)
        GLES30.glEnableVertexAttribArray(0)
        GLES30.glVertexAttribPointer(0, 3, GLES30.GL_FLOAT, false, 3 * 4, 0)
        
        // Normal attribute
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo[1])
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, normals.size * 4, normalBuffer, GLES30.GL_STATIC_DRAW)
        GLES30.glEnableVertexAttribArray(3)
        GLES30.glVertexAttribPointer(3, 3, GLES30.GL_FLOAT, false, 3 * 4, 0)
        
        // Texture coordinate attribute
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo[2])
        GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, texCoords.size * 4, texCoordBuffer, GLES30.GL_STATIC_DRAW)
        GLES30.glEnableVertexAttribArray(2)
        GLES30.glVertexAttribPointer(2, 2, GLES30.GL_FLOAT, false, 2 * 4, 0)
        
        // Index buffer
        GLES30.glBindBuffer(GLES30.GL_ELEMENT_ARRAY_BUFFER, ebo)
        GLES30.glBufferData(GLES30.GL_ELEMENT_ARRAY_BUFFER, indices.size * 2, indexBuffer, GLES30.GL_STATIC_DRAW)
        
        GLES30.glBindVertexArray(0)
        
        val mesh = Mesh(
            vertices = vertexBuffer,
            normals = normalBuffer,
            texCoords = texCoordBuffer,
            indices = indexBuffer,
            vertexCount = vertices.size / 3,
            indexCount = indices.size,
            vao = vao,
            vbo = vbo,
            ebo = ebo
        )
        
        meshes[meshId] = mesh
        return meshId
    }

    // ===== Helper Functions =====
    
    private fun updateProjectionMatrix(width: Int, height: Int) {
        val aspect = width.toFloat() / height.toFloat()
        Matrix.perspectiveM(projectionMatrix, 0, 45.0f, aspect, 0.1f, 100.0f)
    }
    
    private fun updateScaling(width: Int, height: Int) {
        // Update viewport based on aspect ratio
        when (aspectRatio) {
            ASPECT_4_3 -> {
                val targetAspect = 4.0f / 3.0f
                val currentAspect = width.toFloat() / height.toFloat()
                
                if (currentAspect > targetAspect) {
                    // Wider than 4:3
                    val w = (height * targetAspect).toInt()
                    val x = (width - w) / 2
                    viewport.x = x
                    viewport.width = w
                    viewport.height = height
                } else {
                    // Taller than 4:3
                    val h = (width / targetAspect).toInt()
                    val y = (height - h) / 2
                    viewport.y = y
                    viewport.width = width
                    viewport.height = h
                }
            }
            ASPECT_16_9 -> {
                val targetAspect = 16.0f / 9.0f
                val currentAspect = width.toFloat() / height.toFloat()
                
                if (currentAspect > targetAspect) {
                    val w = (height * targetAspect).toInt()
                    val x = (width - w) / 2
                    viewport.x = x
                    viewport.width = w
                    viewport.height = height
                } else {
                    val h = (width / targetAspect).toInt()
                    val y = (height - h) / 2
                    viewport.y = y
                    viewport.width = width
                    viewport.height = h
                }
            }
            ASPECT_STRETCH -> {
                viewport.x = 0
                viewport.y = 0
                viewport.width = width
                viewport.height = height
            }
            ASPECT_FILL -> {
                // Maintain aspect ratio while filling screen
                val frameWidth = RESOLUTIONS[displayMode]?.first ?: 640
                val frameHeight = RESOLUTIONS[displayMode]?.second ?: 480
                val frameAspect = frameWidth.toFloat() / frameHeight
                val screenAspect = width.toFloat() / height
                
                if (screenAspect > frameAspect) {
                    // Screen is wider
                    val scaledHeight = (width / frameAspect).toInt()
                    viewport.y = -(scaledHeight - height) / 2
                    viewport.height = scaledHeight
                    viewport.width = width
                } else {
                    // Screen is taller
                    val scaledWidth = (height * frameAspect).toInt()
                    viewport.x = -(scaledWidth - width) / 2
                    viewport.width = scaledWidth
                    viewport.height = height
                }
            }
        }
        
        GLES30.glViewport(viewport.x, viewport.y, viewport.width, viewport.height)
    }
    
    private fun updateTextureFiltering() {
        textures.values.forEach { texture ->
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, texture.id)
            
            when (filterType) {
                FILTER_NEAREST -> {
                    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
                    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
                }
                FILTER_BILINEAR -> {
                    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
                    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
                }
                FILTER_TRILINEAR -> {
                    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR_MIPMAP_LINEAR)
                    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
                }
            }
        }
    }
    
    private fun updateFrameStats() {
        val currentTime = System.nanoTime()
        val frameTime = (currentTime - lastFrameTime).toDouble()
        
        stats.frameTime = frameTime / 1_000_000.0 // Convert to milliseconds
        stats.frames++
        
        frameCounter++
        fpsTimer += frameTime.toLong()
        
        if (fpsTimer >= 1_000_000_000L) {
            fps = frameCounter * 1_000_000_000.0 / fpsTimer
            frameCounter = 0
            fpsTimer = 0
        }
        
        lastFrameTime = currentTime
    }
    
    private fun renderDebugOverlay() {
        // Note: Rendering text in OpenGL ES requires a text rendering system
        // This is a placeholder - you would need to implement text rendering
        // using texture atlases or other methods
        
        if (showDebugOverlay) {
            // Render debug info (would need text rendering implementation)
        }
        
        if (showStats) {
            // Render stats overlay (would need text rendering implementation)
        }
    }
    
    fun toggleDebugOverlay() {
        showDebugOverlay = !showDebugOverlay
    }
    
    fun toggleWireframe() {
        showWireframe = !showWireframe
        // ملاحظة: GLES30.glPolygonMode غير موجود في OpenGL ES
        // بدلاً من ذلك، يمكن استخدام GLES30.glLineWidth وتغيير طريقة الرسم
        if (showWireframe) {
            GLES30.glLineWidth(1.0f)
            renderState.fillMode = GLES30.GL_LINE
        } else {
            renderState.fillMode = GLES30.GL_FILL
        }
    }
    
    fun toggleStats() {
        showStats = !showStats
    }
    
    fun getStats(): RenderStats {
        stats.fps = fps
        return stats.copy()
    }
    
    fun getFPS(): Double = fps
    
    fun isInitialized(): Boolean = glInitialized
}