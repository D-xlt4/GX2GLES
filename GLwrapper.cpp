#include "GLwrapper.h"
#include <cstring>
static bool gIsRunning = false;
static void* gCmdlist = NULL;
static GX2ContextState* gContext = NULL;
static void* gTvScanBuffer = NULL;
static void* gDrcScanBuffer = NULL;
static GX2ColorBuffer gColorBuffer;
static void* gColorBufferImageData = NULL;
static GX2DepthBuffer gDepthBuffer;
static void* gDepthBufferImageData = NULL;
static MEMHeapHandle gMEM1HeapHandle;
static MEMHeapHandle gFgHeapHandle;


static GLstuff glstuff;
static std::vector<gl_Buffer> Buffer_list(1);
static std::vector<gl_Shader> Shader_list(1);
static std::vector<gl_Program> Program_list(1);
static std::vector<gl_Texture> Texture_list(1);
static gl_VertexAttrib attribState[16];

static void* streamRingBuffer = nullptr;
static const uint32_t STREAM_BUFFER_MAX_SIZE = 2 * 1024 * 1024;
static uint32_t streamBufferOffset = 0;


void initializing_glstuff(uint32_t width, uint32_t height, uint32_t* pWidth, uint32_t* pHeight){
    glstuff.window_width = width;
    glstuff.window_height = height;

    // Allocate GX2 command buffer
    gCmdlist = MEMAllocFromDefaultHeapEx(
        0x400000,                    // A very commonly used size in Nintendo games
        GX2_COMMAND_BUFFER_ALIGNMENT // Required alignment
    );

    if (!gCmdlist)
        return;

    // Several parameters to initialize GX2 with
    uint32_t initAttribs[] = {
        GX2_INIT_CMD_BUF_BASE, (uintptr_t)gCmdlist, // Command Buffer Base Address
        GX2_INIT_CMD_BUF_POOL_SIZE, 0x400000,       // Command Buffer Size
        GX2_INIT_ARGC, 0,                           // main() arguments count
        GX2_INIT_ARGV, (uintptr_t)NULL,             // main() arguments vector
        GX2_INIT_END                                // Ending delimiter
    };

    // Initialize GX2
    GX2Init(initAttribs);

    // Get the MEM1 heap and Foreground Bucket heap handles
    gMEM1HeapHandle = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM1);
    gFgHeapHandle = MEMGetBaseHeapHandle(MEM_BASE_HEAP_FG);

    uint32_t fb_width, fb_height;
    uint32_t drc_width, drc_height;

    // Allocate TV scan buffer
    {
        GX2TVRenderMode tv_render_mode;

        // Get current TV scan mode
        GX2TVScanMode tv_scan_mode = GX2GetSystemTVScanMode();

        // Determine TV framebuffer dimensions (scan buffer, specifically)
        if (tv_scan_mode != GX2_TV_SCAN_MODE_576I && tv_scan_mode != GX2_TV_SCAN_MODE_480I
            && width >= 1920 && height >= 1080)
        {
            fb_width = 1920;
            fb_height = 1080;
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_1080P;
        }
        else if (width >= 1280 && height >= 720)
        {
            fb_width = 1280;
            fb_height = 720;
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_720P;
        }
        else if (width >= 850 && height >= 480)
        {
            fb_width = 854;
            fb_height = 480;
            tv_render_mode = GX2_TV_RENDER_MODE_WIDE_480P;
        }
        else // if (width >= 640 && height >= 480)
        {
            fb_width = 640;
            fb_height = 480;
            tv_render_mode = GX2_TV_RENDER_MODE_STANDARD_480P;
        }

        // Calculate TV scan buffer byte size
        uint32_t tv_scan_buffer_size, unk;
        GX2CalcTVSize(
            tv_render_mode,                       // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE,            // Two buffers for double-buffering
            &tv_scan_buffer_size,                 // Output byte size
            &unk                                  // Unknown; seems like we have no use for it
        );

        // Allocate TV scan buffer
        gTvScanBuffer = MEMAllocFromFrmHeapEx(
            gFgHeapHandle,
            tv_scan_buffer_size,
            GX2_SCAN_BUFFER_ALIGNMENT // Required alignment
        );

        if (!gTvScanBuffer)
        {
            exit_glstuff();
            return;
        }

        // Flush allocated buffer from CPU cache
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, gTvScanBuffer, tv_scan_buffer_size);

        // Set the current TV scan buffer
        GX2SetTVBuffer(
            gTvScanBuffer,                        // Scan Buffer
            tv_scan_buffer_size,                  // Scan Buffer Size
            tv_render_mode,                       // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE             // Enable double-buffering
        );
        
        // Set the current TV scan buffer dimensions
        GX2SetTVScale(fb_width, fb_height);
    }
    
    // Allocate DRC (Gamepad) scan buffer
    {
        drc_width = 854;
        drc_height = 480;

        // Calculate DRC scan buffer byte size
        uint32_t drc_scan_buffer_size, unk;
        GX2CalcDRCSize(
            GX2_DRC_RENDER_MODE_SINGLE,           // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE,            // Two buffers for double-buffering
            &drc_scan_buffer_size,                // Output byte size
            &unk                                  // Unknown; seems like we have no use for it
        );

        // Allocate DRC scan buffer
        gDrcScanBuffer = MEMAllocFromFrmHeapEx(
            gFgHeapHandle,
            drc_scan_buffer_size,
            GX2_SCAN_BUFFER_ALIGNMENT // Required alignment
        );

        if (!gDrcScanBuffer)
        {
            exit_glstuff();
            return;
        }

        // Flush allocated buffer from CPU cache
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, gDrcScanBuffer, drc_scan_buffer_size);

        // Set the current DRC scan buffer
        GX2SetDRCBuffer(
            gDrcScanBuffer,                       // Scan Buffer
            drc_scan_buffer_size,                 // Scan Buffer Size
            GX2_DRC_RENDER_MODE_SINGLE,           // Render Mode
            GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, // Scan Buffer Surface Format
            GX2_BUFFERING_MODE_DOUBLE             // Enable double-buffering
        );

        // Set the current DRC scan buffer dimensions
        GX2SetDRCScale(drc_width, drc_height);
    }
    
    // Initialize color buffer
    gColorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    gColorBuffer.surface.width = fb_width;
    gColorBuffer.surface.height = fb_height;
    gColorBuffer.surface.depth = 1;
    gColorBuffer.surface.mipLevels = 1;
    gColorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
    gColorBuffer.surface.aa = GX2_AA_MODE1X;
    gColorBuffer.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    gColorBuffer.surface.mipmaps = NULL;
    gColorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    gColorBuffer.surface.swizzle  = 0;
    gColorBuffer.viewMip = 0;
    gColorBuffer.viewFirstSlice = 0;
    gColorBuffer.viewNumSlices = 1;
    GX2CalcSurfaceSizeAndAlignment(&gColorBuffer.surface);
    GX2InitColorBufferRegs(&gColorBuffer);
    
    // Allocate color buffer data
    gColorBufferImageData = MEMAllocFromFrmHeapEx(
        gMEM1HeapHandle,
        gColorBuffer.surface.imageSize, // Data byte size
        gColorBuffer.surface.alignment  // Required alignment
    );

    if (!gColorBufferImageData)
    {
        exit_glstuff();
        return;
    }


    gColorBuffer.surface.image = gColorBufferImageData;

    // Flush allocated buffer from CPU cache
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, gColorBufferImageData, gColorBuffer.surface.imageSize);

    // Initialize depth buffer
    gDepthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    gDepthBuffer.surface.width = fb_width;
    gDepthBuffer.surface.height = fb_height;
    gDepthBuffer.surface.depth = 1;
    gDepthBuffer.surface.mipLevels = 1;
    gDepthBuffer.surface.format = GX2_SURFACE_FORMAT_FLOAT_R32;
    gDepthBuffer.surface.aa = GX2_AA_MODE1X;
    gDepthBuffer.surface.use = GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_DEPTH_BUFFER;
    gDepthBuffer.surface.mipmaps = NULL;
    gDepthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
    gDepthBuffer.surface.swizzle  = 0;
    gDepthBuffer.viewMip = 0;
    gDepthBuffer.viewFirstSlice = 0;
    gDepthBuffer.viewNumSlices = 1;
    gDepthBuffer.hiZPtr = NULL;
    gDepthBuffer.hiZSize = 0;
    gDepthBuffer.depthClear = 1.0f;
    gDepthBuffer.stencilClear = 0;
    GX2CalcSurfaceSizeAndAlignment(&gDepthBuffer.surface);
    GX2InitDepthBufferRegs(&gDepthBuffer);

    // Allocate depth buffer data
    gDepthBufferImageData = MEMAllocFromFrmHeapEx(
        gMEM1HeapHandle,
        gDepthBuffer.surface.imageSize, // Data byte size
        gDepthBuffer.surface.alignment  // Required alignment
    );

    if (!gDepthBufferImageData)
    {
        exit_glstuff();
        return;
    }

    gDepthBuffer.surface.image = gDepthBufferImageData;

    // Flush allocated buffer from CPU cache
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, gDepthBufferImageData, gDepthBuffer.surface.imageSize);

    // Allocate context state instance
    gContext = (GX2ContextState*)MEMAllocFromDefaultHeapEx(
        sizeof(GX2ContextState),    // Size of context
        GX2_CONTEXT_STATE_ALIGNMENT // Required alignment
    );

    if (!gContext)
    {
        exit_glstuff();
        return;
    }

    // Initialize it to default state
    GX2SetupContextStateEx(gContext, false);

    GX2SetContextState(gContext);
    GX2SetColorBuffer(&gColorBuffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&gDepthBuffer);

    GX2SetSwapInterval(1);

    // Scissor test is always enabled in GX2

    // Set the default viewport and scissor
    GX2SetViewport(0, 0, fb_width, fb_height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, fb_width, fb_height);

    // Disable depth test
    GX2SetDepthOnlyControl(
        FALSE,                  // Depth Test;     equivalent to glDisable(GL_DEPTH_TEST)
        FALSE,                  // Depth Write;    equivalent to glDepthMask(GL_FALSE)
        GX2_COMPARE_FUNC_LEQUAL // Depth Function; equivalent to glDepthFunc(GL_LEQUAL)
    );

    if (pWidth)
        *pWidth = fb_width;
    if (pHeight)
        *pHeight = fb_height;
    
    streamRingBuffer = MEMAllocFromDefaultHeapEx(STREAM_BUFFER_MAX_SIZE, GX2_INDEX_BUFFER_ALIGNMENT);
    if (streamRingBuffer) {
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, streamRingBuffer, STREAM_BUFFER_MAX_SIZE);
    }

    ProcUIInit(&ProcUIDrawDoneRelease);
    gIsRunning = true;
    WHBMountSdCard();

    if(!GLSL_Init()) {
        OSReport("GLSL_Init failed!\n");
        exit_glstuff();
        return;
    }
}

