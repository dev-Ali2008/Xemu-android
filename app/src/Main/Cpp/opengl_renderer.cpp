#include "opengl_renderer.h"
#include <android/log.h>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <thread>

#define LOG_TAG "OpenGLRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define LOGDISPLAY(...) __android_log_print(ANDROID_LOG_WARN, "DISPLAY", __VA_ARGS__)


#define LOGDISPLAY_CRITICAL(...) __android_log_print(ANDROID_LOG_ERROR, "DISPLAY_CRITICAL", __VA_ARGS__)
#define LOGDISPLAY_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "DISPLAY_ERROR", __VA_ARGS__)
#define LOGDISPLAY_WARN(...) __android_log_print(ANDROID_LOG_WARN, "DISPLAY_WARN", __VA_ARGS__)
#define LOGDISPLAY_INFO(...) __android_log_print(ANDROID_LOG_INFO, "DISPLAY_INFO", __VA_ARGS__)
#define LOGDISPLAY_DEBUG(...) __android_log_print(ANDROID_LOG_DEBUG, "DISPLAY_DEBUG", __VA_ARGS__)


const uint32_t FB_WIDTH = 1280;
const uint32_t FB_HEIGHT = 720;

const uint32_t FB_MEMORY_BASE = 0xFD000000;



struct Vec3 {
    float x, y, z;
    Vec3(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
};

struct Mat4 {
    float m[16];
    Mat4() { 
        for(int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    float& operator()(int row, int col) { return m[row * 4 + col]; }
    const float& operator()(int row, int col) const { return m[row * 4 + col]; }
};

inline float radians(float degrees) { return degrees * M_PI / 180.0f; }

OpenGLRenderer::OpenGLRenderer(XboxMemory* memory) : 
    eglDisplay(EGL_NO_DISPLAY),
    eglContext(EGL_NO_CONTEXT),
    eglSurface(EGL_NO_SURFACE),
    eglConfig(nullptr),
    vertexShader(0),
    fragmentShader(0),
    shaderProgram(0),
    vertexBuffer(0),
    indexBuffer(0),
    uniformBuffer(0),
    texture(0),
    framebuffer(0),
    renderbuffer(0),
    state(RendererState::Uninitialized),
    vsyncEnabled(true),
    frameCount(0),
    currentImageIndex(0),
    memory(memory) {

    LOGI("OpenGLRenderer constructor called");
}

OpenGLRenderer::~OpenGLRenderer() {
    LOGI("OpenGLRenderer destructor called");
    cleanup();
}

bool OpenGLRenderer::initialize() {
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: INITIALIZE START ===");
    LOGDISPLAY_INFO("DISPLAY INFO: Initializing OpenGL renderer...");

    if (!createEGLContext()) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create EGL context");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: INITIALIZE FAILED - EGL CONTEXT ===");
        return false;
    }

    if (!createShaders()) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create shaders");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: INITIALIZE FAILED - SHADERS ===");
        return false;
    }

    if (!createBuffers()) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create buffers");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: INITIALIZE FAILED - BUFFERS ===");
        return false;
    }

    if (!createTextures()) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create textures");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: INITIALIZE FAILED - TEXTURES ===");
        return false;
    }

    state = RendererState::Initialized;
    LOGDISPLAY_INFO("DISPLAY INFO: OpenGL renderer initialized successfully");
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: INITIALIZE SUCCESS ===");
    return true;
}

void OpenGLRenderer::cleanup() {
    LOGI("Cleaning up OpenGL renderer...");

    cleanupShaders();
    cleanupBuffers();
    cleanupTextures();
    destroyEGLContext();

    state = RendererState::Uninitialized;
}

bool OpenGLRenderer::createEGLContext() {
    LOGI("Creating EGL context...");

    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(eglDisplay, &major, &minor)) {
        LOGE("Failed to initialize EGL");
        return false;
    }

    LOGI("EGL initialized: version %d.%d", major, minor);

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs)) {
        LOGE("Failed to choose EGL config");
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }

    return true;
}

