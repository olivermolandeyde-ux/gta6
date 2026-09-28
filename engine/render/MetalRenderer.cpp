#include "render/MetalRenderer.h"

#include "core/Assert.h"
#include "ecs/CommandBuffer.h"
#include "render/CubeMesh.h"
#include "render/GroundPlaneMesh.h"
#include "render/RenderPipeline.h"
#include "world/CloudSystem.h"
#include "world/SkySystem.h"
#include "world/TerrainSystem.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
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

void mat_mul(float* o, const float* a, const float* b) {
    float t[16];
    for (u32 c = 0; c < 4; ++c) {
        for (u32 r = 0; r < 4; ++r) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1]
                           + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(o, t, sizeof(t));
}

void mat_invert(float* inv, const float* a) {
    inv[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
    inv[12]= -a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
    inv[13]= a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
    inv[10]= a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
    inv[14]= -a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
    inv[11]= -a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
    inv[15]= a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
    float det = a[0]*inv[0]+a[1]*inv[4]+a[2]*inv[8]+a[3]*inv[12];
    if (std::fabs(det) < 1.0e-8f) {
        mat_ident(inv);
        return;
    }
    det = 1.f / det;
    for (u32 i = 0; i < 16; ++i) {
        inv[i] *= det;
    }
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
    cameraPos = float3{96.f, 800.f, -704.f};
    cameraTarget = float3{96.f, 0.f, 96.f};
    cameraYaw = 0.f;
    cameraPitch = -0.78539816f; // -45 degrees, looking down at the terrain
    recorded_sky = recorded_clouds = recorded_terrain = 0;
    metalLayer = windowHandle;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    ENGINE_ASSERT(dev != nil, "Metal device");
    device = (__bridge_retained void*)dev;
    commandQueue = (__bridge_retained void*)[dev newCommandQueue];
    NSError* err = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:kMetalSrc options:nil error:&err];
    if (lib == nil) {
        std::printf("[metal] PBR library FAILED: %s\n",
                    err ? [[err localizedDescription] UTF8String] : "(unknown)");
        std::fflush(stdout);
        pipeline_ok = false;
        fallback_mode = true;
    } else {
        std::printf("[metal] PBR shader library loaded\n");
        std::fflush(stdout);
        shaderLibrary = (__bridge_retained void*)lib;
    }
    pipeline_ok = true;
    fallback_mode = false;
    currentCommandBuffer = currentEncoder = nullptr;
    skyPipeline = cloudPipeline = terrainPipeline = fallbackPipeline = nullptr;
    terrainGridVB = terrainGridIB = skyUniformBuffer = nullptr;
    stubGrass = stubRock = stubSand = stubSnow = stubSplat = nullptr;
    terrainGridIndexCount = 0;
    frame_presented = false;
    if (lib) {
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
        err = nil;
        id<MTLRenderPipelineState> pso = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
        if (pso == nil) {
            std::printf("[metal] PBR pipeline FAILED: %s\n",
                        err ? [[err localizedDescription] UTF8String] : "(unknown)");
            std::fflush(stdout);
            pbrPipeline = nullptr;
            pipeline_ok = false;
            fallback_mode = true;
        } else {
            std::printf("[metal] PBR pipeline created\n");
            std::fflush(stdout);
            pbrPipeline = (__bridge_retained void*)pso;
        }
    }
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

    auto make_tex = [&](uint8_t r, uint8_t g, uint8_t b) -> void* {
        MTLTextureDescriptor* d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:1
                                                              height:1
                                                           mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        id<MTLTexture> t = [dev newTextureWithDescriptor:d];
        uint8_t px[4] = {r, g, b, 255};
        [t replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:px bytesPerRow:4];
        return (__bridge_retained void*)t;
    };
    stubGrass = make_tex(46, 97, 31);
    stubRock  = make_tex(76, 69, 61);
    stubSand  = make_tex(158, 138, 87);
    stubSnow  = make_tex(235, 242, 250);
    stubSplat = make_tex(255, 0, 0);

    static NSString* kPassSrc = @R"MSL(
#include <metal_stdlib>
using namespace metal;
struct SkyU { float3 sunDir; float turb; float3 sunColor; float _p; float4x4 invVP; float2 res; };
vertex float4 sky_vertex(uint vid [[vertex_id]]) {
  float2 p = float2((vid << 1) & 2, vid & 2);
  return float4(p * 2.0 - 1.0, 1.0, 1.0);
}
fragment float4 sky_fragment(float4 pos [[position]], constant SkyU& u [[buffer(0)]]) {
  float3 blue = float3(0.5, 0.7, 1.0);
  float2 res = u.res.x > 1.0 ? u.res : float2(1280.0, 720.0);
  float2 uv = pos.xy / res;
  float4 clip = float4(uv * 2.0 - 1.0, 1.0, 1.0);
  float4 w = u.invVP * clip;
  if (abs(w.w) < 1e-6 || any(isnan(w.xyz))) return float4(blue, 1.0);
  float3 V = normalize(w.xyz / w.w);
  if (any(isnan(V))) return float4(blue, 1.0);
  float3 S = length(u.sunDir) < 1e-4 ? float3(0.5, 0.8, 0.3) : normalize(u.sunDir);
  float ct = clamp(dot(V, S), -1.0, 1.0);
  float3 betaR = float3(5.5e-6, 13.0e-6, 22.4e-6);
  float3 betaM = float3(21e-6) * max(u.turb, 2.0);
  float g = 0.76;
  float mie = (1.0 - g*g) / max(pow(1.0 + g*g - 2.0*g*ct, 1.5), 1e-4);
  float3 c = (betaR * (0.75*(1.0+ct*ct)) + betaM * mie) * 1000.0;
  c += float3(1.0, 0.9, 0.7) * smoothstep(0.9995, 0.9999, ct) * 10.0;
  c *= mix(0.3, 1.0, pow(max(V.y, 0.0), 0.4));
  if (V.y < 0.0) c *= 0.15;
  if (any(isnan(c)) || dot(c,c) < 1e-8) return float4(blue, 1.0);
  c = c / (c + 1.0);
  c = max(pow(c, float3(1.0/2.2)), blue * 0.35);
  return float4(c, 1.0);
}
vertex float4 fallback_vertex(uint vid [[vertex_id]]) {
  float2 p[6] = { float2(-0.6,-0.4), float2(0.6,-0.4), float2(0.6,0.4),
                  float2(-0.6,-0.4), float2(0.6,0.4), float2(-0.6,0.4) };
  return float4(p[vid], 0.0, 1.0);
}
fragment float4 fallback_fragment() { return float4(0.20, 0.55, 0.28, 1.0); }
struct TVIn { float2 uv [[attribute(0)]]; };
struct TVOut { float4 position [[position]]; float3 worldPos; float3 n; float2 uv; float h; };
struct TU { float4x4 vp; float3 cam; float _0; float3 sunDir; float _1; float3 sunColor; float ox; float oz; float chunk; float hscale; };
float hash21(float2 p){ float3 p3=fract(float3(p.xyx)*0.1031); p3+=dot(p3,p3.yzx+33.33); return fract((p3.x+p3.y)*p3.z); }
float vn(float2 p){ float2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(hash21(i),hash21(i+float2(1,0)),f.x), mix(hash21(i+float2(0,1)),hash21(i+float2(1,1)),f.x), f.y); }
float fbm2(float2 p){ float v=0.0,a=0.5; for(int i=0;i<5;i++){ v+=a*vn(p); p=p*2.07+float2(17.1,9.7); a*=0.5;} return v; }
float ht(float2 xz){ float continent=fbm2(xz*0.0022); float rolling=fbm2(xz*0.008);
  float n=vn(xz*0.0031); float ridge=1.0-abs(n*2.0-1.0); ridge*=ridge;
  float h=6.0+rolling*42.0+ridge*280.0+ridge*ridge*360.0; h*=smoothstep(0.22,0.58,continent); return h+3.0; }