void exit_glstuff(){
    if (gCmdlist)
    {
        MEMFreeToDefaultHeap(gCmdlist);
        gCmdlist = NULL;
    }

    if (gContext)
    {
        MEMFreeToDefaultHeap(gContext);
        gContext = NULL;
    }

    if (gIsRunning)
        ProcUIDrawDoneRelease();

    if (gFgHeapHandle)
    {
        MEMFreeToFrmHeap(gFgHeapHandle, MEM_FRM_HEAP_FREE_ALL);
        gFgHeapHandle = NULL;
    }
    gTvScanBuffer = NULL;
    gDrcScanBuffer = NULL;

    if (gMEM1HeapHandle)
    {
        MEMFreeToFrmHeap(gMEM1HeapHandle, MEM_FRM_HEAP_FREE_ALL);
        gMEM1HeapHandle = NULL;
    }
    gColorBufferImageData = NULL;
    gDepthBufferImageData = NULL;

    GLSL_Shutdown();
    WHBUnmountSdCard();
}

bool WindowIsRunning() {
    ProcUIStatus status = ProcUIProcessMessages(true);
    if (status == PROCUI_STATUS_EXITING) {
        gIsRunning = false;
        return false;
    }
    if (status == PROCUI_STATUS_RELEASE_FOREGROUND) {
        ProcUIDrawDoneRelease();
        return true;
    }
    return gIsRunning;
}

void WindowMakeContextCurrent() {
    GX2SetContextState(gContext);
    GX2SetColorBuffer(&gColorBuffer, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&gDepthBuffer);
}

void WindowSwapBuffers()
{
    // Make sure to flush all commands to GPU before copying the color buffer to the scan buffers
    // (Calling GX2DrawDone instead here causes slow downs)
    GX2Flush();

    streamBufferOffset = 0;
    
    // Copy the color buffer to the TV and DRC scan buffers
    GX2CopyColorBufferToScanBuffer(&gColorBuffer, GX2_SCAN_TARGET_TV);
    GX2CopyColorBufferToScanBuffer(&gColorBuffer, GX2_SCAN_TARGET_DRC);
    // Flip
    GX2SwapScanBuffers();

    // Reset context state for next frame
    GX2SetContextState(gContext);

    // Flush all commands to GPU before GX2WaitForFlip since it will block the CPU
    GX2Flush();

    // Make sure TV and DRC are enabled
    GX2SetTVEnable(true);
    GX2SetDRCEnable(true);

    // Wait until swapping is done
    GX2WaitForFlip();
}