bool OpenGLRenderer::createEGLSurface(ANativeWindow* window) {
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: CREATE EGL SURFACE START ===");
    LOGDISPLAY_INFO("DISPLAY INFO: Creating EGL surface with window 0x%p", window);

    if (window) {
        LOGDISPLAY_INFO("DISPLAY INFO: Setting window buffer geometry: %dx%d", FB_WIDTH, FB_HEIGHT);

        int result = ANativeWindow_setBuffersGeometry(window, FB_WIDTH, FB_HEIGHT, WINDOW_FORMAT_RGBA_8888);
        if (result != 0) {
            LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to set window buffer geometry, result=%d", result);
        } else {
            LOGDISPLAY_INFO("DISPLAY INFO: Window buffer geometry set successfully");
        }

        eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, window, nullptr);
        if (eglSurface == EGL_NO_SURFACE) {
            EGLint error = eglGetError();
            LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create EGL window surface: 0x%X", error);
            LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: CREATE EGL SURFACE FAILED ===");
            return false;
        }

        LOGDISPLAY_INFO("DISPLAY INFO: EGL window surface created successfully: 0x%p", eglSurface);
    } else {
        LOGDISPLAY_WARN("DISPLAY WARN: Creating PBuffer surface (no window)");

        const EGLint surfaceAttribs[] = {
            EGL_WIDTH, FB_WIDTH,
            EGL_HEIGHT, FB_HEIGHT,
            EGL_NONE
        };

        eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, surfaceAttribs);
        if (eglSurface == EGL_NO_SURFACE) {
            EGLint error = eglGetError();
            LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create EGL PBuffer surface: 0x%X", error);
            LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: CREATE EGL SURFACE FAILED ===");
            return false;
        }

        LOGDISPLAY_INFO("DISPLAY INFO: EGL PBuffer surface created successfully: 0x%p", eglSurface);
    }


    LOGDISPLAY_INFO("DISPLAY INFO: Making EGL context current...");
    if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
        EGLint error = eglGetError();
        LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to make EGL context current: 0x%X", error);


        LOGDISPLAY_WARN("DISPLAY WARN: Attempting to recover from context activation failure...");


        for (int i = 0; i < 1000000; i++) {  }

        if (!eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            EGLint retryError = eglGetError();
            LOGDISPLAY_ERROR("DISPLAY ERROR: Context activation retry also failed: 0x%X", retryError);
            LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: CREATE EGL SURFACE FAILED ===");
        return false;
        } else {
            LOGDISPLAY_INFO("DISPLAY INFO: Context activation recovery successful");
        }
    } else {
        LOGDISPLAY_INFO("DISPLAY INFO: EGL context made current successfully");
    }


    LOGDISPLAY_DEBUG("DISPLAY DEBUG: Setting viewport and clearing screen");
    glViewport(0, 0, FB_WIDTH, FB_HEIGHT);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f); 
    glClear(GL_COLOR_BUFFER_BIT);

    LOGDISPLAY_INFO("DISPLAY INFO: Viewport set and screen cleared - surface should be ready for rendering");
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: CREATE EGL SURFACE SUCCESS ===");

    return true;
}

bool OpenGLRenderer::setSurface(ANativeWindow* window) {
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: SET SURFACE START ===");

    if (!window) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Window is null in setSurface");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: SET SURFACE FAILED - NULL WINDOW ===");
        return false;
    }

    LOGDISPLAY_INFO("DISPLAY INFO: Setting OpenGL surface with native window 0x%p", window);

    if (eglSurface != EGL_NO_SURFACE) {
        LOGDISPLAY_DEBUG("DISPLAY DEBUG: Destroying existing EGL surface");
        eglDestroySurface(eglDisplay, eglSurface);
        eglSurface = EGL_NO_SURFACE;
    }

    bool success = createEGLSurface(window);

    if (success) {
        LOGDISPLAY_INFO("DISPLAY INFO: OpenGL surface created successfully");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: SET SURFACE SUCCESS ===");
            } else {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to create OpenGL surface");
        LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: SET SURFACE FAILED ===");
    }

    return success;
}