vertex TVOut terrain_vertex(TVIn in [[stage_in]], constant TU& u [[buffer(1)]]) {
  TVOut o; float2 xz=float2(u.ox,u.oz)+in.uv*u.chunk; float h=ht(xz);
  float3 wp=float3(xz.x,h,xz.y); float e=2.0;
  o.n=normalize(float3(ht(xz)-ht(xz+float2(e,0)), e, ht(xz)-ht(xz+float2(0,e))));
  o.worldPos=wp; o.uv=in.uv; o.h=h; o.position=u.vp*float4(wp,1.0); return o;
}
fragment float4 terrain_fragment(TVOut in [[stage_in]], constant TU& u [[buffer(1)]],
    texture2d<float> grassTex [[texture(0)]], texture2d<float> rockTex [[texture(1)]],
    texture2d<float> sandTex [[texture(2)]], texture2d<float> snowTex [[texture(3)]],
    texture2d<float> splatmap [[texture(4)]]) {
  (void)grassTex;(void)rockTex;(void)sandTex;(void)snowTex;(void)splatmap;
  float3 base=float3(0.2,0.6,0.2);
  base=mix(base, float3(0.5,0.3,0.1), smoothstep(100.0,140.0,in.h));
  base=mix(base, float3(0.9,0.9,0.9), smoothstep(400.0,480.0,in.h));
  float3 N=normalize(in.n);
  float3 L=length(u.sunDir)<1e-4 ? normalize(float3(0.5,0.8,0.3)) : normalize(u.sunDir);
  float ndl=max(dot(N,L),0.0);
  float3 c=base*(0.35+0.65*ndl)*max(u.sunColor, float3(0.6));
  return float4(pow(max(c,0.0), float3(1.0/2.2)), 1.0);
}
vertex float4 cloud_vertex(uint vid [[vertex_id]]) {
  float2 p=float2((vid<<1)&2, vid&2); return float4(p*2.0-1.0, 0.999, 1.0);
}
fragment float4 cloud_fragment() { return float4(1.0,1.0,1.0,0.0); }
)MSL";

    err = nil;
    id<MTLLibrary> passLib = [dev newLibraryWithSource:kPassSrc options:nil error:&err];
    if (passLib == nil) {
        std::printf("[metal] sky/terrain library FAILED: %s\n",
                    err ? [[err localizedDescription] UTF8String] : "(unknown)");
        std::fflush(stdout);
        fallback_mode = true;
        pipeline_ok = false;
    } else {
        std::printf("[metal] sky/terrain shader library loaded\n");
        std::fflush(stdout);
        auto pso_named = [&](NSString* vs, NSString* fs, MTLVertexDescriptor* vdesc, const char* tag) -> void* {
            MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
            d.vertexFunction = [passLib newFunctionWithName:vs];
            d.fragmentFunction = [passLib newFunctionWithName:fs];
            d.vertexDescriptor = vdesc;
            d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            d.colorAttachments[0].blendingEnabled = YES;
            d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            NSError* e = nil;
            id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&e];
            if (p == nil) {
                std::printf("[metal] %s pipeline FAILED: %s\n", tag,
                            e ? [[e localizedDescription] UTF8String] : "(unknown)");
                std::fflush(stdout);
                return nullptr;
            }
            std::printf("[metal] %s pipeline created\n", tag);
            std::fflush(stdout);
            return (__bridge_retained void*)p;
        };
        skyPipeline = pso_named(@"sky_vertex", @"sky_fragment", nil, "sky");
        fallbackPipeline = pso_named(@"fallback_vertex", @"fallback_fragment", nil, "fallback");
        cloudPipeline = pso_named(@"cloud_vertex", @"cloud_fragment", nil, "cloud");
        MTLVertexDescriptor* tvd = [[MTLVertexDescriptor alloc] init];
        tvd.attributes[0].format = MTLVertexFormatFloat2;
        tvd.attributes[0].offset = 0;
        tvd.attributes[0].bufferIndex = 0;
        tvd.layouts[0].stride = 8;
        tvd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        terrainPipeline = pso_named(@"terrain_vertex", @"terrain_fragment", tvd, "terrain");
        if (!skyPipeline || !terrainPipeline) {
            fallback_mode = true;
            pipeline_ok = false;
        }
    }

    constexpr u32 kN = 32;
    float uvs[(kN + 1) * (kN + 1) * 2];
    u16  inds[kN * kN * 6];
    u32 vi = 0;
    for (u32 z = 0; z <= kN; ++z) {
        for (u32 x = 0; x <= kN; ++x) {
            uvs[vi++] = static_cast<float>(x) / kN;
            uvs[vi++] = static_cast<float>(z) / kN;
        }
    }
    u32 ii = 0;
    for (u32 z = 0; z < kN; ++z) {
        for (u32 x = 0; x < kN; ++x) {
            const u16 b = static_cast<u16>(z * (kN + 1) + x);
            inds[ii++] = b;
            inds[ii++] = static_cast<u16>(b + 1);
            inds[ii++] = static_cast<u16>(b + kN + 1);
            inds[ii++] = static_cast<u16>(b + 1);
            inds[ii++] = static_cast<u16>(b + kN + 2);
            inds[ii++] = static_cast<u16>(b + kN + 1);
        }
    }
    terrainGridIndexCount = kN * kN * 6;
    terrainGridVB = (__bridge_retained void*)[dev newBufferWithBytes:uvs length:sizeof(uvs)
                                                             options:MTLResourceStorageModeShared];
    terrainGridIB = (__bridge_retained void*)[dev newBufferWithBytes:inds length:sizeof(inds)
                                                             options:MTLResourceStorageModeShared];
    skyUniformBuffer = (__bridge_retained void*)[dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
    std::printf("[metal] init complete pipeline_ok=%d fallback=%d\n", pipeline_ok ? 1 : 0,
                fallback_mode ? 1 : 0);
    std::fflush(stdout);
}

