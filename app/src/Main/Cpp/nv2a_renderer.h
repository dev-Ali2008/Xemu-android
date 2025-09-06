#ifndef NV2A_RENDERER_H
#define NV2A_RENDERER_H

#include <cstdint>
#include <vector>
#include <array>
#include <functional>
#include <arm_neon.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <android/native_window.h>
#include <map>

class XboxMemory;

class NV2ARenderer {
public:
    static constexpr uint32_t FB_WIDTH = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;
    static constexpr uint32_t FB_SIZE = FB_WIDTH * FB_HEIGHT;
    static constexpr uint32_t TEXTURE_MEMORY = 128 * 1024 * 1024;
    static constexpr uint32_t MAX_VERTICES = 65536;
    static constexpr uint32_t MAX_COMMANDS = 16384;

  enum NV2ARegisters {

        NV_PMC_ENABLE = 0x00000200,
        NV_PMC_BOOT_0 = 0x00000000,
        NV_PMC_BOOT_1 = 0x00000004,


        NV_PGRAPH_CTX_CONTROL = 0x00400000,
        NV_PGRAPH_CTX_USER = 0x00400004,
        NV_PGRAPH_CTX_SWITCH1 = 0x00400008,
        NV_PGRAPH_CTX_SWITCH2 = 0x0040000C,
        NV_PGRAPH_CTX_SWITCH3 = 0x00400010,
        NV_PGRAPH_CTX_SWITCH4 = 0x00400014,
        NV_PGRAPH_CTX_SWITCH5 = 0x00400018,
        NV_PGRAPH_CTX_SWITCH6 = 0x0040001C,
        NV_PGRAPH_CTX_SWITCH7 = 0x00400020,


        NV_PGRAPH_VS_START = 0x00400100,
        NV_PGRAPH_VS_END = 0x00400104,
        NV_PGRAPH_VS_REG = 0x00400108,
        NV_PGRAPH_VS_CONST = 0x0040010C,


        NV_PGRAPH_PS_START = 0x00400200,
        NV_PGRAPH_PS_END = 0x00400204,
        NV_PGRAPH_PS_REG = 0x00400208,
        NV_PGRAPH_PS_CONST = 0x0040020C,


        NV_PGRAPH_TEXCTRL0 = 0x00400300,
        NV_PGRAPH_TEXCTRL1 = 0x00400304,
        NV_PGRAPH_TEXCTRL2 = 0x00400308,
        NV_PGRAPH_TEXCTRL3 = 0x0040030C,
        NV_PGRAPH_TEXFMT0 = 0x00400310,
        NV_PGRAPH_TEXFMT1 = 0x00400314,
        NV_PGRAPH_TEXFMT2 = 0x00400318,
        NV_PGRAPH_TEXFMT3 = 0x0040031C,


        NV_PGRAPH_ALPHAFUNC = 0x00400400,
        NV_PGRAPH_ALPHAREF = 0x00400404,
        NV_PGRAPH_BLEND = 0x00400408,
        NV_PGRAPH_BLENDCOLOR = 0x0040040C,
        NV_PGRAPH_DEPTHFUNC = 0x00400410,
        NV_PGRAPH_DEPTHRANGE = 0x00400414,
        NV_PGRAPH_DEPTHWRITE = 0x00400418,
        NV_PGRAPH_FOGENABLE = 0x0040041C,
        NV_PGRAPH_FOGCOLOR = 0x00400420,
        NV_PGRAPH_FOGCOEF0 = 0x00400424,
        NV_PGRAPH_FOGCOEF1 = 0x00400428,
        NV_PGRAPH_FOGCOEF2 = 0x0040042C,
        NV_PGRAPH_FOGCOEF3 = 0x00400430,


        NV_PGRAPH_XFMODE0 = 0x00400500,
        NV_PGRAPH_XFMODE1 = 0x00400504,
        NV_PGRAPH_XFMODE2 = 0x00400508,
        NV_PGRAPH_XFMODE3 = 0x0040050C,
        NV_PGRAPH_XFMODE4 = 0x00400510,
        NV_PGRAPH_XFMODE5 = 0x00400514,
        NV_PGRAPH_XFMODE6 = 0x00400518,
        NV_PGRAPH_XFMODE7 = 0x0040051C,


        NV_PGRAPH_VIEWPORT = 0x00400600,
        NV_PGRAPH_VIEWPORT_CLIP = 0x00400604,
        NV_PGRAPH_VIEWPORT_OFFSET = 0x00400608,
        NV_PGRAPH_VIEWPORT_SCALE = 0x0040060C,
        NV_PGRAPH_VIEWPORT_DIM = 0x00400610,
        NV_PGRAPH_VIEWPORT_HORIZ = 0x00400614,
        NV_PGRAPH_VIEWPORT_VERT = 0x00400618,


        NV_PGRAPH_SCISSOR = 0x00400700,
        NV_PGRAPH_SCISSOR_CLIP = 0x00400704,


        NV_PGRAPH_STENCIL = 0x00400800,
        NV_PGRAPH_STENCIL_FUNC = 0x00400804,
        NV_PGRAPH_STENCIL_REF = 0x00400808,
        NV_PGRAPH_STENCIL_MASK = 0x0040080C,
        NV_PGRAPH_STENCIL_OP = 0x00400810,


        NV_PGRAPH_COLOR = 0x00400900,
        NV_PGRAPH_COLOR_MASK = 0x00400904,
        NV_PGRAPH_COLOR_LOGIC = 0x00400908,


