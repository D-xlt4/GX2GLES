#pragma once

#include <coreinit/foreground.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memfrmheap.h>
#include <coreinit/memheap.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <proc_ui/procui.h>
#include <whb/sdcard.h>
#include <proc_ui/procui.h>
#include <whb/sdcard.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <malloc.h>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <array>
#include "gl.h"
#include "CafeGLSLCompiler.h"

struct gl_Buffer {
    bool isGenerated = false;
    bool isBound = false;
    void* data = nullptr;
    size_t size = 0;
    GLenum target = GL_ARRAY_BUFFER;
    GLenum usage = GL_STATIC_DRAW;
};

enum class ShaderType {
    VERTEX,
    FRAGMENT
};

struct gl_Shader {
    bool isGenerated = false;
    bool isCompiled = false;

    union {
        GX2PixelShader* pixelshader;
        GX2VertexShader* vertexshader;
    }internal_Shader;

    ShaderType type;
    std::string source;
};

struct gl_Uniform{
    std::string name = "";
    GX2UniformVar* vertexUniform = nullptr;
    GX2UniformVar* pixelUniform = nullptr;
    int location = -1;
    std::vector<uint8_t> data = {};
    
};

struct gl_Program {
    bool isGenerated = false;
    bool isLinked = false;
    bool uniformsDirty = true;
    GX2VertexShader* vertexShader = nullptr;
    GX2PixelShader* pixelShader = nullptr;
    GLuint vertexShaderID = 0;
    GLuint pixelShaderID = 0;
    std::vector<gl_Uniform> uniforms = {};
    //GX2ShaderSet* shaderSet = nullptr;
};

struct gl_Texture {
    bool isGenerated = false;
    bool isBound = false;
    GLenum target = GL_TEXTURE_2D;
    GX2Texture* texture = nullptr;

    GLenum TextureMinFilter = GL_NEAREST_MIPMAP_LINEAR;
    GLenum TextureMagFilter = GL_LINEAR;
    GLenum TextureWrapS = GL_REPEAT;
    GLenum TextureWrapT = GL_REPEAT;
};

struct gl_VertexAttrib {
    bool enabled   = false;
    GLuint vbo     = 0;
    GLint size     = 4;
    GLenum type    = GL_FLOAT;
    GLboolean normalized = false;
    GLsizei stride = 0;
    uintptr_t offset = 0;
};

struct GLstuff {
    int window_width;
    int window_height;
    int currentVBO = 0;
    int currentEBO = 0;
    int currentProgram = 0;
    int currentTexture = 0;

    float clearRed = 0;
    float clearGreen = 0;
    float clearBlue = 0;
    float clearAlpha = 0;
    float clearDepth = 1;
    uint8_t clearStencil = 0;
    
    int activeTextureUnit = 0;
    std::array<GLuint, 16> textureUnits = {0};
    
};



void initializing_glstuff(uint32_t width, uint32_t height, uint32_t* pWidth, uint32_t* pHeight);
void exit_glstuff();
void WindowSwapBuffers();
bool WindowIsRunning();
void WindowMakeContextCurrent();
GX2ColorBuffer* WindowGetColorBuffer();
GX2DepthBuffer* WindowGetDepthBuffer();
// void WindowSwapBuffers();