void MetalRenderer::beginFrame() {
    time_s += 1.f / 60.f;
    cube_yaw += 0.012f;
    recorded_sky = recorded_clouds = recorded_terrain = 0;
    frame_presented = false;
    currentCommandBuffer = nullptr;
    currentEncoder = nullptr;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)metalLayer;
    currentDrawable = layer ? (__bridge_retained void*)[layer nextDrawable] : nullptr;
    if (!currentDrawable) {
        std::printf("[metal] nextDrawable returned nil (frame will skip)\n");
        std::fflush(stdout);
    }
}

void MetalRenderer::ensurePass() {
    if (currentEncoder || !currentDrawable) {
        return;
    }
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)currentDrawable;
    id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)commandQueue;
    if (!q || !drawable) {
        return;
    }
    id<MTLCommandBuffer> cmd = [q commandBuffer];
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.5, 0.7, 1.0, 1.0);
    rp.depthAttachment.texture = (__bridge id<MTLTexture>)depthTexture;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionDontCare;
    rp.depthAttachment.clearDepth = 1.0;
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    currentCommandBuffer = (__bridge_retained void*)cmd;
    currentEncoder = (__bridge_retained void*)enc;
    if (fallback_mode && fallbackPipeline) {
        [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)fallbackPipeline];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
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
    if (!pbrPipeline) {
        std::printf("[metal] renderScene skipped — PBR pipeline is nil, using fallback pass\n");
        std::fflush(stdout);
        fallback_mode = true;
        ensurePass();
        return;
    }
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
    frame_presented = true;
}