GX2ColorBuffer* WindowGetColorBuffer()
{
    return &gColorBuffer;
}

GX2DepthBuffer* WindowGetDepthBuffer()
{
    return &gDepthBuffer;
}

void glGenTextures(GLsizei n, GLuint *textures){
    if (n < 0 || !textures) return;

    for (int i = 0; i < n; i++) {
        GLuint allocatedId = 0;

        for (int id = 1; id < Texture_list.size(); id++) {
            if (!Texture_list[id].isGenerated) {
                allocatedId = (GLuint)id;
                break;
            }
        }

        if (allocatedId == 0) {
            gl_Texture newTexture;
            Texture_list.push_back(newTexture);
            allocatedId = (GLuint)(Texture_list.size() - 1);
        }

        Texture_list[allocatedId].isGenerated = true;

        textures[i] = allocatedId;
    }
}

void glActiveTexture(GLenum texture) {
    int unitIndex = texture - GL_TEXTURE0;

    // Guard against out-of-bounds mapping
    if (unitIndex >= 0 && unitIndex < 16) {
        glstuff.activeTextureUnit = unitIndex;
    }
}

void glBindTexture(GLenum target, GLuint texture){
    if(target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) return;

    if (texture != 0 && (texture >= Texture_list.size() || !Texture_list[texture].isGenerated)) {
        return;
    }

    if (texture == 0) {
        GLuint previousTexID = glstuff.textureUnits[glstuff.activeTextureUnit];
        if (previousTexID != 0 && previousTexID < Texture_list.size()) {
            Texture_list[previousTexID].isBound = false;
        }

        glstuff.textureUnits[glstuff.activeTextureUnit] = 0;

        if (glstuff.activeTextureUnit == 0) {
            glstuff.currentTexture = 0;
        }
    }
    else {
        GLuint previousTexID = glstuff.textureUnits[glstuff.activeTextureUnit];
        if (previousTexID != 0 && previousTexID < Texture_list.size()) {
            Texture_list[previousTexID].isBound = false;
        }

        glstuff.textureUnits[glstuff.activeTextureUnit] = texture;
        
        Texture_list[texture].target = target;
        Texture_list[texture].isBound = true;

        if (glstuff.activeTextureUnit == 0) {
            glstuff.currentTexture = texture;
        }
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param){
    if(target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) return;
    if(pname != GL_TEXTURE_MIN_FILTER && pname != GL_TEXTURE_MAG_FILTER && pname != GL_TEXTURE_WRAP_S && pname != GL_TEXTURE_WRAP_T) return;
    
    GLuint activeTexID = glstuff.textureUnits[glstuff.activeTextureUnit];
    if(activeTexID == 0) return;

    gl_Texture& t = Texture_list[activeTexID];
    switch(pname){
        case GL_TEXTURE_MIN_FILTER:
            if(param != GL_NEAREST && param != GL_LINEAR && param != GL_NEAREST_MIPMAP_NEAREST && pname != GL_LINEAR_MIPMAP_NEAREST && param != GL_NEAREST_MIPMAP_LINEAR && param != GL_LINEAR_MIPMAP_LINEAR)
                return;
            
            t.TextureMinFilter = param;
            break;
        
        case GL_TEXTURE_MAG_FILTER:
            if(param != GL_NEAREST && param != GL_LINEAR)
                return;
            
            t.TextureMagFilter = param;
            break;
        
        case GL_TEXTURE_WRAP_S:
            if(param != GL_CLAMP_TO_EDGE && param != GL_MIRRORED_REPEAT && param != GL_REPEAT)
                return;
            
            t.TextureWrapS = param;
            break;
        
        case GL_TEXTURE_WRAP_T:
            if(param != GL_CLAMP_TO_EDGE && param != GL_MIRRORED_REPEAT && param != GL_REPEAT)
                return;
            
            t.TextureWrapT = param;
            break;
    }
}

GX2SurfaceFormat GLformattoGX2(GLint format, GLenum type){
    if(type == GL_UNSIGNED_BYTE){
        if(format == GL_RGBA || format == GL_RGB)
            return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
        
        return GX2_SURFACE_FORMAT_UNORM_R8;
    }

    if(type == GL_UNSIGNED_SHORT_5_6_5) return GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
    if(type == GL_UNSIGNED_SHORT_4_4_4_4) return GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
    if(type == GL_UNSIGNED_SHORT_5_5_5_1) return GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data){
    if(target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP) return;
    if(internalFormat != format) return;
    if(border != 0) return;
    
    if(target == GL_TEXTURE_2D){
        gl_Texture& tex = Texture_list[glstuff.currentTexture];
        

        if (tex.texture != nullptr) {
            if (tex.texture->surface.image) {
                MEMFreeToDefaultHeap(tex.texture->surface.image);
                tex.texture->surface.image = nullptr;
            }
            MEMFreeToDefaultHeap(tex.texture);
            tex.texture = nullptr;
        }
        tex.texture = (GX2Texture*)MEMAllocFromDefaultHeap(sizeof(GX2Texture));
        memset(tex.texture, 0, sizeof(GX2Texture));

        tex.texture->surface.width = width;
        tex.texture->surface.height = height;
        tex.texture->surface.mipLevels = level + 1;
        tex.texture->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
        tex.texture->surface.depth = 1;
        tex.texture->surface.format = GLformattoGX2(internalFormat, type);
        tex.texture->surface.aa = GX2_AA_MODE1X;
        tex.texture->surface.use = GX2_SURFACE_USE_TEXTURE;
        tex.texture->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        tex.texture->surface.swizzle = 0;
        tex.texture->viewFirstMip = 0;
        tex.texture->viewNumMips = 1;
        tex.texture->viewFirstSlice = 0;
        tex.texture->viewNumSlices = 1;
        tex.texture->compMap = 0x00010203;
        
        GX2CalcSurfaceSizeAndAlignment(&tex.texture->surface);
        tex.texture->surface.image = memalign(tex.texture->surface.alignment, tex.texture->surface.imageSize);
        if (!tex.texture->surface.image) {
            WHBLogPrintf("GX2 Allocation failed for Texture ID %d", glstuff.currentTexture);
            return;
        }
        memset(tex.texture->surface.image, 0, tex.texture->surface.imageSize);

        GX2InitTextureRegs(tex.texture);

        if (data) {
            int numbytes = 4;

            if(tex.texture->surface.format == GX2_SURFACE_FORMAT_UNORM_R8)
                numbytes = 1;
            if(tex.texture->surface.format == GX2_SURFACE_FORMAT_UNORM_R5_G6_B5)
                numbytes = 2;


            uint32_t src_stride = width * numbytes;
            uint32_t dst_stride = tex.texture->surface.pitch * numbytes; 

            for (uint32_t y = 0; y < height; y++) {
                const uint8_t* src_row = (const uint8_t*)data + (y * src_stride);
                uint8_t* dst_row = (uint8_t*)tex.texture->surface.image + (y * dst_stride);
                
                // Copy exactly the pixel width data, ignoring the alignment padding at the end of dst_row
                std::memcpy(dst_row, src_row, src_stride);
            }
        } 
        else {
            std::memset(tex.texture->surface.image, 0, tex.texture->surface.imageSize);
        }

        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, tex.texture->surface.image, tex.texture->surface.imageSize);
    }
}

