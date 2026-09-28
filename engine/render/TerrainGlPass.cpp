#include "render/TerrainGlPass.h"

#include "world/SkySystem.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#endif

namespace engine {

namespace {

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

void mat_ident(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

void mat_look(float* m, float3 eye, float3 target, float3 up) {
    float3 f = float3_normalize_or(float3_sub(target, eye), float3{0.f, 0.f, -1.f});
    if (std::fabs(float3_dot(f, up)) > 0.999f) {
        up = float3{0.f, 0.f, 1.f};
    }
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

bool load_text_file(const char* rel, char* dst, u32 cap) {
    const char* paths[4] = {rel, nullptr, nullptr, nullptr};
    static char abs0[512];
    static char abs1[512];
    std::snprintf(abs0, sizeof(abs0), "%s/%s", LEONIDA_SOURCE_DIR, rel);
    std::snprintf(abs1, sizeof(abs1), "shaders/%s", rel);
    paths[1] = abs0;
    paths[2] = abs1;
    for (u32 i = 0; paths[i]; ++i) {
        FILE* f = std::fopen(paths[i], "rb");
        if (!f) {
            continue;
        }
        const usize n = std::fread(dst, 1, cap - 1, f);
        std::fclose(f);
        dst[n] = 0;
        return true;
    }
    return false;
}

unsigned compile_shader(GLenum type, const char* src, const char* tag) {
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::printf("[gl] %s COMPILE FAILED:\n%s\n", tag, log);
        std::fflush(stdout);
        glDeleteShader(s);
        return 0;
    }
    std::printf("[gl] %s compiled\n", tag);
    std::fflush(stdout);
    return s;
}

unsigned link_program(unsigned vs, unsigned fs, const char* tag) {
    const GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::printf("[gl] %s LINK FAILED:\n%s\n", tag, log);
        std::fflush(stdout);
        glDeleteProgram(p);
        return 0;
    }
    std::printf("[gl] %s program linked\n", tag);
    std::fflush(stdout);
    return p;
}

unsigned make_program_from_files(const char* vs_rel, const char* fs_rel, const char* vs_fb,
                                 const char* fs_fb, const char* tag) {
    char vs_src[16384];
    char fs_src[16384];
    const char* vs = vs_fb;
    const char* fs = fs_fb;
    if (load_text_file(vs_rel, vs_src, sizeof(vs_src))) {
        vs = vs_src;
    } else {
        std::printf("[gl] %s: using embedded vertex source (%s not found)\n", tag, vs_rel);
    }
    if (load_text_file(fs_rel, fs_src, sizeof(fs_src))) {
        fs = fs_src;
    } else {
        std::printf("[gl] %s: using embedded fragment source (%s not found)\n", tag, fs_rel);
    }
    const unsigned v = compile_shader(GL_VERTEX_SHADER, vs, tag);
    const unsigned f = compile_shader(GL_FRAGMENT_SHADER, fs, tag);
    if (!v || !f) {
        return 0;
    }
    const unsigned p = link_program(v, f, tag);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

constexpr const char* kSkyVs =
    "#version 330 core\n"
    "void main(){vec2 p=vec2((gl_VertexID<<1)&2, gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,1.0,1.0);}\n";

constexpr const char* kSkyFs =
    "#version 330 core\n"
    "uniform vec3 uSunDir; uniform float uTurb; uniform vec2 uRes; uniform mat4 uInvVP; out vec4 o;\n"
    "vec3 skyColor(vec3 V, vec3 S, float turb){\n"
    "  float ct=clamp(dot(V,S),-1.0,1.0); float ct2=ct*ct;\n"
    "  vec3 betaR=vec3(5.5e-6,13.0e-6,22.4e-6); vec3 betaM=vec3(21e-6)*max(turb,2.0);\n"
    "  float ray=0.75*(1.0+ct2); float g=0.76;\n"
    "  float mie=(1.0-g*g)/max(pow(1.0+g*g-2.0*g*ct,1.5),1e-4);\n"
    "  vec3 c=(betaR*ray+betaM*mie)*1000.0;\n"
    "  c+=vec3(1.0,0.9,0.7)*smoothstep(0.9995,0.9999,ct)*10.0;\n"
    "  c*=mix(0.3,1.0,pow(max(V.y,0.0),0.4)); if(V.y<0.0) c*=0.15; return c;}\n"
    "void main(){ vec3 blue=vec3(0.5,0.7,1.0);\n"
    "  vec2 uv=gl_FragCoord.xy/max(uRes,vec2(1.0));\n"
    "  vec4 clip=vec4(uv*2.0-1.0,1.0,1.0); vec4 w=uInvVP*clip;\n"
    "  if(abs(w.w)<1e-6){o=vec4(blue,1.0);return;}\n"
    "  vec3 V=normalize(w.xyz/w.w);\n"
    "  vec3 S=length(uSunDir)<1e-4?normalize(vec3(0.5,0.8,0.3)):normalize(uSunDir);\n"
    "  vec3 c=skyColor(V,S,uTurb);\n"
    "  if(any(isnan(c))||dot(c,c)<1e-8){o=vec4(blue,1.0);return;}\n"
    "  c=c/(c+vec3(1.0)); c=pow(c,vec3(1.0/2.2)); c=max(c,blue*0.35); o=vec4(c,1.0);}\n";

constexpr const char* kTerVs =
    "#version 330 core\n"
    "layout(location=0) in vec2 aUv; uniform mat4 uVP; uniform float uChunk,uOx,uOz;\n"
    "out vec3 vWorld; out vec3 vN; out float vH;\n"
    "float hash21(vec2 p){vec3 p3=fract(vec3(p.xyx)*0.1031);p3+=dot(p3,p3.yzx+33.33);return fract((p3.x+p3.y)*p3.z);}\n"
    "float vn(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash21(i),hash21(i+vec2(1,0)),f.x),mix(hash21(i+vec2(0,1)),hash21(i+vec2(1,1)),f.x),f.y);}\n"
    "float fbm(vec2 p){float v=0.0,a=0.5; for(int i=0;i<6;i++){v+=a*vn(p);p=p*2.07+vec2(17.1,9.7);a*=0.5;} return v;}\n"
    "float ht(vec2 xz){float continent=fbm(xz*0.0022); float rolling=fbm(xz*0.008);\n"
    "  float n=vn(xz*0.0031); float ridge=1.0-abs(n*2.0-1.0); ridge*=ridge;\n"
    "  float h=6.0+rolling*42.0+ridge*280.0+ridge*ridge*360.0; h*=smoothstep(0.22,0.58,continent); return h+3.0;}\n"
    "void main(){ vec2 xz=vec2(uOx,uOz)+aUv*uChunk; float h=ht(xz); vec3 wp=vec3(xz.x,h,xz.y);\n"
    "  float e=2.0; vec3 n=normalize(vec3(ht(xz)-ht(xz+vec2(e,0)),e,ht(xz)-ht(xz+vec2(0,e))));\n"
    "  vWorld=wp; vWorld.y=h; vN=n; vH=vWorld.y; gl_Position=uVP*vec4(vWorld,1.0);}\n";

constexpr const char* kTerFs =
    "#version 330 core\n"
    "in vec3 vWorld; in vec3 vN; in float vH; out vec4 o;\n"
    "void main(){ float height=vWorld.y; vec3 color;\n"
    "  if(height<100.0) color=vec3(0.1,1.0,0.1);\n"
    "  else if(height<400.0) color=vec3(1.0,0.5,0.0);\n"
    "  else color=vec3(1.0,1.0,1.0);\n"
    "  o=vec4(color,1.0);}\n";

} // namespace

bool TerrainGlPass::init(int w, int h) {
    width = w;
    height = h;
    ok = false;
    sky_prog = terrain_prog = 0;
    grid_vao = grid_vbo = grid_ibo = 0;
    grid_index_count = 0;
    cameraPos = float3{96.f, 800.f, -704.f};
    cameraTarget = float3{96.f, 0.f, 96.f};

    const char* glver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::printf("[gl] version: %s\n", glver ? glver : "(null)");
    std::fflush(stdout);

    sky_prog = make_program_from_files("shaders/sky.vert", "shaders/sky.frag", kSkyVs, kSkyFs, "sky");
    terrain_prog =
        make_program_from_files("shaders/terrain.vert", "shaders/terrain.frag", kTerVs, kTerFs, "terrain");
    if (!sky_prog || !terrain_prog) {
        std::printf("[gl] TERRAIN SHADER FAILED\n");
        std::fflush(stdout);
        return false;
    }
    std::printf("[gl] TERRAIN SHADER COMPILED SUCCESSFULLY\n");
    std::fflush(stdout);

    constexpr u32 kN = 32;
    float uvs[(kN + 1) * (kN + 1) * 2];
    u32 inds[kN * kN * 6];
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
            const u32 b = z * (kN + 1) + x;
            inds[ii++] = b;
            inds[ii++] = b + 1;
            inds[ii++] = b + kN + 1;
            inds[ii++] = b + 1;
            inds[ii++] = b + kN + 2;
            inds[ii++] = b + kN + 1;
        }
    }
    grid_index_count = kN * kN * 6;
    glGenVertexArrays(1, &grid_vao);
    glBindVertexArray(grid_vao);
    glGenBuffers(1, &grid_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(uvs)), uvs, GL_STATIC_DRAW);
    glGenBuffers(1, &grid_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, grid_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(inds)), inds, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, reinterpret_cast<void*>(0));
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    ok = true;
    return true;
}