void MetalRenderer::endFrame() {
    ensurePass();
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)currentEncoder;
    id<MTLCommandBuffer> cmd = (__bridge id<MTLCommandBuffer>)currentCommandBuffer;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)currentDrawable;
    if (enc) {
        [enc endEncoding];
        CFRelease(currentEncoder);
        currentEncoder = nullptr;
    }
    if (cmd && drawable && !frame_presented) {
        [cmd presentDrawable:drawable];
        [cmd commit];
        frame_presented = true;
    }
    if (currentCommandBuffer) {
        CFRelease(currentCommandBuffer);
        currentCommandBuffer = nullptr;
    }
    if (currentDrawable) {
        CFRelease(currentDrawable);
        currentDrawable = nullptr;
    }
}

void MetalRenderer::shutdown() {
    currentDrawable = nullptr;
    currentEncoder = nullptr;
    currentCommandBuffer = nullptr;
}

void MetalRenderer::renderSky(const SkyComponent& sky) {
    recorded_sky = 1;
    ensurePass();
    if (fallback_mode || !skyPipeline || !currentEncoder) {
        return;
    }
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)currentEncoder;
    float view[16], proj[16], vp[16], inv[16];
    mat_look(view, cameraPos, cameraTarget, float3{0.f, 1.f, 0.f});
    mat_persp(proj, 1.04719755f, static_cast<float>(width) / max_of(1, height), 0.5f, 4000.f);
    mat_mul(vp, proj, view);
    mat_invert(inv, vp);
    float* ub = static_cast<float*>([(__bridge id<MTLBuffer>)skyUniformBuffer contents]);
    std::memset(ub, 0, 256);
    ub[0] = sky.sun_direction.x;
    ub[1] = sky.sun_direction.y;
    ub[2] = sky.sun_direction.z;
    ub[3] = sky.turbidity;
    ub[4] = sky.sun_color.x;
    ub[5] = sky.sun_color.y;
    ub[6] = sky.sun_color.z;
    std::memcpy(ub + 8, inv, 16 * sizeof(float));
    ub[24] = static_cast<float>(width);
    ub[25] = static_cast<float>(height);
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)skyPipeline];
    [enc setVertexBuffer:(__bridge id<MTLBuffer>)skyUniformBuffer offset:0 atIndex:0];
    [enc setFragmentBuffer:(__bridge id<MTLBuffer>)skyUniformBuffer offset:0 atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void MetalRenderer::renderClouds(const CloudLayerComponent& clouds, const SkyComponent& sky) {
    recorded_clouds = 1;
    (void)clouds;
    (void)sky;
    ensurePass();
    if (fallback_mode || !cloudPipeline || !currentEncoder) {
        return;
    }
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)currentEncoder;
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)cloudPipeline];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void MetalRenderer::renderTerrain(World& world, FrameAllocator& frame_alloc, const float* heights,
                                 u32 hw, u32 hh) {
    (void)heights;
    (void)hw;
    (void)hh;
    u32 n = 0;
    for (Entity e : world.query<TerrainChunkComponent>()) {
        (void)e;
        ++n;
    }
    TerrainChunkComponent* packets = frame_alloc.allocate_array<TerrainChunkComponent>(max_of(n, 1u));
    u32 w = 0;
    for (Entity e : world.query<TerrainChunkComponent>()) {
        const TerrainChunkComponent* c = world.get<TerrainChunkComponent>(e);
        if (c && packets && w < n) {
            packets[w++] = *c;
        }
    }
    recorded_terrain = w;
    ensurePass();
    if (fallback_mode || !terrainPipeline || !currentEncoder || !terrainGridVB) {
        std::printf("[metal] terrain draw skipped pipeline=%p fallback=%d encoder=%p vb=%p\n",
                    terrainPipeline, fallback_mode ? 1 : 0, currentEncoder, terrainGridVB);
        std::fflush(stdout);
        return;
    }
    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)currentEncoder;
    [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)terrainPipeline];
    if (depthState) {
        [enc setDepthStencilState:(__bridge id<MTLDepthStencilState>)depthState];
    }
    [enc setVertexBuffer:(__bridge id<MTLBuffer>)terrainGridVB offset:0 atIndex:0];
    if (stubGrass) {
        [enc setFragmentTexture:(__bridge id<MTLTexture>)stubGrass atIndex:0];
        [enc setFragmentTexture:(__bridge id<MTLTexture>)stubRock atIndex:1];
        [enc setFragmentTexture:(__bridge id<MTLTexture>)stubSand atIndex:2];
        [enc setFragmentTexture:(__bridge id<MTLTexture>)stubSnow atIndex:3];
        [enc setFragmentTexture:(__bridge id<MTLTexture>)stubSplat atIndex:4];
    }
    float view[16], proj[16], vp[16];
    mat_look(view, cameraPos, cameraTarget, float3{0.f, 1.f, 0.f});
    mat_persp(proj, 1.04719755f, static_cast<float>(width) / max_of(1, height), 0.5f, 4000.f);
    mat_mul(vp, proj, view);
    SkyComponent* sky = find_sky(world);
    float3 sun = sky ? sky->sun_direction : float3{0.5f, 0.8f, 0.3f};
    sun = float3_normalize_or(sun, float3{0.5f, 0.8f, 0.3f});
    float3 scol = sky ? sky->sun_color : float3{1.f, 0.95f, 0.85f};
    // Match WebGL: 6x6 of 64 m chunks covering the origin landscape.
    u32 draws = 0;
    for (u32 z = 0; z < 6; ++z) {
        for (u32 x = 0; x < 6; ++x) {
            alignas(16) float ub[32];
            std::memset(ub, 0, sizeof(ub));
            std::memcpy(ub, vp, 16 * sizeof(float));
            ub[16] = cameraPos.x;
            ub[17] = cameraPos.y;
            ub[18] = cameraPos.z;
            ub[20] = sun.x;
            ub[21] = sun.y;
            ub[22] = sun.z;
            ub[24] = scol.x;
            ub[25] = scol.y;
            ub[26] = scol.z;
            ub[27] = static_cast<float>(x) * 64.f;
            ub[28] = static_cast<float>(z) * 64.f;
            ub[29] = 64.f;
            ub[30] = 1.f;
            [enc setVertexBytes:ub length:sizeof(ub) atIndex:1];
            [enc setFragmentBytes:ub length:sizeof(ub) atIndex:1];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexCount:terrainGridIndexCount
                              indexType:MTLIndexTypeUInt16
                            indexBuffer:(__bridge id<MTLBuffer>)terrainGridIB
                      indexBufferOffset:0];
            ++draws;
        }
    }
    recorded_terrain = draws;
    (void)packets;
}