        NV_PGRAPH_FIFO = 0x00400A00,
        NV_PGRAPH_FIFO_PUT = 0x00400A04,
        NV_PGRAPH_FIFO_GET = 0x00400A08,
        NV_PGRAPH_FIFO_REF = 0x00400A0C,
        NV_PGRAPH_FIFO_STAT = 0x00400A10,


        NV_PGRAPH_DMA = 0x00400B00,
        NV_PGRAPH_DMA_PUT = 0x00400B04,
        NV_PGRAPH_DMA_GET = 0x00400B08,
        NV_PGRAPH_DMA_REF = 0x00400B0C,
        NV_PGRAPH_DMA_STAT = 0x00400B10,


        NV_PGRAPH_INTR = 0x00400C00,
        NV_PGRAPH_INTR_EN = 0x00400C04,
        NV_PGRAPH_INTR_STAT = 0x00400C08,


        NV_PGRAPH_STATUS = 0x00400D00,
        NV_PGRAPH_DEBUG = 0x00400D04,
        NV_PGRAPH_DEBUG1 = 0x00400D08,
        NV_PGRAPH_DEBUG2 = 0x00400D0C,
        NV_PGRAPH_DEBUG3 = 0x00400D10
    };


    enum TextureFormat {
        TEX_FORMAT_A8R8G8B8 = 0x00,
        TEX_FORMAT_R5G6B5 = 0x01,
        TEX_FORMAT_A1R5G5B5 = 0x02,
        TEX_FORMAT_A4R4G4B4 = 0x03,
        TEX_FORMAT_DXT1 = 0x04,
        TEX_FORMAT_DXT3 = 0x05,
        TEX_FORMAT_DXT5 = 0x06,
        TEX_FORMAT_L8 = 0x07,
        TEX_FORMAT_A8L8 = 0x08,
        TEX_FORMAT_V8U8 = 0x09,
        TEX_FORMAT_Q8W8V8U8 = 0x0A,
        TEX_FORMAT_D24S8 = 0x0B
    };


    enum BlendMode {
        BLEND_ZERO = 0x00,
        BLEND_ONE = 0x01,
        BLEND_SRC_COLOR = 0x02,
        BLEND_INV_SRC_COLOR = 0x03,
        BLEND_SRC_ALPHA = 0x04,
        BLEND_INV_SRC_ALPHA = 0x05,
        BLEND_DEST_ALPHA = 0x06,
        BLEND_INV_DEST_ALPHA = 0x07,
        BLEND_DEST_COLOR = 0x08,
        BLEND_INV_DEST_COLOR = 0x09,
        BLEND_SRC_ALPHA_SAT = 0x0A,
        BLEND_BOTH_SRC_ALPHA = 0x0B,
        BLEND_BOTH_INV_SRC_ALPHA = 0x0C
    };


    enum CompareFunc {
        CMP_NEVER = 0x00,
        CMP_LESS = 0x01,
        CMP_EQUAL = 0x02,
        CMP_LESS_EQUAL = 0x03,
        CMP_GREATER = 0x04,
        CMP_NOT_EQUAL = 0x05,
        CMP_GREATER_EQUAL = 0x06,
        CMP_ALWAYS = 0x07
    };

    NV2ARenderer(XboxMemory* memory);
    NV2ARenderer();
    ~NV2ARenderer();

    void reset();
    void renderFrame();
    void processDMA();

    uint32_t readRegister(uint32_t addr);
    void writeRegister(uint32_t addr, uint32_t value);

    const uint32_t* getFramebuffer() const;
    bool hasVertexData() const;
    uint32_t getFramebufferWidth() const { return FB_WIDTH; }
    uint32_t getFramebufferHeight() const { return FB_HEIGHT; }
    uint32_t getWidth() const;
    uint32_t getHeight() const;

    void setDebugCallback(std::function<void(const std::string&)> callback);

    enum class GpuState {
        Ready,
        Processing,
        Error,
        Idle
    };

    enum class RendererType {
        Vulkan,
        OpenGL
    };

    GpuState getState() const;
    bool isRunning() const { return !shouldStop && currentState != GpuState::Error; }

    void setOutputResolution(uint32_t width, uint32_t height);
    void enableDepthTest(bool enable);
    void enableAlphaBlending(bool enable);
    void setClipRect(int left, int top, int right, int bottom);
    void enableVSync(bool enabled) { vsyncEnabled = enabled; }
    bool checkInterrupt() const { return interruptPending; }


    uint32_t getCommandBufferPC() const { return cmdState.pc; }
    uint32_t getCommandBufferPUT() const { return cmdState.put; }
    bool isCommandBufferEmpty() const { return cmdState.fifoEmpty; }


    void setRendererType(RendererType type);
    RendererType getRendererType() const { return rendererType; }
    bool isVulkanRenderer() const { return rendererType == RendererType::Vulkan; }
    bool isOpenGLRenderer() const { return rendererType == RendererType::OpenGL; }


    bool setSurface(ANativeWindow* window);
    void releaseSurface(); 


    bool hasAudioOutput() const { return true; }
    const uint32_t* getAudioBuffer() const;

    uint32_t getOutputWidth() const { return outputWidth; }
    uint32_t getOutputHeight() const { return outputHeight; }

    void stop() {
        shouldStop = true;
        {
            std::lock_guard<std::mutex> lock(renderMutex);
            renderCond.notify_all();
        }
    }