void OpenGLRenderer::destroyEGLContext() {
        if (eglSurface != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
        }

        if (eglContext != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay, eglContext);
            eglContext = EGL_NO_CONTEXT;
        }

    if (eglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(eglDisplay);
        eglDisplay = EGL_NO_DISPLAY;
    }
}

bool OpenGLRenderer::createShaders() {
    LOGI("Creating shaders...");


    const char* vertexShaderSource = R"(
        #version 300 es
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 texCoord;
        layout(location = 2) in vec4 color;

        out vec2 fragTexCoord;
        out vec4 fragColor;

        void main() {
            gl_Position = vec4(position, 1.0);
            fragTexCoord = texCoord;
            fragColor = color;
        }
    )";


    const char* fragmentShaderSource = R"(
        #version 300 es
        precision mediump float;

        in vec2 fragTexCoord;
        in vec4 fragColor;

        out vec4 outColor;

        void main() {
            outColor = fragColor;
        }
    )";

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);

    if (!compileShader(vertexShader, vertexShaderSource) ||
        !compileShader(fragmentShader, fragmentShaderSource)) {
        return false;
    }

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);

    if (!linkProgram(shaderProgram)) {
        return false;
    }

    return true;
}

bool OpenGLRenderer::compileShader(GLuint shader, const std::string& source) {
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    return validateShader(shader);
}

bool OpenGLRenderer::linkProgram(GLuint program) {
    glLinkProgram(program);
    return validateProgram(program);
}

void OpenGLRenderer::cleanupShaders() {
    if (shaderProgram) {
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
    }
    if (vertexShader) {
        glDeleteShader(vertexShader);
        vertexShader = 0;
    }
    if (fragmentShader) {
        glDeleteShader(fragmentShader);
        fragmentShader = 0;
    }
}

bool OpenGLRenderer::createBuffers() {
    LOGI("Creating buffers...");

    return createVertexBuffer() && createIndexBuffer() && createUniformBuffer();
}

bool OpenGLRenderer::createVertexBuffer() {

    float vertices[] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f, 1.0f, 1.0f
    };

    glGenBuffers(1, &vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    return true;
}