void glGenerateMipmap(GLenum target){
    
}

void glGenBuffers(GLsizei n, GLuint* buffers){
    if (n < 0 || !buffers) return;

    for (int i = 0; i < n; i++) {
        GLuint allocatedId = 0;

        for (int id = 1; id < Buffer_list.size(); id++) {
            if (!Buffer_list[id].isGenerated) {
                allocatedId = (GLuint)id;
                break;
            }
        }

        if (allocatedId == 0) {
            gl_Buffer newBuffer;
            Buffer_list.push_back(newBuffer);
            allocatedId = (GLuint)(Buffer_list.size() - 1);
        }

        Buffer_list[allocatedId].isGenerated = true;

        buffers[i] = allocatedId;
    }
}

void glBindBuffer(GLenum target, GLuint buffer){
    if(target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) return;

    if (buffer != 0 && (buffer >= Buffer_list.size() || !Buffer_list[buffer].isGenerated)) {
        return;
    }

    if(buffer == 0){
        if(target == GL_ARRAY_BUFFER){
            glstuff.currentVBO = 0;
        }
        else{
            glstuff.currentEBO = 0;
        }
    }
    else{
        if(Buffer_list[glstuff.currentVBO].isBound)
            Buffer_list[glstuff.currentVBO].isBound = false;
        
        if(target == GL_ARRAY_BUFFER){
            glstuff.currentVBO = buffer;
            Buffer_list[buffer].target = GL_ARRAY_BUFFER;
        }
        else{
            glstuff.currentEBO = buffer;
            Buffer_list[buffer].target = GL_ELEMENT_ARRAY_BUFFER;
        }

        Buffer_list[buffer].isBound = true;
    }
}

void glBufferData(GLenum target, GLsizei size, const GLvoid *data, GLenum usage){
    if(target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) return;

    if(usage != GL_STREAM_DRAW && usage != GL_STATIC_DRAW && usage != GL_DYNAMIC_DRAW) return;


    int curr_buffer = 0;
    uint32_t requiredAlignment = GX2_VERTEX_BUFFER_ALIGNMENT;

    if(target == GL_ARRAY_BUFFER) {
        curr_buffer = glstuff.currentVBO;
        requiredAlignment = GX2_VERTEX_BUFFER_ALIGNMENT;
    } else {
        curr_buffer = glstuff.currentEBO;
        requiredAlignment = GX2_INDEX_BUFFER_ALIGNMENT;
    }

    if(!curr_buffer || size <= 0) return;

    


    if(Buffer_list[curr_buffer].data && Buffer_list[curr_buffer].usage != GL_STREAM_DRAW) {
        MEMFreeToDefaultHeap(Buffer_list[curr_buffer].data);
    }
    Buffer_list[curr_buffer].data = nullptr;

    void* gpuData = nullptr;

    if (usage == GL_STREAM_DRAW) {
        // Aligned sizing step
        uint32_t alignedSize = (size + (requiredAlignment - 1)) & ~(requiredAlignment - 1);

        // Check if we have room left in our transient scratch pad for this frame
        if (streamBufferOffset + alignedSize <= STREAM_BUFFER_MAX_SIZE) {
            // Slice memory out of the pre-allocated ring buffer
            gpuData = (void*)((uintptr_t)streamRingBuffer + (uintptr_t)streamBufferOffset);
            streamBufferOffset += alignedSize; // Push the offset down the track
        } else {
            // Fallback to standard heap if a massive buffer overflows our frame buffer limits
            gpuData = MEMAllocFromDefaultHeapEx(size, requiredAlignment);
        }
    } 
    else if (usage == GL_STATIC_DRAW) {
        // Static buffers get dedicated long-term allocations from the hardware device heap
        gpuData = MEMAllocFromDefaultHeapEx(size, requiredAlignment);
    } 
    else if (usage == GL_DYNAMIC_DRAW) {
        gpuData = MEMAllocFromDefaultHeapEx(size, requiredAlignment);
    }
    

    if(!gpuData) return;

    if(data != nullptr) {
        memcpy(gpuData, data, size);
        
        // Push CPU cache modifications directly down onto the physical device bus line
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, gpuData, size);
    }

    Buffer_list[curr_buffer].data = gpuData;
    Buffer_list[curr_buffer].size = size;
    Buffer_list[curr_buffer].usage = usage;
}

GLuint glCreateShader(GLenum shaderType){
    if (shaderType != GL_VERTEX_SHADER && shaderType != GL_FRAGMENT_SHADER) return 0;

    GLuint allocatedId = 0;

    for (int id = 1; id < Shader_list.size(); id++) {
        if (!Shader_list[id].isGenerated) {
            allocatedId = (GLuint)id;
            break;
        }
    }

    if (allocatedId == 0) {
        gl_Shader newShader;
        Shader_list.push_back(newShader);
        allocatedId = (GLuint)(Shader_list.size() - 1);
    }

    if (shaderType == GL_VERTEX_SHADER)
        Shader_list[allocatedId].type = ShaderType::VERTEX;
    else
        Shader_list[allocatedId].type = ShaderType::FRAGMENT;
    
    Shader_list[allocatedId].isGenerated = true;

    return allocatedId;
}

