#include "render/MetalRenderer.h"

#include "core/Assert.h"
#include "ecs/CommandBuffer.h"
#include "render/CubeMesh.h"
#include "render/GroundPlaneMesh.h"
#include "render/RenderPipeline.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Foundation/Foundation.h>
#endif

namespace engine {

static_assert(sizeof(MetalPbrUniforms) == 256, "Metal constant-buffer alignment");

namespace {

constexpr u32 kMaxDraws = 4;
constexpr float kFovY   = 1.04719755f;
constexpr float kZNear  = 0.05f;
constexpr float kZFar   = 250.f;

void mat_ident(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

void mat_rot_y(float* m, float a) {
    mat_ident(m);
    const float c = std::cos(a);
    const float s = std::sin(a);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
}

void mat_look(float* m, float3 eye, float3 target, float3 up) {
    float3 f = float3_normalize_or(float3_sub(target, eye), float3{0.f, 0.f, -1.f});
    float3 r = float3_normalize_or(float3_cross(f, up), float3{1.f, 0.f, 0.f});
    float3 u = float3_cross(r, f);
    mat_ident(m);
    m[0] = r.x;
    m[4] = r.y;
    m[8] = r.z;
    m[1] = u.x;
    m[5] = u.y;
    m[9] = u.z;
    m[2] = -f.x;
    m[6] = -f.y;
    m[10] = -f.z;
    m[12] = -float3_dot(r, eye);
    m[13] = -float3_dot(u, eye);
    m[14] = float3_dot(f, eye);
}

void mat_persp(float* m, float fovy, float aspect, float n, float fa) {
    std::memset(m, 0, 16 * sizeof(float));
    const float t = 1.f / std::tan(fovy * 0.5f);
    m[0] = t / aspect;
    m[5] = t;
    m[10] = (fa + n) / (n - fa);
    m[11] = -1.f;
    m[14] = (2.f * fa * n) / (n - fa);
}

float3 orbiting_sun_dir(float time_s) {
    const float a = time_s * 0.40f;
    const float3 to_sun =
        float3_normalize_or(float3{std::sin(a), 0.62f, std::cos(a)}, float3{0.f, 1.f, 0.f});
    return float3{-to_sun.x, -to_sun.y, -to_sun.z};
}

void fill_common(MetalPbrUniforms& u, float3 camera, float time_s, int width, int height) {
    mat_look(u.viewMatrix, camera, float3{0.f, 0.f, 0.f}, float3{0.f, 1.f, 0.f});
    mat_persp(u.projectionMatrix, kFovY, static_cast<float>(width) / max_of(1, height), kZNear, kZFar);
    u.cameraPos = camera;
    u._pad0 = 0.f;
    u.lightDir = orbiting_sun_dir(time_s);
    u.lightIntensity = 5.2f;
    u.time_s = time_s;
    u._pad1[0] = 0.f;
    u._pad1[1] = 0.f;
}

void fill_red_metal_cube(MetalPbrUniforms& u, float yaw, const float* pos, float3 camera,
                         float time_s, int width, int height) {
    mat_rot_y(u.modelMatrix, yaw);
    u.modelMatrix[12] = pos ? pos[0] : 0.f;
    u.modelMatrix[13] = pos ? pos[1] : 0.5f;
    u.modelMatrix[14] = pos ? pos[2] : 0.f;
    fill_common(u, camera, time_s, width, height);
    u.albedoColor = float3{0.8f, 0.2f, 0.2f};
    u.roughness = 0.4f;
    u.metallic = 0.8f;
}

void fill_concrete_ground(MetalPbrUniforms& u, const float* pos, float3 camera, float time_s,
                          int width, int height) {
    mat_ident(u.modelMatrix);
    if (pos) {
        u.modelMatrix[12] = pos[0];
        u.modelMatrix[13] = pos[1];
        u.modelMatrix[14] = pos[2];
    }
    fill_common(u, camera, time_s, width, height);
    u.albedoColor = float3{0.5f, 0.5f, 0.5f};
    u.roughness = 0.9f;
    u.metallic = 0.0f;
}

MetalDrawPacket* record_scene(World& world, FrameAllocator& frame_alloc, float3 camera, float yaw,
                              float time_s, int width, int height, u32& out_n) {
    MetalDrawPacket* packets = frame_alloc.allocate_array<MetalDrawPacket>(kMaxDraws);
    ENGINE_ASSERT(packets != nullptr, "frame draw packets");
    u32 n = 0;
    for (Entity e : world.query<RenderableComponent>()) {
        if (n >= kMaxDraws) {
            break;
        }
        const RenderableComponent* r = world.get<RenderableComponent>(e);
        const TransformComponent* t = world.get<TransformComponent>(e);
        if (!r) {
            continue;
        }
        MetalDrawPacket& p = packets[n++];
        p.mesh_id = r->mesh_id;
        const float* pos = t ? t->position : nullptr;
        if (r->mesh_id == 2) {
            p.index_count = kGroundIndexCount;
            fill_concrete_ground(p.uniforms, pos, camera, time_s, width, height);
        } else {
            p.index_count = kCubeIndexCount;
            fill_red_metal_cube(p.uniforms, yaw, pos, camera, time_s, width, height);
        }
    }
    if (n == 0) {
        MetalDrawPacket& cube = packets[n++];
        cube.mesh_id = 1;
        cube.index_count = kCubeIndexCount;
        fill_red_metal_cube(cube.uniforms, yaw, nullptr, camera, time_s, width, height);
        MetalDrawPacket& ground = packets[n++];
        ground.mesh_id = 2;
        ground.index_count = kGroundIndexCount;
        fill_concrete_ground(ground.uniforms, nullptr, camera, time_s, width, height);
    }
    out_n = n;
    return packets;
}

} // namespace

#if defined(__APPLE__)

static NSString* kMetalSrc = @R"MSL(
#include <metal_stdlib>
using namespace metal;
struct VertexIn { float3 position [[attribute(0)]]; float3 normal [[attribute(1)]]; float2 uv [[attribute(2)]]; };
struct VertexOut { float4 position [[position]]; float3 worldPos; float3 normal; float2 uv; };
struct Uniforms {
  float4x4 modelMatrix; float4x4 viewMatrix; float4x4 projectionMatrix;
  float3 cameraPos; float _pad0; float3 lightDir; float lightIntensity;
  float3 albedoColor; float roughness; float metallic; float time_s; float2 _pad1;
};
float D_GGX(float NdotH, float roughness) {
  float a=roughness*roughness; float a2=a*a; float d=(NdotH*NdotH)*(a2-1.0)+1.0;
  return a2/(3.14159265*d*d);
}
float G_SchlickGGX(float NdotV, float roughness) {
  float r=roughness+1.0; float k=(r*r)/8.0; return NdotV/(NdotV*(1.0-k)+k);
}
float G_Smith(float NdotV, float NdotL, float roughness) {
  return G_SchlickGGX(NdotV,roughness)*G_SchlickGGX(NdotL,roughness);
}
float3 F_Schlick(float cosTheta, float3 F0) { return F0+(1.0-F0)*pow(1.0-cosTheta,5.0); }
vertex VertexOut vertex_main(VertexIn in [[stage_in]], constant Uniforms& u [[buffer(1)]]) {
  VertexOut o; float4 wp=u.modelMatrix*float4(in.position,1.0); o.worldPos=wp.xyz;
  o.position=u.projectionMatrix*u.viewMatrix*wp;
  o.normal=normalize((u.modelMatrix*float4(in.normal,0.0)).xyz); o.uv=in.uv; return o;
}
fragment float4 fragment_main(VertexOut in [[stage_in]], constant Uniforms& u [[buffer(1)]]) {
  float3 N=normalize(in.normal); float3 V=normalize(u.cameraPos-in.worldPos);
  float3 L=normalize(-u.lightDir); float3 H=normalize(V+L);
  float NdotV=max(dot(N,V),0.0); float NdotL=max(dot(N,L),0.0);
  float NdotH=max(dot(N,H),0.0); float VdotH=max(dot(V,H),0.0);
  float3 albedo=u.albedoColor;
  float2 g=abs(fract(in.uv)-float2(0.5));
  float grout=smoothstep(0.47,0.50,max(g.x,g.y));
  albedo*=mix(1.0,0.78,grout*(1.0-u.metallic));
  float3 F0=mix(float3(0.04), albedo, u.metallic);
  float3 F=F_Schlick(VdotH,F0); float D=D_GGX(NdotH,u.roughness); float G=G_Smith(NdotV,NdotL,u.roughness);
  float3 spec=(D*G*F)/max(4.0*NdotV*NdotL,0.001);
  float3 kD=(1.0-F)*(1.0-u.metallic); float3 diff=kD*albedo/3.14159265;
  float shin=mix(16.0,256.0,1.0-u.roughness);
  float3 highlight=F*pow(NdotH,shin)*NdotL;
  float3 rad=(diff+spec)*NdotL*u.lightIntensity;
  rad+=highlight*u.lightIntensity*(0.25+0.55*u.metallic);
  float3 ambient=float3(0.05,0.07,0.1);
  float hemi=0.55+0.45*max(N.y,0.0);
  rad+=ambient*mix(albedo,F0,u.metallic)*hemi;
  rad=pow(max(rad,float3(0.0)), float3(1.0/2.2));
  return float4(rad,1.0);
}
)MSL";

void MetalRenderer::init(void* windowHandle, int w, int h) {
    width = w;
    height = h;
    time_s = 0.f;
    cube_yaw = 0.f;
    cameraPos = float3{0.f, 2.f, -5.f};
    metalLayer = windowHandle;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    ENGINE_ASSERT(dev != nil, "Metal device");
    device = (__bridge_retained void*)dev;
    commandQueue = (__bridge_retained void*)[dev newCommandQueue];
    NSError* err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:kMetalSrc options:nil error:&err];
    ENGINE_ASSERT(lib != nil, "Metal library");
    shaderLibrary = (__bridge_retained void*)lib;
    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat3;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat3;
    vd.attributes[1].offset = 12;
    vd.attributes[1].bufferIndex = 0;
    vd.attributes[2].format = MTLVertexFormatFloat2;
    vd.attributes[2].offset = 24;
    vd.attributes[2].bufferIndex = 0;
    vd.layouts[0].stride = sizeof(CubeVertex);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [lib newFunctionWithName:@"vertex_main"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fragment_main"];
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
    ENGINE_ASSERT(pso != nil, "PBR pipeline");
    pbrPipeline = (__bridge_retained void*)pso;
    MTLDepthStencilDescriptor* dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    depthState = (__bridge_retained void*)[dev newDepthStencilStateWithDescriptor:dd];
    vertexBuffer = (__bridge_retained void*)[dev newBufferWithBytes:kCubeVertices
                                                             length:sizeof(kCubeVertices)
                                                            options:MTLResourceStorageModeShared];
    indexBuffer = (__bridge_retained void*)[dev newBufferWithBytes:kCubeIndices
                                                            length:sizeof(kCubeIndices)
                                                           options:MTLResourceStorageModeShared];
    groundVertexBuffer = (__bridge_retained void*)[dev newBufferWithBytes:kGroundVertices
                                                                   length:sizeof(kGroundVertices)
                                                                  options:MTLResourceStorageModeShared];
    groundIndexBuffer = (__bridge_retained void*)[dev newBufferWithBytes:kGroundIndices
                                                                  length:sizeof(kGroundIndices)
                                                                 options:MTLResourceStorageModeShared];
    uniformBuffer = (__bridge_retained void*)[dev newBufferWithLength:sizeof(MetalPbrUniforms) * kMaxDraws
                                                              options:MTLResourceStorageModeShared];
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                  width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    depthTexture = (__bridge_retained void*)[dev newTextureWithDescriptor:td];
    CAMetalLayer* layer = (__bridge CAMetalLayer*)metalLayer;
    if (layer) {
        layer.device = dev;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.drawableSize = CGSizeMake(w, h);
    }
}

void MetalRenderer::beginFrame() {
    time_s += 1.f / 60.f;
    cube_yaw += 0.012f;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)metalLayer;
    currentDrawable = layer ? (__bridge_retained void*)[layer nextDrawable] : nullptr;
}