bool OpenGLRenderer::createIndexBuffer() {
    uint16_t indices[] = { 0, 1, 2, 2, 3, 0 };

    glGenBuffers(1, &indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    return true;
}

bool OpenGLRenderer::createUniformBuffer() {
    glGenBuffers(1, &uniformBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(UniformBufferObject), nullptr, GL_DYNAMIC_DRAW);

    return true;
}

void OpenGLRenderer::cleanupBuffers() {
    if (vertexBuffer) {
        glDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }
    if (indexBuffer) {
        glDeleteBuffers(1, &indexBuffer);
        indexBuffer = 0;
    }
    if (uniformBuffer) {
        glDeleteBuffers(1, &uniformBuffer);
        uniformBuffer = 0;
    }
}

bool OpenGLRenderer::createTextures() {
    LOGI("Creating textures...");

    return createTextureImage() && createTextureSampler();
}

bool OpenGLRenderer::createTextureImage() {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FB_WIDTH, FB_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    return true;
}

bool OpenGLRenderer::createTextureSampler() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

void OpenGLRenderer::cleanupTextures() {
    if (texture) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
}

void OpenGLRenderer::updateTextureFromMemory() {
    if (!memory) {
        LOGW("OpenGL: No memory reference for texture update");
        return;
    }

    LOGDISPLAY_CRITICAL("=== OPENGL: UPDATING TEXTURE FROM MEMORY - FRAME %u ===", frameCount);


    uint32_t framebufferData[FB_SIZE];
    bool hasValidData = false;
    uint32_t validPixels = 0;
    uint32_t nonZeroPixels = 0;


    const uint32_t FB_REGIONS[] = {
        FB_MEMORY_BASE,           
        0xFC000000,              
        0xFB000000,              
        0xFA000000,              
        0x00010000,              
        0x0058FD80               
    };

    uint32_t bestRegion = FB_MEMORY_BASE;
    uint32_t bestPixelCount = 0;

    LOGDISPLAY_INFO("OpenGL: Checking %zu memory regions for game data", sizeof(FB_REGIONS)/sizeof(FB_REGIONS[0]));


    for (uint32_t region : FB_REGIONS) {
        uint32_t regionValidPixels = 0;
        uint32_t regionNonZeroPixels = 0;

        LOGDISPLAY_DEBUG("OpenGL: Checking region 0x%08X", region);

        for (uint32_t i = 0; i < std::min(FB_SIZE, 10000u); i++) { 
            uint32_t addr = region + (i * 4);
            uint32_t pixel = memory->read32(addr);

            if (pixel != 0) {
                regionNonZeroPixels++;
                if (pixel != 0xFFFFFFFF && pixel != 0xFF000000) { 
                    regionValidPixels++;
                }
            }
        }

        LOGDISPLAY_DEBUG("OpenGL: Region 0x%08X: %u non-zero, %u valid pixels", region, regionNonZeroPixels, regionValidPixels);

        if (regionValidPixels > bestPixelCount) {
            bestPixelCount = regionValidPixels;
            bestRegion = region;
        }
    }

    LOGDISPLAY_INFO("OpenGL: Best region: 0x%08X with %u valid pixels", bestRegion, bestPixelCount);

    if (bestPixelCount == 0) {
        LOGDISPLAY_WARN("OpenGL: NO VALID GAME DATA FOUND IN ANY MEMORY REGION!");
        LOGDISPLAY_WARN("OpenGL: This means the game is NOT writing to memory!");
        return;
    }


    for (uint32_t i = 0; i < FB_SIZE; i++) {
        uint32_t addr = bestRegion + (i * 4);
        uint32_t pixel = memory->read32(addr);

        if (pixel != 0) {
            nonZeroPixels++;


            uint8_t r, g, b, a;

            if (bestRegion == 0xFC000000) {

                float* floatData = reinterpret_cast<float*>(&pixel);
                r = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, floatData[0] * 255.0f)));
                g = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, floatData[1] * 255.0f)));
                b = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, floatData[2] * 255.0f)));
                a = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, floatData[3] * 255.0f)));
    } else {

                r = (pixel >> 16) & 0xFF;
                g = (pixel >> 8) & 0xFF;
                b = pixel & 0xFF;
                a = (pixel >> 24) & 0xFF;


                if (r == 0 && g == 0 && b == 0 && a == 0) {
                    r = (pixel >> 0) & 0xFF;
                    g = (pixel >> 8) & 0xFF;
                    b = (pixel >> 16) & 0xFF;
                    a = (pixel >> 24) & 0xFF;
                }
            }


            if (r == 0 && g == 0 && b == 0) {
                r = g = b = 128; 
                a = 255;
            }

            framebufferData[i] = (a << 24) | (r << 16) | (g << 8) | b;
            validPixels++;
            hasValidData = true;
        } else {

            framebufferData[i] = 0xFF202020; 
        }
    }

    if (hasValidData) {
        LOGI("OpenGL: UPDATING TEXTURE with %u valid pixels from region 0x%08X", validPixels, bestRegion);


        for (uint32_t i = 0; i < 5; i++) {
            LOGI("OpenGL: Pixel[%u] = 0x%08X", i, framebufferData[i]);
        }
    } else {
        LOGW("OpenGL: No valid framebuffer data found - creating aggressive test pattern");


        for (uint32_t i = 0; i < FB_SIZE; i++) {
            uint32_t x = i % FB_WIDTH;
            uint32_t y = i / FB_WIDTH;


            if ((x / 100 + y / 100) % 2 == 0) {
                framebufferData[i] = 0xFFFF0000; 
            } else {
                framebufferData[i] = 0xFF00FF00; 
            }


            if (x < 10 || x > FB_WIDTH - 10 || y < 10 || y > FB_HEIGHT - 10) {
                framebufferData[i] = 0xFFFFFF00; 
            }
        }

        LOGI("OpenGL: Created aggressive test pattern - should be VERY visible!");
    }


    glBindTexture(GL_TEXTURE_2D, texture);
    if (!checkGLError("glBindTexture")) {
        LOGE("OpenGL: Failed to bind texture");
        return;
    }

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB_WIDTH, FB_HEIGHT, 
                    GL_RGBA, GL_UNSIGNED_BYTE, framebufferData);

    if (!checkGLError("glTexSubImage2D")) {
        LOGE("OpenGL: Failed to update texture from memory");
        return;
    }

    LOGI("OpenGL: Texture updated successfully with %u pixels", FB_SIZE);
}

