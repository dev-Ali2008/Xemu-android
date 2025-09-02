#pragma once

#include <GLES3/gl32.h>
#include <EGL/egl.h>
#include <android/native_window.h>
#include <vector>
#include <memory>
#include <string>
#include "xbox_memory.h"

class OpenGLRenderer {
public:
    enum class RendererState {
        Uninitialized,
        Initialized,
        Error
    };

    struct Vertex {
        float position[3];
        float texCoord[2];
        float color[4];
    };

    struct UniformBufferObject {
        float model[16];
        float view[16];
        float proj[16];
    };


    OpenGLRenderer(XboxMemory* memory = nullptr);
    ~OpenGLRenderer();


    bool initialize();
    void cleanup();


    bool createEGLContext();
    bool createEGLSurface(ANativeWindow* window = nullptr);
    bool setSurface(ANativeWindow* window);
    void destroyEGLContext();


    bool createShaders();
    bool compileShader(GLuint shader, const std::string& source);
    bool linkProgram(GLuint program);
    void cleanupShaders();


    bool createBuffers();
    bool createVertexBuffer();
    bool createIndexBuffer();
    bool createUniformBuffer();
    void updateUniformBuffer();
    void cleanupBuffers();


    bool createTextures();
    bool createTextureImage();
    bool createTextureSampler();
    void cleanupTextures();
    void updateTextureFromMemory();
    void writeTestDataToXboxMemory(); 
    void writeTestDataToTexture(); 


    void renderFrame();
    void drawFrame();
    void clearScreen();
    void swapBuffers();
    void renderSimpleColoredQuad(); 
    void checkDisplayStatus(); 


    const uint32_t* getFramebuffer() const;
    bool hasAudioOutput() const;
    const uint32_t* getAudioBuffer() const;
    bool checkInterrupt() const;
    void enableVSync(bool enabled);


    RendererState getState() const { return state; }
    bool isInitialized() const { return state == RendererState::Initialized; }

private:

    EGLDisplay eglDisplay;
    EGLContext eglContext;
    EGLSurface eglSurface;
    EGLConfig eglConfig;


    GLuint vertexShader;
    GLuint fragmentShader;
    GLuint shaderProgram;
    GLuint vertexBuffer;
    GLuint indexBuffer;
    GLuint uniformBuffer;
    GLuint texture;
    GLuint framebuffer;
    GLuint renderbuffer;


    RendererState state;
    bool vsyncEnabled;
    uint32_t frameCount;
    uint32_t currentImageIndex;


    XboxMemory* memory;


    static constexpr uint32_t FB_WIDTH = 1280;   
    static constexpr uint32_t FB_HEIGHT = 720;   
    static constexpr uint32_t FB_SIZE = FB_WIDTH * FB_HEIGHT;


    bool checkGLError(const char* operation);
    void logGLError(const char* operation);
    bool validateShader(GLuint shader);
    bool validateProgram(GLuint program);
};