void TerrainGlPass::beginFrame() {
    glViewport(0, 0, max_of(1, width), max_of(1, height));
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void TerrainGlPass::drawSky(const SkyComponent& sky) {
    if (!sky_prog) {
        return;
    }
    float view[16], proj[16], vp[16], inv[16];
    mat_look(view, cameraPos, cameraTarget, float3{0.f, 1.f, 0.f});
    mat_persp(proj, 1.04719755f, static_cast<float>(width) / max_of(1, height), 0.5f, 4000.f);
    mat_mul(vp, proj, view);
    mat_invert(inv, vp);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(sky_prog);
    const float3 sun = float3_normalize_or(sky.sun_direction, float3{0.5f, 0.8f, 0.3f});
    glUniform3f(glGetUniformLocation(sky_prog, "uSunDir"), sun.x, sun.y, sun.z);
    glUniform1f(glGetUniformLocation(sky_prog, "uTurb"), sky.turbidity > 0.f ? sky.turbidity : 3.f);
    glUniform2f(glGetUniformLocation(sky_prog, "uRes"), static_cast<float>(width),
                static_cast<float>(height));
    glUniformMatrix4fv(glGetUniformLocation(sky_prog, "uInvVP"), 1, GL_FALSE, inv);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}

void TerrainGlPass::drawTerrain() {
    if (!terrain_prog || !grid_vao) {
        return;
    }
    float view[16], proj[16], vp[16];
    mat_look(view, cameraPos, cameraTarget, float3{0.f, 1.f, 0.f});
    mat_persp(proj, 1.04719755f, static_cast<float>(width) / max_of(1, height), 0.5f, 4000.f);
    mat_mul(vp, proj, view);
    glUseProgram(terrain_prog);
    glBindVertexArray(grid_vao);
    glUniformMatrix4fv(glGetUniformLocation(terrain_prog, "uVP"), 1, GL_FALSE, vp);
    glUniform1f(glGetUniformLocation(terrain_prog, "uChunk"), 64.f);
    u32 draws = 0;
    for (u32 z = 0; z < 6; ++z) {
        for (u32 x = 0; x < 6; ++x) {
            glUniform1f(glGetUniformLocation(terrain_prog, "uOx"), static_cast<float>(x) * 64.f);
            glUniform1f(glGetUniformLocation(terrain_prog, "uOz"), static_cast<float>(z) * 64.f);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(grid_index_count), GL_UNSIGNED_INT,
                           nullptr);
            ++draws;
        }
    }
    glBindVertexArray(0);
    static bool logged = false;
    if (!logged) {
        std::printf("[gl] drawing %u terrain chunks (GLSL bound)\n", draws);
        std::fflush(stdout);
        logged = true;
    }
}

void TerrainGlPass::shutdown() {
    if (grid_vao) {
        glDeleteVertexArrays(1, &grid_vao);
    }
    if (grid_vbo) {
        glDeleteBuffers(1, &grid_vbo);
    }
    if (grid_ibo) {
        glDeleteBuffers(1, &grid_ibo);
    }
    if (sky_prog) {
        glDeleteProgram(sky_prog);
    }
    if (terrain_prog) {
        glDeleteProgram(terrain_prog);
    }
    sky_prog = terrain_prog = 0;
}

} // namespace engine