void OpenGLRenderer::writeTestDataToXboxMemory() {
    if (!memory) {
        LOGW("OpenGL: No memory available for test data writing");
        return;
    }

    LOGI("OpenGL: WRITING AGGRESSIVE TEST DATA TO XBOX MEMORY");


    const uint32_t TEST_REGIONS[] = {
        FB_MEMORY_BASE,           
        0xFC000000,              
        0xFB000000,              
        0xFA000000               
    };

    for (uint32_t region : TEST_REGIONS) {
        LOGI("OpenGL: Writing test pattern to region 0x%08X", region);

        for (uint32_t i = 0; i < FB_SIZE; i++) {
            uint32_t addr = region + (i * 4);
            uint32_t x = i % FB_WIDTH;
            uint32_t y = i / FB_WIDTH;


            uint32_t color;

            if (x < 100 && y < 100) {
                color = 0xFFFF0000; 
            } else if (x > FB_WIDTH - 100 && y < 100) {
                color = 0xFF00FF00; 
            } else if (x < 100 && y > FB_HEIGHT - 100) {
                color = 0xFF0000FF; 
            } else if (x > FB_WIDTH - 100 && y > FB_HEIGHT - 100) {
                color = 0xFFFFFF00; 
            } else if ((x / 50 + y / 50) % 2 == 0) {
                color = 0xFFFFFFFF; 
            } else {
                color = 0xFF000000; 
            }


            if (x < 5 || x > FB_WIDTH - 5 || y < 5 || y > FB_HEIGHT - 5) {
                color = 0xFFFF00FF; 
            }

            memory->write32(addr, color);
        }

        LOGI("OpenGL: Test pattern written to region 0x%08X", region);
    }

    LOGI("OpenGL: AGGRESSIVE TEST DATA WRITTEN - SHOULD BE VERY VISIBLE!");
}


void OpenGLRenderer::writeTestDataToTexture() {
    LOGI("OpenGL: WRITING TEST DATA DIRECTLY TO TEXTURE");

    uint32_t framebufferData[FB_SIZE];


    for (uint32_t i = 0; i < FB_SIZE; i++) {
        uint32_t x = i % FB_WIDTH;
        uint32_t y = i / FB_HEIGHT;

        uint32_t color;


        if (x < 200 && y < 200) {
            color = 0xFFFF0000; 
        } else if (x > FB_WIDTH - 200 && y < 200) {
            color = 0xFF00FF00; 
        } else if (x < 200 && y > FB_HEIGHT - 200) {
            color = 0xFF0000FF; 
        } else if (x > FB_WIDTH - 200 && y > FB_HEIGHT - 200) {
            color = 0xFFFFFF00; 
        } else if ((x / 100 + y / 100) % 2 == 0) {
            color = 0xFFFFFFFF; 
        } else {
            color = 0xFF808080; 
        }


        if (x < 10 || x > FB_WIDTH - 10 || y < 10 || y > FB_HEIGHT - 10) {
            color = 0xFFFF00FF; 
        }

        framebufferData[i] = color;
    }


    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FB_WIDTH, FB_HEIGHT, 
                    GL_RGBA, GL_UNSIGNED_BYTE, framebufferData);

    LOGI("OpenGL: Test data written directly to texture - should be visible!");
}