    void setTextureFormat(uint32_t unit, TextureFormat format);
    void setBlendMode(BlendMode srcBlend, BlendMode destBlend);
    void setDepthFunc(CompareFunc func);
    void setAlphaFunc(CompareFunc func, uint8_t ref);
    void setFogEnable(bool enable);
    void setFogColor(uint32_t color);
    void setFogCoeffs(float start, float end, float density);
    void setViewport(float x, float y, float width, float height, float minZ, float maxZ);
    void setScissor(int x, int y, int width, int height);
    void setStencilFunc(CompareFunc func, uint8_t ref, uint8_t mask);
    void setStencilOp(uint8_t fail, uint8_t zfail, uint8_t pass);
    void setColorMask(bool red, bool green, bool blue, bool alpha);
    void setLogicOp(uint8_t op);


    void setupDefaultState();
    void renderBasicFrame();

public:
    struct Vertex {
        float x, y, z;
        float u, v;
        uint32_t color;
        float fog;
        float nx, ny, nz; 


        Vertex() : x(0.0f), y(0.0f), z(0.0f), u(0.0f), v(0.0f), color(0), fog(0.0f), nx(0.0f), ny(0.0f), nz(1.0f) {}


        Vertex(float x, float y, float z, float u, float v, uint32_t color, float fog) 
            : x(x), y(y), z(z), u(u), v(v), color(color), fog(fog), nx(0.0f), ny(0.0f), nz(1.0f) {}


        Vertex(const Vertex& other) = default;


        Vertex(Vertex&& other) noexcept = default;


        Vertex& operator=(const Vertex& other) = default;


        Vertex& operator=(Vertex&& other) noexcept = default;


        void swap(Vertex& other) noexcept {
            std::swap(x, other.x);
            std::swap(y, other.y);
            std::swap(z, other.z);
            std::swap(u, other.u);
            std::swap(v, other.v);
            std::swap(color, other.color);
            std::swap(fog, other.fog);
            std::swap(nx, other.nx);
            std::swap(ny, other.ny);
            std::swap(nz, other.nz);
        }
    };

private:
    struct TextureInfo {
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t address;
        uint32_t pitch;
        uint32_t mipLevels;
        bool swizzled;
        TextureFormat xboxFormat;


        bool filtering;
        bool anisotropic;
        bool mipmapFiltering;
        uint32_t addressModeU;
        uint32_t addressModeV;
        uint32_t borderColor;
        uint32_t coordGen;
        uint32_t matrixMode;
    };

    enum class PrimitiveType {
        Points,
        Lines,
        LineStrip,
        Triangles,
        TriangleStrip,
        TriangleFan,
        Quads,
        QuadStrip,
        Polygon
    };


    struct RenderState {

        BlendMode srcBlend = BLEND_ONE;
        BlendMode destBlend = BLEND_ZERO;
        uint32_t blendColor = 0xFFFFFFFF;


        CompareFunc depthFunc = CMP_LESS;
        CompareFunc alphaFunc = CMP_ALWAYS;
        uint8_t alphaRef = 0;
        bool depthWrite = true;
        bool depthTest = true;
        bool alphaTest = false;


        bool stencilTest = false;
        CompareFunc stencilFunc = CMP_ALWAYS;
        uint8_t stencilRef = 0;
        uint8_t stencilMask = 0xFF;
        uint8_t stencilFail = 0;
        uint8_t stencilZFail = 0;
        uint8_t stencilPass = 0;


        bool fogEnable = false;
        uint32_t fogColor = 0;
        float fogStart = 0.0f;
        float fogEnd = 1.0f;
        float fogDensity = 1.0f;
        uint32_t fogMode = 0; 


        bool colorMaskRed = true;
        bool colorMaskGreen = true;
        bool colorMaskBlue = true;
        bool colorMaskAlpha = true;
        uint8_t logicOp = 0;


        float viewportX = 0.0f;
        float viewportY = 0.0f;
        float viewportWidth = 1.0f;
        float viewportHeight = 1.0f;
        float viewportMinZ = 0.0f;
        float viewportMaxZ = 1.0f;
        int scissorX = 0;
        int scissorY = 0;
        int scissorWidth = FB_WIDTH;
        int scissorHeight = FB_HEIGHT;


        bool lightingEnabled = false;
        bool postProcessingEnabled = false;
        bool blurEnabled = false;
        bool colorCorrectionEnabled = false;
        bool debugOverlayEnabled = false;
        bool backfaceCullingEnabled = false;
        bool textureCachingEnabled = false;
        bool particleEffectsEnabled = false;
        bool screenSpaceEffectsEnabled = false;


        float lightDirection[3] = {0.0f, 0.0f, 1.0f};
        float lightColor[3] = {1.0f, 1.0f, 1.0f};
        float ambientLight[3] = {0.2f, 0.2f, 0.2f};


        float viewMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };


        float projectionMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };


        float modelMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };


        uint32_t currentTextureUnit = 0;
        bool textureFilteringEnabled = true;
        bool textureSwizzlingEnabled = false;
        float anisotropicFiltering = 1.0f;


        bool vsyncEnabled = true;
        uint32_t maxAnisotropy = 16;
        bool enableMultisampling = false;
        uint32_t multisampleCount = 1;
    };


    struct VertexInputAssembly {
        bool active = false;
        uint32_t maxVertices = 0;
        uint32_t vertexSize = 0;
    };

    struct VertexTransformation {
        bool active = false;
        std::array<float, 16> modelMatrix;
        std::array<float, 16> viewMatrix;
        std::array<float, 16> projectionMatrix;
    };

    struct VertexLighting {
        bool active = false;
        std::array<float, 4> ambientLight;
        std::array<float, 4> diffuseLight;
        std::array<float, 4> specularLight;
    };

    struct VertexClipping {
        bool active = false;
        uint32_t clipPlanes = 6;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

    struct VertexCulling {
        bool active = false;
        uint32_t cullMode = 1;
        uint32_t frontFace = 0;
    };

    struct FragmentGeneration {
        bool active = false;
        uint32_t maxFragments = 0;
        uint32_t fragmentSize = 0;
    };

    struct FragmentShading {
        bool active = false;
        uint32_t shaderProgram = 0;
        std::vector<float> uniforms;
    };

    struct FragmentTexturing {
        bool active = false;
        uint32_t maxTextureUnits = 8;
        bool textureFiltering = true;
    };

    struct FragmentLighting {
        bool active = false;
        uint32_t lightingModel = 1;
        uint32_t maxLights = 8;
        std::array<float, 4> ambientLight;
        std::array<float, 4> diffuseLight;
        std::array<float, 4> specularLight;
    };


    struct PerformanceCounters {
        uint32_t vertexCount = 0;
        uint32_t fragmentCount = 0;
        uint32_t textureCount = 0;
        uint32_t drawCallCount = 0;
        uint32_t frameCount = 0;
        float fps = 0.0f;
        float vertexPerSecond = 0.0f;
        float fragmentPerSecond = 0.0f;
        float texturePerSecond = 0.0f;
        float drawCallPerSecond = 0.0f;
        std::chrono::high_resolution_clock::time_point startTime;


        uint32_t framesRendered = 0;
        uint32_t trianglesRendered = 0;
        uint32_t verticesProcessed = 0;
        uint32_t texturesLoaded = 0;
        uint32_t commandsProcessed = 0;
        uint32_t memoryUsage = 0;
        float gpuTime = 0.0f;
        float cpuTime = 0.0f;
        float frameTime = 0.0f;
        float currentQuality = 1.0f;
    };


    enum class GPUError {
        None,
        OutOfMemory,
        InvalidShader,
        TextureNotFound,
        BufferOverflow,
        InvalidState,
        HardwareError,
        DriverError,
        UnknownError
    };


    struct SavedGPUState {
        std::array<uint32_t, 0x10000> registers;
        std::vector<TextureInfo> textureStates;
        std::map<uint32_t, std::vector<uint32_t>> shaderStates;
        RenderState renderState;
        std::vector<Vertex> vertexBufferState;
        std::vector<uint32_t> framebufferState;
    };

    struct FragmentEffects {
        bool active = false;
        bool fogEnabled = true;
        bool alphaTestEnabled = true;
        bool stencilTestEnabled = true;
    };

    struct TriangleSetup {
        bool active = false;
        uint32_t maxTriangles = 0;
        uint32_t edgeBufferSize = 0;
    };

    struct Edge {
        float x, dx;
        int y, yEnd;
    };

    struct EdgeWalking {
        bool active = false;
        std::vector<Edge> edgeBuffer;
        uint32_t activeEdges = 0;
    };

    struct Scanline {
        int x, y;
        float z;
        uint32_t color;
    };

    struct ScanConversion {
        bool active = false;
        std::vector<Scanline> scanlineBuffer;
        uint32_t currentScanline = 0;
    };

    struct CoverageTesting {
        bool active = false;
        std::vector<uint8_t> coverageBuffer;
        float coverageThreshold = 0.5f;
    };

    struct DepthTesting {
        bool active = false;
        std::vector<float> depthBuffer;
        CompareFunc depthFunc = CMP_LESS;
        bool depthMask = true;
        float depthBias = 0.0f;
        float depthSlope = 0.0f;
    };

    struct AlphaBlending {
        bool active = false;
        BlendMode srcBlend = BLEND_SRC_ALPHA;
        BlendMode destBlend = BLEND_INV_SRC_ALPHA;
        uint32_t blendColor = 0xFFFFFFFF;
        bool separateAlphaBlend = false;
    };

    struct LogicOperations {
        bool active = false;
        uint8_t logicOp = 0;
        bool enabled = false;
        uint32_t mask = 0xFFFFFFFF;
    };

    struct StencilOperations {
        bool active = false;
        std::vector<uint8_t> stencilBuffer;
        CompareFunc stencilFunc = CMP_ALWAYS;
        uint8_t stencilRef = 0;
        uint8_t stencilMask = 0xFF;
        uint8_t stencilFail = 0;
        uint8_t stencilZFail = 0;
        uint8_t stencilPass = 0;
    };

    struct ColorMasking {
        bool active = false;
        bool redMask = true;
        bool greenMask = true;
        bool blueMask = true;
        bool alphaMask = true;
    };

    struct FrameBufferBlending {
        bool active = false;
        std::vector<uint32_t> framebuffer;
        std::vector<uint32_t> backBuffer;
        bool doubleBuffering = true;
    };

    struct FrameBufferOutput {
        bool active = false;
        uint32_t* outputBuffer = nullptr;
        uint32_t outputSize = 0;
    };

    struct DisplayOutput {
        bool active = false;
        std::vector<uint32_t> displayBuffer;
        bool vsyncEnabled = true;
    };

    struct VideoOutput {
        bool active = false;
        std::vector<uint8_t> videoBuffer;
        uint32_t videoFormat = 0;
    };

    struct ScreenshotOutput {
        bool active = false;
        std::vector<uint8_t> screenshotBuffer;
        uint32_t screenshotFormat = 1;
    };

    struct DebugOutput {
        bool active = false;
        std::vector<uint8_t> debugBuffer;
        uint32_t debugLevel = 1;
    };

    struct StreamingPipeline {
        bool active = false;
        uint32_t maxBandwidth = 0;
        uint32_t currentBandwidth = 0;
    };

    struct PrefetchSystem {
        bool active = false;
        uint32_t prefetchDistance = 3;
        uint32_t maxPrefetchTextures = 16;
    };

    struct QualityScaling {
        bool active = false;
        float minQuality = 0.5f;
        float maxQuality = 1.0f;
        float currentQuality = 1.0f;
    };

    struct TextureMemoryManager {
        uint32_t totalMemory = TEXTURE_MEMORY;
        uint32_t usedMemory = 0;
        uint32_t fragmentedMemory = 0;
    };

private:

    std::map<uint32_t, TextureInfo> textureCache;
    std::map<uint32_t, TextureInfo> streamingCache;
    std::map<uint32_t, TextureInfo> compressionCache;
    std::map<uint32_t, TextureInfo> formatConversionCache;


    std::map<uint32_t, std::vector<uint32_t>> shaderPrograms;
    PerformanceCounters performanceCounters;
    SavedGPUState savedState;
    std::array<uint32_t, 0x10000> currentRegisters;
    std::vector<TextureInfo> textureStates;
    std::map<uint32_t, std::vector<uint32_t>> shaderStates;
    std::vector<Vertex> vertexBufferState;
    std::vector<uint32_t> framebufferState;

    std::vector<uint32_t> framebuffer;
    std::vector<uint8_t> textureMemory;
    std::vector<Vertex> vertexBuffer;
    std::vector<float> depthBuffer;

    std::array<uint32_t, 0x10000> registers;
    std::array<TextureInfo, 32> textureUnits;
    RenderState renderState;


    VertexInputAssembly vertexInputAssembly;
    VertexTransformation vertexTransformation;
    VertexLighting vertexLighting;
    VertexClipping vertexClipping;
    VertexCulling vertexCulling;

    FragmentGeneration fragmentGeneration;
    FragmentShading fragmentShading;
    FragmentTexturing fragmentTexturing;
    FragmentLighting fragmentLighting;
    FragmentEffects fragmentEffects;

    TriangleSetup triangleSetup;
    EdgeWalking edgeWalking;
    ScanConversion scanConversion;
    CoverageTesting coverageTesting;
    DepthTesting depthTesting;

    AlphaBlending alphaBlending;
    LogicOperations logicOperations;
    StencilOperations stencilOperations;
    ColorMasking colorMasking;
    FrameBufferBlending frameBufferBlending;

    FrameBufferOutput frameBufferOutput;
    DisplayOutput displayOutput;
    VideoOutput videoOutput;
    ScreenshotOutput screenshotOutput;
    DebugOutput debugOutput;

    StreamingPipeline streamingPipeline;
    PrefetchSystem prefetchSystem;
    QualityScaling qualityScaling;
    TextureMemoryManager textureMemoryManager;

    struct {
        uint32_t source;
        uint32_t dest;
        uint32_t size;
        bool active;
    } dmaState;

    struct {
        uint32_t pc;
        uint32_t put;
        uint32_t get;
        bool fifoEmpty;
    } cmdState;

    struct {
        int left, top, right, bottom;
    } clipRect;

    GpuState currentState;


    uint32_t currentColor = 0xFFFFFFFF; 
    PrimitiveType currentPrimitive;
    uint32_t currentTexture;
    uint32_t currentVertexFormat = 0; 
    uint32_t currentShaderId = 0; 
    bool depthTestEnabled;
    bool alphaBlendEnabled;
    bool textureFilteringEnabled;
    bool textureSwizzlingEnabled;
    float anisotropicFiltering;
    uint32_t frameCounter;
    bool vsyncEnabled;
    bool interruptPending = false;
    std::atomic<bool> shouldStop{false};

    uint32_t outputWidth = FB_WIDTH;
    uint32_t outputHeight = FB_HEIGHT;

    RendererType rendererType = RendererType::Vulkan; 


    ANativeWindow* nativeWindow = nullptr;

    XboxMemory* memory;
    std::thread* renderThread;
    std::mutex renderMutex;
    std::condition_variable renderCond;
    std::chrono::high_resolution_clock::time_point lastFrameTime;

    std::function<void(const std::string&)> debugCallback;


    uint32_t currentRenderTarget = 0;
    uint32_t featureFlags = 0;


    float loadingProgress = 0.0f;
    std::string loadingText = "Loading Xbox Emulator...";

    void clearFramebuffer(uint32_t color);
    void processVertices();
    void renderThreadFunc();

    void handlePrimitive(uint32_t command);
    void handleTextureUpload(uint32_t command);
    void handleVertexData(uint32_t command);
    void handleRegisterWrite(uint32_t reg, uint32_t value);
    void handleSpecialCommand(uint32_t command);
    void handleNV2ACommand(uint32_t command);


    void renderLines();
    void renderPoints();
    void clearFramebuffer();
    void presentFrame();
    void syncGPUState();
    void flushCommandBuffer();
    void setRenderTarget(uint32_t target);
    void setFeatureFlags(uint32_t flags);
    void triggerInterrupt();
    void drawLine(int x1, int y1, int x2, int y2, uint32_t color);

    void processPoints();
    void processLines();
    void processLineStrip();
    void processTriangles();
    void processTriangleStrip();
    void processTriangleFan();
    void processQuads();
    void processQuadStrip();
    void processPolygon();

    void drawPoint(const Vertex& v);
    void drawLine(const Vertex& v0, const Vertex& v1);
    void drawTriangle(const Vertex& v0, const Vertex& v1, const Vertex& v2);
    void drawQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2, const Vertex& v3);

    void drawTriangleNEON(const Vertex& v0, const Vertex& v1, const Vertex& v2);
    void drawLineNEON(const Vertex& v0, const Vertex& v1);

    uint32_t sampleTexture(float u, float v, uint32_t texUnit);
    uint32_t sampleTextureNearest(float x, float y, const TextureInfo& tex);
    uint32_t sampleTextureBilinear(float x, float y, const TextureInfo& tex);
    uint32_t blendColors(uint32_t color1, uint32_t color2);
    uint32_t blendPixels(uint32_t src, uint32_t dst);
    uint32_t bilinearInterpolate(uint32_t c00, uint32_t c01, uint32_t c10, uint32_t c11, float fx, float fy);

    bool depthTest(int x, int y, float depth);
    bool alphaTest(uint8_t alpha);
    bool stencilTest(uint8_t stencil);

    void logDebug(const std::string& message);
    void updateDMA();
    void checkFifoStatus();

    void uploadTexture(uint32_t dest, const uint8_t* src, uint32_t size);
    void downloadTexture(uint8_t* dest, uint32_t src, uint32_t size);

    void swizzleTexture(uint8_t* dest, const uint8_t* src, uint32_t width, uint32_t height);
    void deswizzleTexture(uint8_t* dest, const uint8_t* src, uint32_t width, uint32_t height);


    uint32_t decodeTextureFormat(const TextureInfo& tex, uint32_t x, uint32_t y);
    uint32_t decodeDXT1Block(const TextureInfo& tex, uint32_t x, uint32_t y);
    uint32_t decodeDXT3Block(const TextureInfo& tex, uint32_t x, uint32_t y);
    uint32_t decodeDXT5Block(const TextureInfo& tex, uint32_t x, uint32_t y);
    void decodeDXT1(uint8_t* dest, const uint8_t* src, uint32_t width, uint32_t height);
    void decodeDXT3(uint8_t* dest, const uint8_t* src, uint32_t width, uint32_t height);
    void decodeDXT5(uint8_t* dest, const uint8_t* src, uint32_t width, uint32_t height);


    uint32_t applyBlending(uint32_t src, uint32_t dst);
    uint32_t applyLogicOp(uint32_t src, uint32_t dst);


    uint32_t blendColors(uint32_t color1, uint32_t color2, float factor);


    void processVertexShader(const Vertex& input, Vertex& output);
    void processPixelShader(uint32_t x, uint32_t y, const Vertex& v0, const Vertex& v1, const Vertex& v2);
    void executeXboxShaderProgram(uint32_t programId, const std::vector<float>& constants);
    void setupXboxRenderingPipeline();
    void optimizeCommandBuffer();
    void synchronizeGPU();
    void processHardwareTransform();
    void applyXboxLighting(const Vertex& v);


    void optimizeMemoryBandwidth();
    void setupGPUCache();
    void enableHardwareAcceleration();


    void processXboxTextureEffects();
    void applyXboxFogEffects(Vertex& v);
    void processXboxStencilOperations();
    void executeXboxLogicOperations();


    void finalizeGPUOptimization();
    void enableAllXboxFeatures();
    void setupGameSpecificOptimizations();
    void applyFinalPerformanceTuning();
    void setupAdvancedShaderEffects();
    void validateGPUCompatibility();
    void setupXboxGameCompatibility();
    void setupHaloCompatibility();
    void setupFableCompatibility();
    void setupPGCompatibility();
    void setupGenericXboxCompatibility();


    void setupXboxVertexShaders();
    void setupXboxPixelShaders();
    void setupAdvancedLightingShaders();
    void setupPostProcessingShaders();
    void setupParticleSystemShaders();
    void setupEnvironmentMappingShaders();
    void setupShadowMappingShaders();


    void setupCompleteTextureManagement();
    void setupAdvancedTextureSwizzling();
    void setupCompleteMipmapGeneration();
    void setupAdvancedTextureCaching();
    void setupTextureCompressionSupport();
    void setupTextureStreaming();


    void swizzleTextureARGB(TextureInfo* tex);
    void swizzleTextureRGB565(TextureInfo* tex);
    void swizzleTextureDXT1(TextureInfo* tex);
    void swizzleTextureDXT3(TextureInfo* tex);
    void swizzleTextureDXT5(TextureInfo* tex);
    void swizzleTextureGeneric(TextureInfo* tex);


    void generateMipmapChain(TextureInfo* tex);
    void applyAnisotropicFiltering(TextureInfo* tex);
    void optimizeMipmapCache(TextureInfo* tex);


    void setupTextureCache();
    void setupStreamingCache();
    void setupCompressionCache();
    void setupFormatConversionCache();


    void setupDXT1Compression();
    void setupDXT3Compression();
    void setupDXT5Compression();
    void setupXboxCompressionFormats();


    void setupStreamingPipeline();
    void setupTexturePrefetching();
    void setupQualityScaling();
    void setupTextureMemoryManagement();


    void setupCompleteRenderingPipeline();
    void setupCompleteVertexPipeline();
    void setupCompleteFragmentPipeline();
    void setupCompleteRasterizationPipeline();
    void setupCompleteBlendingPipeline();
    void setupCompleteOutputPipeline();


    void setupVertexInputAssembly();
    void setupVertexTransformation();
    void setupVertexLighting();
    void setupVertexClipping();
    void setupVertexCulling();


    void setupFragmentGeneration();
    void setupFragmentShading();
    void setupFragmentTexturing();
    void setupFragmentLighting();
    void setupFragmentEffects();


    void setupTriangleSetup();
    void setupEdgeWalking();
    void setupScanConversion();
    void setupCoverageTesting();
    void setupDepthTesting();


    void setupAlphaBlending();
    void setupLogicOperations();
    void setupStencilOperations();
    void setupColorMasking();
    void setupFrameBufferBlending();


    void setupFrameBufferOutput();
    void setupDisplayOutput();
    void setupVideoOutput();
    void setupScreenshotOutput();
    void setupDebugOutput();


    void generateMipmapLevel(TextureInfo* tex, uint32_t level, uint32_t width, uint32_t height);
    void applyAnisotropicFilteringToLevel(TextureInfo* tex, uint32_t level);
    void setupLRUEviction();


    void updateGPUStatus();
    void updateVertexPipelineStatus();
    void updateFragmentPipelineStatus();
    void updateRasterizationPipelineStatus();
    void updateBlendingPipelineStatus();
    void updateOutputPipelineStatus();
    void updateTextureManagementStatus();
    void updateShaderSystemStatus();
    void updatePerformanceStatus();
    void optimizePerformance();


    void compileShaderProgram(const std::string& source, uint32_t programId);
    void executeTessellationShader(uint32_t programId, const std::vector<Vertex>& inputVertices);
    void executeComputeShader(uint32_t programId, uint32_t workGroupX, uint32_t workGroupY, uint32_t workGroupZ);
    void executeHardwareInstruction(uint32_t instruction);


    void startPerformanceMonitoring();
    void updatePerformanceCounters();


    void handleGPUError(GPUError error, const std::string& context);


    void optimizeMemoryUsage();


    void saveGPUState();
    void restoreGPUState();


    std::vector<std::string> tokenizeShaderSource(const std::string& source);
    std::string getErrorString(GPUError error);


    void executeVertexInstruction(uint32_t instruction);
    void executeFragmentInstruction(uint32_t instruction);
    void executeTextureInstruction(uint32_t instruction);
    void executeMemoryInstruction(uint32_t instruction);
    void executeControlInstruction(uint32_t instruction);
    void executeSpecialFunctionInstruction(uint32_t instruction);
    void executeFloatingPointInstruction(uint32_t instruction);
    void executeIntegerInstruction(uint32_t instruction);


    void updateParticleSystem(uint32_t x, uint32_t y, uint32_t z);
    void updatePhysicsSimulation(uint32_t x, uint32_t y, uint32_t z);
    void applyPostProcessing(uint32_t x, uint32_t y, uint32_t z);
    void updateAIComputation(uint32_t x, uint32_t y, uint32_t z);
    void processAudioData(uint32_t x, uint32_t y, uint32_t z);


    void defragmentTextureMemory();
    void compactVertexBuffers();
    void optimizeShaderCache();
    void cleanupUnusedResources();
    void preallocateMemoryPools();


    void useDefaultShader();
    void useDefaultTexture();
    void resizeBuffer();
    void resetGPUState();
    void recoverFromError();
    void logErrorForDebugging(GPUError error, const std::string& context);
    void enableHardwarePerformanceCounters();


    void completeGPUImplementation();
    void completeTextureSampling();
    void completeBlendingOperations();
    void completeDepthAndStencilOperations();
    void completeFogAndLightingEffects();
    void completeVertexProcessing();
    void completeFragmentProcessing();
    void completeRasterizationOperations();
    void completeOutputOperations();
    void completePerformanceOptimizations();
    void completeXboxCompatibility();


    void completeTextureFormatSupport(TextureInfo* tex);
    void completeTextureFiltering(TextureInfo* tex);
    void completeTextureAddressing(TextureInfo* tex);
    void completeTextureCoordinateGeneration(TextureInfo* tex);


    void completeAlphaBlending();
    void completeLogicOperations();
    void completeColorBlending();
    void completeFrameBufferBlending();


    void completeDepthTesting();
    void completeStencilTesting();
    void completeDepthWriting();
    void completeStencilWriting();


    void completeFogEffects();
    void completeLightingCalculations();
    void completeMaterialProperties();
    void completeEnvironmentMapping();


    void completeVertexTransformation();
    void completeVertexLighting();
    void completeVertexClipping();
    void completeVertexCulling();


    void completeFragmentGeneration();
    void completeFragmentShading();
    void completeFragmentTexturing();
    void completeFragmentEffects();


    void completeTriangleSetup();
    void completeEdgeWalking();
    void completeScanConversion();
    void completeCoverageTesting();


    void completeFrameBufferOutput();
    void completeDisplayOutput();
    void completeVideoOutput();
    void completeScreenshotOutput();


    void completeMemoryOptimizations();
    void completeCacheOptimizations();
    void completeThreadOptimizations();
    void completeNEONOptimizations();


    void completeHaloCompatibility();
    void completeFableCompatibility();
    void completePGRCompatibility();
    void completeGenericXboxCompatibility();


    void validateCompleteGPU();
    bool validateTextureSystem();
    bool validateShaderSystem();
    bool validateRenderingPipeline();
    bool validatePerformanceOptimizations();
    bool validateXboxCompatibility();

    void updateScalingFactors() {

    }


    void renderLoadingScreen();
    void renderGameContent(uint32_t signature, uint32_t version, uint32_t state);
    void renderSimpleGameScreen();