void MetalRenderer::renderScene(World& world, FrameAllocator& frame_alloc) {
    u32 n = 0;
    MetalDrawPacket* packets =
        record_scene(world, frame_alloc, cameraPos, cube_yaw, time_s, width, height, n);
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)currentDrawable;
    if (!drawable) {
        return;
    }
    id<MTLBuffer> ub = (__bridge id<MTLBuffer>)uniformBuffer;
    uint8_t* ub_bytes = static_cast<uint8_t*>([ub contents]);
    for (u32 i = 0; i < n; ++i) {
        std::memcpy(ub_bytes + i * sizeof(MetalPbrUniforms), &packets[i].uniforms,
                    sizeof(MetalPbrUniforms));
    }
    id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)commandQueue;
    id<MTLCommandBuffer> cmd = [q commandBuffer];
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.10, 0.13, 0.18, 1.0);
    rp.depthAttachment.texture = (__bridge id<MTLTexture>)depthTexture;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth = 1.0;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pbrPipeline];
    [enc setDepthStencilState:(__bridge id<MTLDepthStencilState>)depthState];
    for (u32 i = 0; i < n; ++i) {
        const bool ground = packets[i].mesh_id == 2;
        id<MTLBuffer> vb = (__bridge id<MTLBuffer>)(ground ? groundVertexBuffer : vertexBuffer);
        id<MTLBuffer> ib = (__bridge id<MTLBuffer>)(ground ? groundIndexBuffer : indexBuffer);
        const NSUInteger icount = ground ? kGroundIndexCount : kCubeIndexCount;
        [enc setVertexBuffer:vb offset:0 atIndex:0];
        [enc setVertexBuffer:ub offset:i * sizeof(MetalPbrUniforms) atIndex:1];
        [enc setFragmentBuffer:ub offset:i * sizeof(MetalPbrUniforms) atIndex:1];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:icount
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:ib
                 indexBufferOffset:0];
    }
    [enc endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}