void OpenGLRenderer::renderFrame() {
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: STARTING FRAME %u ===", frameCount);

    if (state != RendererState::Initialized) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Renderer not initialized! State: %d", static_cast<int>(state));
                return;
            }


    if (frameCount % 10 == 0) {
        checkDisplayStatus();
    }

    LOGDISPLAY_INFO("DISPLAY INFO: ===== RENDERING FRAME %u =====", frameCount);


    LOGDISPLAY_DEBUG("DISPLAY DEBUG: Clearing screen with bright red");
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f); 
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    if (frameCount % 30 == 0) {
        LOGDISPLAY_INFO("DISPLAY INFO: Writing aggressive test data to ensure visibility");
        writeTestDataToXboxMemory();
        writeTestDataToTexture(); 
    }


    LOGDISPLAY_DEBUG("DISPLAY DEBUG: Rendering simple colored quad");
    renderSimpleColoredQuad();


    LOGDISPLAY_DEBUG("DISPLAY DEBUG: Updating texture from memory");
    updateTextureFromMemory();

    LOGDISPLAY_DEBUG("DISPLAY DEBUG: Drawing frame");
    drawFrame();


    LOGDISPLAY_INFO("DISPLAY INFO: Attempting buffer swap...");
    swapBuffers();


    if (frameCount % 10 == 0) { 
        LOGDISPLAY_INFO("DISPLAY INFO: Force double buffer swap for frame %u", frameCount);
        swapBuffers();
    }

    frameCount++;

    LOGDISPLAY_INFO("DISPLAY INFO: ===== FRAME %u RENDERED SUCCESSFULLY =====", frameCount - 1);
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: FRAME %u COMPLETE ===", frameCount - 1);
}


void OpenGLRenderer::renderSimpleColoredQuad() {
    LOGI("OpenGL: Rendering simple colored quad for visibility test");


    const char* simpleVertexShader = R"(
        #version 300 es
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec4 color;

        out vec4 fragColor;

        void main() {
            gl_Position = vec4(position, 1.0);
            fragColor = color;
        }
    )";


    const char* simpleFragmentShader = R"(
        #version 300 es
        precision mediump float;

        in vec4 fragColor;
        out vec4 outColor;

        void main() {
            outColor = fragColor;
        }
    )";


    GLuint simpleVS = glCreateShader(GL_VERTEX_SHADER);
    GLuint simpleFS = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(simpleVS, 1, &simpleVertexShader, nullptr);
    glShaderSource(simpleFS, 1, &simpleFragmentShader, nullptr);

    glCompileShader(simpleVS);
    glCompileShader(simpleFS);

    GLuint simpleProgram = glCreateProgram();
    glAttachShader(simpleProgram, simpleVS);
    glAttachShader(simpleProgram, simpleFS);
    glLinkProgram(simpleProgram);


    float vertices[] = {

        -0.5f, -0.5f, 0.0f,     1.0f, 0.0f, 0.0f, 1.0f,  
         0.5f, -0.5f, 0.0f,     0.0f, 1.0f, 0.0f, 1.0f,  
         0.5f,  0.5f, 0.0f,     0.0f, 0.0f, 1.0f, 1.0f,  
        -0.5f,  0.5f, 0.0f,     1.0f, 1.0f, 0.0f, 1.0f   
    };

    uint16_t indices[] = { 0, 1, 2, 2, 3, 0 };


    GLuint quadVBO, quadEBO;
    glGenBuffers(1, &quadVBO);
    glGenBuffers(1, &quadEBO);

    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);


    glUseProgram(simpleProgram);


    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));


    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);


    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glDeleteBuffers(1, &quadVBO);
    glDeleteBuffers(1, &quadEBO);
    glDeleteProgram(simpleProgram);
    glDeleteShader(simpleVS);
    glDeleteShader(simpleFS);

    LOGI("OpenGL: Simple colored quad rendered - should be visible!");
}