void glShaderSource(GLuint handle, GLsizei count, const GLchar *const *string, const GLint *length){
    if (handle >= Shader_list.size() || !Shader_list[handle].isGenerated) return;

    std::string fullSource = "";

    for(int i = 0; i < count; i++){
        if(length && length[i] > 0)
            fullSource.append(string[i], length[i]);
        else
            fullSource.append(string[i]);
    }

    Shader_list[handle].source = fullSource;
}

void glCompileShader(GLuint shader){
    if (shader >= Shader_list.size() || !Shader_list[shader].isGenerated) return;

    char outputBuff[1024];
    if(Shader_list[shader].type == ShaderType::FRAGMENT){
        Shader_list[shader].internal_Shader.pixelshader = GLSL_CompilePixelShader(Shader_list[shader].source.c_str(), outputBuff, sizeof(outputBuff), GLSL_COMPILER_FLAG_NONE);
        Shader_list[shader].isCompiled = (Shader_list[shader].internal_Shader.pixelshader != nullptr);
    }
    else{
        Shader_list[shader].internal_Shader.vertexshader = GLSL_CompileVertexShader(Shader_list[shader].source.c_str(), outputBuff, sizeof(outputBuff), GLSL_COMPILER_FLAG_NONE);
        Shader_list[shader].isCompiled = (Shader_list[shader].internal_Shader.vertexshader != nullptr);
    }

    WHBLogPrintf("compile result: isCompiled=%d outputBuff=%s", 
    Shader_list[shader].isCompiled, outputBuff);
}

GLuint glCreateProgram(){
    GLuint allocatedId = 0;

    for (int id = 1; id < Program_list.size(); id++) {
        if (!Program_list[id].isGenerated) {
            allocatedId = (GLuint)id;
            break;
        }
    }

    if (allocatedId == 0) {
        gl_Program newProgram;
        Program_list.push_back(newProgram);
        allocatedId = (GLuint)(Program_list.size() - 1);
    }

    Program_list[allocatedId].isGenerated = true;

    return allocatedId;
}

void glAttachShader(GLuint prog, GLuint shad){
    if (prog >= Program_list.size() || !Program_list[prog].isGenerated) return;
    if (shad >= Shader_list.size() || !Shader_list[shad].isGenerated) return;

    gl_Program& p = Program_list[prog];
    gl_Shader& s = Shader_list[shad];

    if(s.type == ShaderType::VERTEX){
        p.vertexShaderID = shad;
    }
    else{
        p.pixelShaderID = shad;
    }
}

void glLinkProgram(GLuint progr){
    if (progr >= Program_list.size() || !Program_list[progr].isGenerated) return;

    gl_Program& p = Program_list[progr];
    p.isLinked = false;
    if(p.vertexShaderID == 0 || p.pixelShaderID == 0) return;
    if (!Shader_list[p.vertexShaderID].isCompiled || !Shader_list[p.pixelShaderID].isCompiled) return;

    p.vertexShader = Shader_list[p.vertexShaderID].internal_Shader.vertexshader;
    p.pixelShader = Shader_list[p.pixelShaderID].internal_Shader.pixelshader;

    if (!p.vertexShader || !p.pixelShader) {
        p.isLinked = false;
        return;
    }

    std::unordered_map<std::string, size_t> st;
    p.uniforms.reserve(p.vertexShader->uniformVarCount + p.pixelShader->uniformVarCount);
    for (int i = 0; i < p.vertexShader->uniformVarCount; i++) {
        gl_Uniform u;
        u.name = std::string(p.vertexShader->uniformVars[i].name);
        u.vertexUniform = &p.vertexShader->uniformVars[i];
        u.location = p.uniforms.size();
        
        p.uniforms.push_back(u);
        st[u.name] = p.uniforms.size() - 1; 
    }

    for (int i = 0; i < p.pixelShader->uniformVarCount; i++) {
        std::string name = std::string(p.pixelShader->uniformVars[i].name);

        if (st.count(name)) {
            size_t index = st[name];
            p.uniforms[index].pixelUniform = &p.pixelShader->uniformVars[i];
            continue;
        }
        
        gl_Uniform u;
        u.name = name;
        u.pixelUniform = &p.pixelShader->uniformVars[i];
        u.location = p.uniforms.size();
        
        p.uniforms.push_back(u);
        st[u.name] = p.uniforms.size() - 1;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, p.vertexShader->program, p.vertexShader->size);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, p.pixelShader->program, p.pixelShader->size);
    
    p.isLinked = true;
}

void glUseProgram(GLuint program){
    if (program != 0 && (program >= Program_list.size() || !Program_list[program].isGenerated || !Program_list[program].isLinked)) return;
    
    glstuff.currentProgram = program;
    Program_list[glstuff.currentProgram].uniformsDirty = true;
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer){
    if(index > 15) return;

    if(size < 1 || size > 4) return;

    if(type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT && type != GL_UNSIGNED_SHORT && type != GL_FIXED && type != GL_FLOAT) return;

    attribState[index].vbo = glstuff.currentVBO;
    attribState[index].size = size;
    attribState[index].type = type;
    attribState[index].normalized = normalized;
    attribState[index].stride = stride;
    attribState[index].offset = (uintptr_t)pointer;
}

void glEnableVertexAttribArray(GLuint index){
    if (index > 15) return;

    attribState[index].enabled = true;
}

GLint glGetUniformLocation(GLuint prog, const GLchar *name) {
    if (prog >= Program_list.size() || !Program_list[prog].isGenerated)
        return -1;
    
    if (!Program_list[prog].isLinked)
        return -1;

    gl_Program program = Program_list[prog];

    for (int i = 0; i < program.uniforms.size(); i++) {
        if (strcmp(name, program.uniforms[i].name.c_str()) == 0) {
            return i;
        }
    }

    return -1;
}

void glUniform1i(GLint location, GLint v0){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLint);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);

    prog.uniformsDirty = true;
}

void glUniform2i(GLint location, GLint v0, GLint v1){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLint);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize, &v1, actualSize);

    prog.uniformsDirty = true;
}

void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLint);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize, &v1, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize * 2, &v2, actualSize);

    prog.uniformsDirty = true;
}

void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLint);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize, &v1, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize * 2, &v2, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize * 3, &v3, actualSize);

    prog.uniformsDirty = true;
}

void glUniform1f(GLint location, GLfloat v0){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLfloat);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);

    prog.uniformsDirty = true;
}

void glUniform2f(GLint location, GLfloat v0, GLfloat v1){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLfloat);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize, &v1, actualSize);

    prog.uniformsDirty = true;
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLfloat);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize, &v1, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize * 2, &v2, actualSize);

    prog.uniformsDirty = true;
}