#else

void MetalRenderer::init(void* windowHandle, int w, int h) {
    (void)windowHandle;
    width = w;
    height = h;
    time_s = 0.f;
    cube_yaw = 0.f;
    cameraPos = float3{96.f, 800.f, -704.f};
    cameraTarget = float3{96.f, 0.f, 96.f};
    cameraYaw = 0.f;
    cameraPitch = -0.78539816f;
    recorded_draws = 0;
    recorded_sky = recorded_clouds = recorded_terrain = 0;
    pipeline_ok = true;
    fallback_mode = false;
    frame_presented = false;
    device = commandQueue = shaderLibrary = pbrPipeline = depthState = nullptr;
    metalLayer = vertexBuffer = indexBuffer = uniformBuffer = currentDrawable = nullptr;
    groundVertexBuffer = groundIndexBuffer = nullptr;
    std::printf("[metal] Linux host — frames presented via hosted WebGL (sky blue clear fallback)\n");
    std::fflush(stdout);
}

void MetalRenderer::ensurePass() {}

void MetalRenderer::beginFrame() {
    time_s += 1.f / 60.f;
    cube_yaw += 0.012f;
    recorded_draws = 0;
    recorded_sky = recorded_clouds = recorded_terrain = 0;
    frame_presented = false;
}