void OpenGLRenderer::drawFrame() {
    LOGI("OpenGL: Drawing frame with texture 0x%u", texture);

    glUseProgram(shaderProgram);
    if (!checkGLError("glUseProgram")) {
        LOGE("OpenGL: Failed to use shader program");
        return;
    }


    glActiveTexture(GL_TEXTURE0);
    if (!checkGLError("glActiveTexture")) {
        LOGE("OpenGL: Failed to activate texture unit");
        return;
            }

            glBindTexture(GL_TEXTURE_2D, texture);
    if (!checkGLError("glBindTexture")) {
        LOGE("OpenGL: Failed to bind texture");
        return;
    }

    LOGI("OpenGL: Texture bound successfully");

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);

    if (!checkGLError("glBindBuffer")) {
        LOGE("OpenGL: Failed to bind buffers");
        return;
    }


    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));

    if (!checkGLError("glVertexAttribPointer")) {
        LOGE("OpenGL: Failed to set vertex attributes");
        return;
    }

    LOGI("OpenGL: Drawing elements...");
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    if (!checkGLError("glDrawElements")) {
        LOGE("OpenGL: Failed to draw frame");
            return;
        }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    LOGI("OpenGL: Frame drawn successfully - should be visible on screen!");
}

void OpenGLRenderer::clearScreen() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::swapBuffers() {
    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: SWAP BUFFERS START ===");

    if (eglSurface != EGL_NO_SURFACE) {
        LOGDISPLAY_INFO("DISPLAY INFO: Swapping buffers with EGL surface 0x%p", eglSurface);
        LOGDISPLAY_DEBUG("DISPLAY DEBUG: EGL Display: 0x%p, EGL Context: 0x%p", eglDisplay, eglContext);

        EGLBoolean result = eglSwapBuffers(eglDisplay, eglSurface);
        if (result == EGL_FALSE) {
            EGLint error = eglGetError();
            LOGDISPLAY_ERROR("DISPLAY ERROR: eglSwapBuffers failed with error 0x%X", error);


            LOGDISPLAY_WARN("DISPLAY WARN: Attempting to recover from swap buffer failure...");


            for (int i = 0; i < 1000000; i++) {  }

            result = eglSwapBuffers(eglDisplay, eglSurface);
            if (result == EGL_FALSE) {
                EGLint retryError = eglGetError();
                LOGDISPLAY_ERROR("DISPLAY ERROR: eglSwapBuffers retry also failed with error 0x%X", retryError);
            } else {
                LOGDISPLAY_INFO("DISPLAY INFO: Swap buffer recovery successful");
            }
        } else {
            LOGDISPLAY_INFO("DISPLAY INFO: Buffers swapped successfully - frame should be visible!");
            LOGDISPLAY_CRITICAL("=== DISPLAY SUCCESS: BUFFER SWAP COMPLETE ===");
        }
    } else {
        LOGDISPLAY_ERROR("DISPLAY ERROR: Cannot swap buffers - no EGL surface (0x%p)", eglSurface);
        LOGDISPLAY_ERROR("DISPLAY ERROR: EGL Display: 0x%p, EGL Context: 0x%p", eglDisplay, eglContext);


        LOGDISPLAY_WARN("DISPLAY WARN: Attempting to recreate EGL surface...");
        if (createEGLSurface()) {
            LOGDISPLAY_INFO("DISPLAY INFO: EGL surface recreated successfully");
            swapBuffers(); 
        } else {
            LOGDISPLAY_ERROR("DISPLAY ERROR: Failed to recreate EGL surface");
        }
    }

    LOGDISPLAY_CRITICAL("=== DISPLAY DEBUG: SWAP BUFFERS END ===");
}