void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    size_t actualSize = sizeof(GLfloat);
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), &v0, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize, &v1, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize * 2, &v2, actualSize);
    std::memcpy(prog.uniforms[location].data.data() + actualSize * 3, &v3, actualSize);

    prog.uniformsDirty = true;
}

void glUniform1iv(GLint location, GLsizei count, const GLint *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLint) * count;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform2iv(GLint location, GLsizei count, const GLint *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLint) * count * 2;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform3iv(GLint location, GLsizei count, const GLint *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLint) * count * 3;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform4iv(GLint location, GLsizei count, const GLint *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLint) * count * 4;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform1fv(GLint location, GLsizei count, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform2fv(GLint location, GLsizei count, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count * 2;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count * 3;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count * 4;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    if(transpose != GL_FALSE)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count * 4;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    if(transpose != GL_FALSE)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count * 9;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value){
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (!prog.isLinked)
        return;
    
    if(location >= prog.uniforms.size())
        return;
    
    if(count < 1)
        return;
    
    if(transpose != GL_FALSE)
        return;
    
    size_t actualSize = sizeof(GLfloat) * count * 16;
    
    size_t alignedSize = (actualSize + 15) & ~15;

    prog.uniforms[location].data.assign(alignedSize, 0);

    std::memcpy(prog.uniforms[location].data.data(), value, actualSize);

    prog.uniformsDirty = true;
}


bool SetupVertexStateAndShaders(void*& fetchCode) {
    int activeStreamsCount = 0;
    GX2AttribStream streams[16];

    for (int i = 0; i < 16; i++) {
        if (attribState[i].enabled) {
            GX2AttribStream& stream = streams[activeStreamsCount];
            stream.location = i;
            stream.buffer = activeStreamsCount;
            stream.offset = (uintptr_t)attribState[i].offset;
            
            GX2AttribFormat format;


            if(attribState[i].size == 1){
                if(attribState[i].type == GL_BYTE || attribState[i].type == GL_UNSIGNED_BYTE) format = GX2_ATTRIB_TYPE_8;
                if(attribState[i].type == GL_SHORT || attribState[i].type == GL_UNSIGNED_SHORT) format = GX2_ATTRIB_TYPE_16;
                if(attribState[i].type == GL_FIXED) format = GX2_ATTRIB_TYPE_32;
                if(attribState[i].type == GL_FLOAT) format = GX2_ATTRIB_TYPE_32_FLOAT;
            }
            else if(attribState[i].size == 2){
                if(attribState[i].type == GL_BYTE || attribState[i].type == GL_UNSIGNED_BYTE) format = GX2_ATTRIB_TYPE_8_8;
                if(attribState[i].type == GL_SHORT || attribState[i].type == GL_UNSIGNED_SHORT) format = GX2_ATTRIB_TYPE_16_16;
                if(attribState[i].type == GL_FIXED) format = GX2_ATTRIB_TYPE_32_32;
                if(attribState[i].type == GL_FLOAT) format = GX2_ATTRIB_TYPE_32_32_FLOAT;
            }
            else if(attribState[i].size == 3 && (attribState[i].type == GL_FIXED ||attribState[i].type == GL_FLOAT)){
                if(attribState[i].type == GL_FIXED) format = GX2_ATTRIB_TYPE_32_32_32;
                if(attribState[i].type == GL_FLOAT) format = GX2_ATTRIB_TYPE_32_32_32_FLOAT;
            }
            else{
                if(attribState[i].type == GL_BYTE || attribState[i].type == GL_UNSIGNED_BYTE) format = GX2_ATTRIB_TYPE_8_8_8_8;
                if(attribState[i].type == GL_SHORT || attribState[i].type == GL_UNSIGNED_SHORT) format = GX2_ATTRIB_TYPE_16_16_16_16;
                if(attribState[i].type == GL_FIXED) format = GX2_ATTRIB_TYPE_32_32_32_32;
                if(attribState[i].type == GL_FLOAT) format = GX2_ATTRIB_TYPE_32_32_32_32_FLOAT;
            }

            if(attribState[i].type != GL_FLOAT && attribState[i].type != GL_FIXED){
                if(attribState[i].normalized)
                    format |= GX2_ATTRIB_FLAG_SCALED;
                else
                    format |= GX2_ATTRIB_FLAG_INTEGER;
            } else {
                format |= GX2_ATTRIB_FLAG_SCALED;
            }

            if(attribState[i].type == GL_BYTE || attribState[i].type == GL_SHORT) format |= GX2_ATTRIB_FLAG_SIGNED;

            stream.format = (GX2AttribFormat)format;
            
            
            stream.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
            stream.type = GX2_ATTRIB_INDEX_PER_VERTEX;
            stream.aluDivisor = 0;

            if(attribState[i].size == 1) stream.mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
            else if(attribState[i].size == 2) stream.mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
            else if(attribState[i].size == 3) stream.mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1);
            else if(attribState[i].size == 4) stream.mask = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);

            GX2SetAttribBuffer(activeStreamsCount, Buffer_list[attribState[i].vbo].size, attribState[i].stride, Buffer_list[attribState[i].vbo].data);
        
            activeStreamsCount++;
        }
    }


    if (activeStreamsCount == 0) return false;

    uint32_t fetchSize = GX2CalcFetchShaderSizeEx(activeStreamsCount, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    
    fetchCode = MEMAllocFromDefaultHeapEx(fetchSize, GX2_SHADER_PROGRAM_ALIGNMENT);
    if (!fetchCode) {
        WHBLogPrintf("Failed to allocate fetch shader memory!");
        return false;
    }
    
    GX2FetchShader FetchShader;
    GX2InitFetchShaderEx(&FetchShader, (uint8_t*)fetchCode, activeStreamsCount, streams, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, FetchShader.program, FetchShader.size);

    GX2SetShaderModeEx(GX2_SHADER_MODE_UNIFORM_REGISTER, 48, 64, 0, 0, 200, 192);
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
    GX2SetFetchShader(&FetchShader);

    gl_Program& prog = Program_list[glstuff.currentProgram];
    
    if (!prog.isLinked) {
        MEMFreeToDefaultHeap(fetchCode);
        return false;
    }
    
    GX2SetVertexShader(prog.vertexShader);
    GX2SetPixelShader(prog.pixelShader);

    if (prog.pixelShader != nullptr) {
        for (uint32_t i = 0; i < prog.pixelShader->samplerVarCount; i++) {
            const GX2SamplerVar& sVar = prog.pixelShader->samplerVars[i];
            
            // 1. Find what slot index the user bound via glUniform1i
            int textureUnitSlot = 0; 
            for (const auto& u : prog.uniforms) {
                if (u.name == sVar.name && !u.data.empty()) {
                    std::memcpy(&textureUnitSlot, u.data.data(), sizeof(int));
                    break;
                }
            }

            // 2. Look up which texture ID is on that unit slot
            GLuint boundTexID = glstuff.textureUnits[textureUnitSlot];
            if (boundTexID == 0) continue; // Safely skip if nothing is bound to this unit

            gl_Texture& tex = Texture_list[boundTexID];
            if (tex.texture == nullptr || tex.texture->surface.image == nullptr) continue;

            // 3. Configure the GX2 Sampler state
            GX2Sampler sampler;
            std::memset(&sampler, 0, sizeof(GX2Sampler));

            GX2TexXYFilterMode minxyFilter = (tex.TextureMinFilter == GL_NEAREST) ? GX2_TEX_XY_FILTER_MODE_POINT : GX2_TEX_XY_FILTER_MODE_LINEAR;
            GX2TexXYFilterMode magxyFilter = (tex.TextureMagFilter == GL_NEAREST) ? GX2_TEX_XY_FILTER_MODE_POINT : GX2_TEX_XY_FILTER_MODE_LINEAR;

            GX2TexClampMode clampS = GX2_TEX_CLAMP_MODE_WRAP;
            if (tex.TextureWrapS == GL_CLAMP_TO_EDGE)    clampS = GX2_TEX_CLAMP_MODE_CLAMP;
            if (tex.TextureWrapS == GL_MIRRORED_REPEAT) clampS = GX2_TEX_CLAMP_MODE_MIRROR;

            GX2TexClampMode clampT = GX2_TEX_CLAMP_MODE_WRAP;
            if (tex.TextureWrapT == GL_CLAMP_TO_EDGE)    clampT = GX2_TEX_CLAMP_MODE_CLAMP;
            if (tex.TextureWrapT == GL_MIRRORED_REPEAT) clampT = GX2_TEX_CLAMP_MODE_MIRROR;
            
            GX2InitSamplerXYFilter(&sampler, magxyFilter, minxyFilter, GX2_TEX_ANISO_RATIO_NONE);
            GX2InitSamplerZMFilter(&sampler, GX2_TEX_Z_FILTER_MODE_NONE, GX2_TEX_MIP_FILTER_MODE_NONE);
            GX2InitSamplerClamping(&sampler, clampS, clampT, clampT);

            // 4. Bind directly to the hardware-assigned register location
            uint32_t hardwareRegisterSlot = sVar.location;
            GX2SetPixelTexture(tex.texture, hardwareRegisterSlot);
            GX2SetPixelSampler(&sampler, hardwareRegisterSlot);
        }
    }
    return true;
}

