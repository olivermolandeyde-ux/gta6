#pragma once

#include "core/Types.h"
#include "ecs/World.h"
#include "memory/FrameAllocator.h"

namespace engine {

struct MetalPbrUniforms {
    float modelMatrix[16];
    float viewMatrix[16];
    float projectionMatrix[16];
    float3 cameraPos;
    float  _pad0;
    float3 lightDir;
    float  lightIntensity;
    float3 albedoColor;
    float  roughness;
    float  metallic;
    float  time_s;
    float  _pad1[2];
};

struct MetalDrawPacket {
    u32 mesh_id;
    u32 index_count;
    MetalPbrUniforms uniforms;
};

struct MetalRenderer {
#if defined(__APPLE__)
    void* device;        // id<MTLDevice>
    void* commandQueue;  // id<MTLCommandQueue>
    void* shaderLibrary; // id<MTLLibrary>
    void* pbrPipeline;   // id<MTLRenderPipelineState>
    void* depthState;    // id<MTLDepthStencilState>
    void* metalLayer;    // CAMetalLayer*
    void* currentDrawable;
    void* vertexBuffer;
    void* indexBuffer;
    void* uniformBuffer;
    void* depthTexture;
#else
    void* device;
    void* commandQueue;
    void* shaderLibrary;
    void* pbrPipeline;
    void* depthState;
    void* metalLayer;
    void* currentDrawable;
    void* vertexBuffer;
    void* indexBuffer;
    void* uniformBuffer;
    u32   recorded_draws;
#endif
    int   width;
    int   height;
    float time_s;
    float3 cameraPos;
    float  cube_yaw;

    void init(void* windowHandle, int width, int height);
    void beginFrame();
    void renderScene(World& world, FrameAllocator& frame_alloc);
    void endFrame();
    void shutdown();
};

} // namespace engine