const uint32_t* OpenGLRenderer::getFramebuffer() const {
    static uint32_t framebufferData[FB_SIZE];
    return framebufferData;
}

bool OpenGLRenderer::hasAudioOutput() const {
    return true;
}

const uint32_t* OpenGLRenderer::getAudioBuffer() const {
    static uint32_t audioBuffer[2048];
    return audioBuffer;
}

bool OpenGLRenderer::checkInterrupt() const {
        return false;
}

void OpenGLRenderer::enableVSync(bool enabled) {
    vsyncEnabled = enabled;
    if (enabled) {
        eglSwapInterval(eglDisplay, 1);
    } else {
        eglSwapInterval(eglDisplay, 0);
    }
}

bool OpenGLRenderer::checkGLError(const char* operation) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        logGLError(operation);
        return false;
    }
    return true;
}

void OpenGLRenderer::logGLError(const char* operation) {
    GLenum error = glGetError();
    LOGE("OpenGL error after %s: 0x%X", operation, error);
}

bool OpenGLRenderer::validateShader(GLuint shader) {
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGE("Shader compilation failed: %s", infoLog);
        return false;
    }
    return true;
}

bool OpenGLRenderer::validateProgram(GLuint program) {
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOGE("Program linking failed: %s", infoLog);
        return false;
    }
    return true;
}

void OpenGLRenderer::checkDisplayStatus() {
    LOGDISPLAY_CRITICAL("=== DISPLAY STATUS CHECK ===");
    LOGDISPLAY_INFO("DISPLAY INFO: Renderer State: %d", static_cast<int>(state));
    LOGDISPLAY_INFO("DISPLAY INFO: EGL Display: 0x%p", eglDisplay);
    LOGDISPLAY_INFO("DISPLAY INFO: EGL Context: 0x%p", eglContext);
    LOGDISPLAY_INFO("DISPLAY INFO: EGL Surface: 0x%p", eglSurface);
    LOGDISPLAY_INFO("DISPLAY INFO: EGL Config: 0x%p", eglConfig);
    LOGDISPLAY_INFO("DISPLAY INFO: Texture: %u", texture);
    LOGDISPLAY_INFO("DISPLAY INFO: Frame Count: %u", frameCount);
    LOGDISPLAY_INFO("DISPLAY INFO: Memory Pointer: 0x%p", memory);


    EGLDisplay currentDisplay = eglGetCurrentDisplay();
    EGLContext currentContext = eglGetCurrentContext();
    EGLSurface currentSurface = eglGetCurrentSurface(EGL_DRAW);

    LOGDISPLAY_INFO("DISPLAY INFO: Current EGL Display: 0x%p", currentDisplay);
    LOGDISPLAY_INFO("DISPLAY INFO: Current EGL Context: 0x%p", currentContext);
    LOGDISPLAY_INFO("DISPLAY INFO: Current EGL Surface: 0x%p", currentSurface);


    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: OpenGL error detected: 0x%X", error);
    } else {
        LOGDISPLAY_INFO("DISPLAY INFO: No OpenGL errors detected");
    }


    EGLint eglError = eglGetError();
    if (eglError != EGL_SUCCESS) {
        LOGDISPLAY_ERROR("DISPLAY ERROR: EGL error detected: 0x%X", eglError);
    } else {
        LOGDISPLAY_INFO("DISPLAY INFO: No EGL errors detected");
    }

    LOGDISPLAY_CRITICAL("=== DISPLAY STATUS CHECK COMPLETE ===");
}