void MetalRenderer::endFrame() {
    if (currentDrawable) {
        CFRelease(currentDrawable);
        currentDrawable = nullptr;
    }
}

void MetalRenderer::shutdown() {
    currentDrawable = nullptr;
}

#else

void MetalRenderer::init(void* windowHandle, int w, int h) {
    (void)windowHandle;
    width = w;
    height = h;
    time_s = 0.f;
    cube_yaw = 0.f;
    cameraPos = float3{0.f, 2.f, -5.f};
    recorded_draws = 0;
    device = commandQueue = shaderLibrary = pbrPipeline = depthState = nullptr;
    metalLayer = vertexBuffer = indexBuffer = uniformBuffer = currentDrawable = nullptr;
    groundVertexBuffer = groundIndexBuffer = nullptr;
}

void MetalRenderer::beginFrame() {
    time_s += 1.f / 60.f;
    cube_yaw += 0.012f;
    recorded_draws = 0;
}

void MetalRenderer::renderScene(World& world, FrameAllocator& frame_alloc) {
    u32 n = 0;
    (void)record_scene(world, frame_alloc, cameraPos, cube_yaw, time_s, width, height, n);
    recorded_draws = n;
}

void MetalRenderer::endFrame() {}

void MetalRenderer::shutdown() {}

#endif

} // namespace engine