void PrepareDraw() {
    gl_Program& prog = Program_list[glstuff.currentProgram];

    if (prog.uniformsDirty) {
        for (const auto& u : prog.uniforms) {
            if (u.vertexUniform != nullptr) {
                GX2SetVertexUniformReg(u.vertexUniform->offset, u.vertexUniform->count, u.data.data());
            }
            if (u.pixelUniform != nullptr) {
                GX2SetPixelUniformReg(u.pixelUniform->offset, u.pixelUniform->count, u.data.data());
            }
        }
    }

    prog.uniformsDirty = false; 
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count){
    if(mode != GL_POINTS && mode != GL_LINE_STRIP && mode != GL_LINE_LOOP && mode != GL_LINES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_TRIANGLES) { return; }
    if(first < 0) { return; }
    if(count < 0) { return; }
    void* fetchCode = nullptr;
    
    if(!SetupVertexStateAndShaders(fetchCode)) return;

    GX2PrimitiveMode pm;

    switch(mode){
        case GL_POINTS:
            pm = GX2_PRIMITIVE_MODE_POINTS;
            break;

        case GL_LINE_STRIP:
            pm = GX2_PRIMITIVE_MODE_LINE_STRIP;
            break;

        case GL_LINE_LOOP:
            pm = GX2_PRIMITIVE_MODE_LINE_LOOP;
            break;

        case GL_LINES:
            pm = GX2_PRIMITIVE_MODE_LINES;
            break;

        case GL_TRIANGLE_STRIP:
            pm = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP;
            break;

        case GL_TRIANGLE_FAN:
            pm = GX2_PRIMITIVE_MODE_TRIANGLE_FAN;
            break;

        case GL_TRIANGLES:
            pm = GX2_PRIMITIVE_MODE_TRIANGLES;
            break;
    }

    PrepareDraw();

    GX2DrawEx(pm, count, first, 1);
    GX2DrawDone();
    if (fetchCode) MEMFreeToDefaultHeap(fetchCode);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices){
    if(mode != GL_POINTS && mode != GL_LINE_STRIP && mode != GL_LINE_LOOP && mode != GL_LINES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_TRIANGLES) { return; }
    if(count < 1) return;
    if(type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT) return;
    
    uintptr_t rawIndexAddress = 0;
    void* finalIndexPointer;

    if(glstuff.currentEBO != 0){
        gl_Buffer& ebo = Buffer_list[glstuff.currentEBO];
        if (!ebo.data) return;

        rawIndexAddress = (uintptr_t)ebo.data + (uintptr_t)indices;
        finalIndexPointer = (void*)rawIndexAddress;

        if (type == GL_UNSIGNED_BYTE) {
            // 8-BIT UPSAMPLING WORKAROUND
            uint32_t requiredSpace = count * sizeof(uint16_t);
            uint32_t alignedSpace = (requiredSpace + (GX2_INDEX_BUFFER_ALIGNMENT - 1)) & ~(GX2_INDEX_BUFFER_ALIGNMENT - 1);

            
            if (streamBufferOffset + alignedSpace <= STREAM_BUFFER_MAX_SIZE) {
                uint16_t* upsampledBuffer = (uint16_t*)((uintptr_t)streamRingBuffer + streamBufferOffset);
                streamBufferOffset += alignedSpace;

                const uint8_t* srcBytes = (const uint8_t*)rawIndexAddress;
                for (GLsizei i = 0; i < count; i++) {
                    upsampledBuffer[i] = (uint16_t)srcBytes[i];
                }

                
                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, upsampledBuffer, requiredSpace);
                
                finalIndexPointer = (void*)upsampledBuffer;
            } 
            else return;
        }
    }
    else{
        if (!indices) return;
        rawIndexAddress = (uintptr_t)indices;
        finalIndexPointer = (void*)rawIndexAddress;

        if (type == GL_UNSIGNED_BYTE) {
            // --- 8-BIT UPSAMPLING WORKAROUND ---
            uint32_t requiredSpace = count * sizeof(uint16_t);
            uint32_t alignedSpace = (requiredSpace + (GX2_INDEX_BUFFER_ALIGNMENT - 1)) & ~(GX2_INDEX_BUFFER_ALIGNMENT - 1);

            
            if (streamBufferOffset + alignedSpace <= STREAM_BUFFER_MAX_SIZE) {
                uint16_t* upsampledBuffer = (uint16_t*)((uintptr_t)streamRingBuffer + streamBufferOffset);
                streamBufferOffset += alignedSpace;

                const uint8_t* srcBytes = (const uint8_t*)rawIndexAddress;
                for (GLsizei i = 0; i < count; i++) {
                    upsampledBuffer[i] = (uint16_t)srcBytes[i];
                }

                GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, upsampledBuffer, requiredSpace);
                finalIndexPointer = (void*)upsampledBuffer;
            } else return;
        }
    }


    void* fetchCode = nullptr;
    
    if(!SetupVertexStateAndShaders(fetchCode)) return;

    GX2PrimitiveMode pm;

    switch(mode){
        case GL_POINTS:
            pm = GX2_PRIMITIVE_MODE_POINTS;
            break;

        case GL_LINE_STRIP:
            pm = GX2_PRIMITIVE_MODE_LINE_STRIP;
            break;

        case GL_LINE_LOOP:
            pm = GX2_PRIMITIVE_MODE_LINE_LOOP;
            break;

        case GL_LINES:
            pm = GX2_PRIMITIVE_MODE_LINES;
            break;

        case GL_TRIANGLE_STRIP:
            pm = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP;
            break;

        case GL_TRIANGLE_FAN:
            pm = GX2_PRIMITIVE_MODE_TRIANGLE_FAN;
            break;

        case GL_TRIANGLES:
            pm = GX2_PRIMITIVE_MODE_TRIANGLES;
            break;
    }

    PrepareDraw();

    GX2DrawIndexedEx(pm, count, GX2_INDEX_TYPE_U16, finalIndexPointer, 0, 1);
    GX2DrawDone();
    if (fetchCode) MEMFreeToDefaultHeap(fetchCode);
}