public:

    void processCommandBuffer();


    void setLoadingProgress(float progress); 
    void setLoadingText(const std::string& text);


    void updateDisplay();



    void onVertexDataUpdate(uint32_t offset, uint32_t value);
    void onIndexDataUpdate(uint32_t offset, uint32_t value);
    void onTextureDataUpdate(uint32_t offset, uint32_t value);


    void markMemoryRegionDirty(uint32_t offset, uint32_t size);
    void markTextureDirty(uint32_t textureBlock);
    void updateGPUState();


    void renderGameGeometry();

    void renderGameContentFromMemory(uint32_t memoryRegion);
    void generateGameContentFromAnyMemory();
    void generateTestPatternFromGameData(uint32_t region, uint32_t dataCount);
    void syncFramebufferFromMemory();
    void checkForVertexData();
    void updateVertexBufferFromMemory();
    void updateIndexBufferFromMemory();
    void processMemoryUpdates();


    void loadVerticesFromRegion(uint32_t region);
    void detectVerticesByPattern(uint32_t region);
    void analyzeMemoryPatterns(uint32_t region);
    bool isValidVertexData(uint32_t data);
    uint32_t calculateMemoryQuality(uint32_t region);


    void generateCommandsFromMemory();
    void generateTestCommands();
    void interpretAsData(uint32_t command);
    void processXboxCommand(uint32_t command);
    void handleAdvancedPrimitive(uint32_t command);
    void processVertexArray(uint32_t command);
    void handleTextureCommand(uint32_t command);
    void processRenderState(uint32_t command);


    void renderAdvancedGeometry();
    void applyPostProcessing();
    void renderDebugOverlay();
    void optimizeRenderingPipeline();
    void handleSpecialEffects();


    void initializeGPU();
    void resetGPU();
    void processGPUCommands();
    void swapBuffers();
    void clearBuffers();
    void setFog(bool enable, uint32_t color, float start, float end);
    void setLighting(bool enable, float direction[3], float color[3]);
    void setTexture(uint32_t unit, uint32_t address, uint32_t format, uint32_t width, uint32_t height);
    void setTextureFiltering(bool enable, float anisotropy);
    void setTextureSwizzling(bool enable);
    void setMultisampling(bool enable, uint32_t samples);
    void setVSync(bool enable);
    void setPerformanceMode(uint32_t mode);
    void updateTextureCache();
    void optimizeVertexBuffer();
    void applyBackfaceCulling();
    void processFragmentShaders();
    void applyTextureFiltering();
    void handleTextureCompression();
    void processVertexShaders();
    void applyFogEffects();
    void handleStencilOperations();
    void processLogicOperations();
    void applyColorMasking();
    void handleFrameBufferOperations();
    void processDisplayOutput();
    void handleVideoOutput();
    void processScreenshotOutput();
    void handleDebugOutput();
    void updateStreamingPipeline();
    void processPrefetchSystem();
    void updateQualityScaling();
    void manageTextureMemory();
    void processDMAOperations();
    void handleInterrupts();
    void validateGPUState();
    void optimizeRenderingPerformance();
    void handleGPUErrors();
    void processGPUTiming();
    void updateGPUMetrics();
    void handleGPUSynchronization();
    void processGPUCommandsAdvanced();
    void renderFrameAdvanced();
    void applyAdvancedEffects();
    void handleAdvancedTexturing();
    void processAdvancedLighting();
    void applyAdvancedBlending();
    void handleAdvancedFog();
    void processAdvancedStencil();
    void applyAdvancedLogicOps();
    void handleAdvancedColorOps();
    void processAdvancedFrameBuffer();
    void applyAdvancedDisplay();
    void handleAdvancedVideo();
    void processAdvancedScreenshot();
    void applyAdvancedDebug();
    void updateAdvancedStreaming();
    void processAdvancedPrefetch();
    void applyAdvancedQuality();
    void handleAdvancedMemory();
    void processAdvancedDMA();
    void handleAdvancedInterrupts();
    void updateAdvancedPerformance();
    void saveAdvancedGPUState();
    void restoreAdvancedGPUState();
    void validateAdvancedGPUState();
    void optimizeAdvancedPerformance();
    void handleAdvancedErrors();
    void processAdvancedTiming();
    void updateAdvancedMetrics();
    void handleAdvancedSynchronization();


    void renderTriangle(const Vertex& v1, const Vertex& v2, const Vertex& v3);


    uint32_t interpolateColor(uint32_t c1, uint32_t c2, uint32_t c3, float bary1, float bary2, float bary3);
    uint32_t applyTexture(uint32_t baseColor, float u, float v, int textureUnit);
    uint32_t applyFog(uint32_t color, float fogFactor);
    uint32_t getTexturePixel(int textureUnit, int x, int y);
    uint32_t modulateColors(uint32_t baseColor, uint32_t texColor);
    void applyXboxTransformations();
    void updateTextureBlock(size_t blockIndex);


    float calculateFogFactor(float x, float y, float z);


    bool vertexBufferDirty = false;
    bool indexBufferDirty = false;
    std::vector<bool> textureDirtyFlags;


    bool memoryUpdatePending = false;
    bool gpuStateUpdated = false;


    std::vector<uint16_t> indexBuffer;


    struct ViewportState {
        float offset[2] = {0.0f, 0.0f};
        float scale[2] = {1.0f, 1.0f};
        float depth[2] = {0.0f, 1.0f};
    } viewport;


    bool fogEnabled = false;
    float fogColor[4] = {0.5f, 0.5f, 0.5f, 1.0f};


    void updateRenderState();
    void updateVertexState();
    void updateTextureState();



};


inline void swap(NV2ARenderer::Vertex& a, NV2ARenderer::Vertex& b) noexcept {
    a.swap(b);
}


namespace std {
    template<>
    inline void swap<NV2ARenderer::Vertex>(NV2ARenderer::Vertex& a, NV2ARenderer::Vertex& b) noexcept {
        a.swap(b);
    }
}

#endif 