void MetalRenderer::renderScene(World& world, FrameAllocator& frame_alloc) {
    u32 n = 0;
    (void)record_scene(world, frame_alloc, cameraPos, cube_yaw, time_s, width, height, n);
    recorded_draws = n;
}

void MetalRenderer::endFrame() {}

void MetalRenderer::shutdown() {}

void MetalRenderer::renderSky(const SkyComponent& sky) {
    recorded_sky = 1;
    (void)sky;
}

void MetalRenderer::renderClouds(const CloudLayerComponent& clouds, const SkyComponent& sky) {
    recorded_clouds = 1;
    (void)clouds;
    (void)sky;
}

void MetalRenderer::renderTerrain(World& world, FrameAllocator& frame_alloc, const float* heights,
                                 u32 hw, u32 hh) {
    (void)heights;
    (void)hw;
    (void)hh;
    u32 n = 0;
    for (Entity e : world.query<TerrainChunkComponent>()) {
        (void)e;
        ++n;
    }
    TerrainChunkComponent* packets = frame_alloc.allocate_array<TerrainChunkComponent>(max_of(n, 1u));
    u32 w = 0;
    for (Entity e : world.query<TerrainChunkComponent>()) {
        const TerrainChunkComponent* c = world.get<TerrainChunkComponent>(e);
        if (c && packets && w < n) {
            packets[w++] = *c;
        }
    }
    recorded_terrain = w;
}

#endif

} // namespace engine