GLint glGetAttribLocation(GLuint prog, const GLchar *name){
    if (prog >= Program_list.size() || !Program_list[prog].isGenerated)
        return 0;
    if (!Program_list[prog].isLinked)
        return 0;
        
    for(int i = 0; i < Program_list[prog].vertexShader->attribVarCount; i++){
        if(strcmp(name, Program_list[prog].vertexShader->attribVars[i].name) == 0){
            return Program_list[prog].vertexShader->attribVars[i].location;
        }
    }
    

    return 0;
}


void glClear(GLbitfield mask){
    if(mask & GL_COLOR_BUFFER_BIT)
        GX2ClearColor(WindowGetColorBuffer(), glstuff.clearRed, glstuff.clearGreen, glstuff.clearBlue, glstuff.clearAlpha);
    
    if(mask & GL_DEPTH_BUFFER_BIT)
        GX2ClearBuffersEx(nullptr, WindowGetDepthBuffer(), 0, 0, 0, 0, glstuff.clearDepth, 0, GX2_CLEAR_FLAGS_DEPTH);
    
    if(mask & GL_STENCIL_BUFFER_BIT)
        GX2ClearBuffersEx(nullptr, WindowGetDepthBuffer(), 0, 0, 0, 0, 0, glstuff.clearStencil, GX2_CLEAR_FLAGS_STENCIL);
    
    WindowMakeContextCurrent();
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha){
    glstuff.clearRed = std::clamp(red, 0.0f, 1.0f);
    glstuff.clearGreen = std::clamp(green, 0.0f, 1.0f);
    glstuff.clearBlue = std::clamp(blue, 0.0f, 1.0f);
    glstuff.clearAlpha = std::clamp(alpha, 0.0f, 1.0f);
}

void glClearDepth(GLdouble depth){
    glstuff.clearDepth = std::clamp((float)depth, 0.0f, 1.0f);
}

void glClearStencil(GLint s){
    glstuff.clearStencil = std::clamp(s, 0, 255);
}

/*void glViewport(int x, int y, int w, int h){
    GX2SetViewport(x, y, w, h, 0, 1);
    glstuff.window_width = w;
    glstuff.window_height = h;
}


void glEnable(GLenum cap) {
    switch(cap) {
        case GL_BLEND:
            glstuff.blend = TRUE;
            GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, glstuff.blend, TRUE);
            break;
        
        case GL_CULL_FACE:
            glstuff.cullface = TRUE;
            glCullFace(GL_BACK);
            break;

        case GL_DEPTH_TEST:
            glstuff.depthtest = TRUE;
            GX2SetDepthOnlyControl(glstuff.depthtest, glstuff.depthwrite, glstuff.depthfunc);
            break;
        
        case GL_DITHER:
            // Placeholder for GX2 dithering logic
            break;

        case GL_POLYGON_OFFSET_FILL:
            // Logic for GX2SetPolygonControl offset params
            glstuff.polygonoffset = TRUE;
            glstuff.polygonoffset_offset = 0;
            glstuff.polygonoffset_scale = 0;
            GX2SetPolygonOffset( 0, 0, 0, 0, 0 );
            break;

        case GL_SAMPLE_ALPHA_TO_COVERAGE:
            // Logic for GX2SetAlphaToMask
            GX2SetAlphaToMask(TRUE, GX2_ALPHA_TO_MASK_MODE_NON_DITHERED);
            break;

        case GL_SAMPLE_COVERAGE:
            // Logic for multisample mask control
            break;

        case GL_SCISSOR_TEST:
            // Logic to toggle scissor box usage
            break;

        case GL_STENCIL_TEST:
            // Logic for GX2SetStencilControl
            break;

        default:
            // Handle unknown or unsupported caps
            break;
    }
}


void glCullFace(GLenum mode) {
    BOOL cullFront = FALSE;
    BOOL cullBack = FALSE;

    if (glstuff.cullface) {
        if (mode == GL_FRONT) cullFront = TRUE;
        else if (mode == GL_BACK) cullBack = TRUE;
        else if (mode == GL_FRONT_AND_BACK) {
            cullFront = TRUE; 
            cullBack = TRUE;
        }
    }

    GX2SetCullOnlyControl(glstuff.frontFace, cullFront, cullBack);
}*/