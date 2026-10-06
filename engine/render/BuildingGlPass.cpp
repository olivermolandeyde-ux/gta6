#include "render/BuildingGlPass.h"

#include "ecs/World.h"
#include "objects/StreetLight.h"
#include "render/CarWheelFit.h"
#include "render/CityProcTex.h"
#include "render/CitySolidMesh.h"
#include "render/RenderPipeline.h"
#include "world/CityGenerator.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

#ifndef ENABLE_TREES
#define ENABLE_TREES 1
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

bool load_text_file(const char* rel, char* dst, u32 cap) {
    char buf[6][512];
    std::snprintf(buf[0], sizeof(buf[0]), "%s", rel);
    std::snprintf(buf[1], sizeof(buf[1]), "%s/%s", LEONIDA_SOURCE_DIR, rel);
    std::snprintf(buf[2], sizeof(buf[2]), "../%s", rel);
    std::snprintf(buf[3], sizeof(buf[3]), "../../%s", rel);
    std::snprintf(buf[4], sizeof(buf[4]), "%s/../%s", LEONIDA_SOURCE_DIR, rel);
    std::snprintf(buf[5], sizeof(buf[5]), "./%s", rel);
    for (u32 i = 0; i < 6; ++i) {
        FILE* f = std::fopen(buf[i], "rb");
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
    return p;
}

unsigned make_program(const char* vs_rel, const char* fs_rel, const char* vs_fb, const char* fs_fb,
                      const char* tag) {
    char vs_src[8192];
    char fs_src[8192];
    const char* vs = vs_fb;
    const char* fs = fs_fb;
    if (load_text_file(vs_rel, vs_src, sizeof(vs_src))) {
        vs = vs_src;
    }
    if (load_text_file(fs_rel, fs_src, sizeof(fs_src))) {
        fs = fs_src;
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

constexpr const char* kBldVs =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aUV;\n"
    "uniform mat4 model, view, projection; out vec3 FragPos; out vec3 Normal; out vec2 UV;\n"
    "void main(){ FragPos=vec3(model*vec4(aPos,1.0)); Normal=aNormal; UV=aUV;\n"
    "  gl_Position=projection*view*vec4(FragPos,1.0); }\n";

constexpr const char* kBldFs =
    "#version 330 core\n"
    "in vec3 FragPos; in vec3 Normal; in vec2 UV;\n"
    "uniform vec3 lightDir, lightColor, albedo; uniform float roughness, time_of_day, emissionBoost;\n"
    "out vec4 FragColor;\n"
    "void main(){ vec3 norm=normalize(Normal); float diff=max(dot(norm,normalize(lightDir)),0.0);\n"
    "  vec3 diffuse=diff*lightColor; vec3 ambient=0.18*lightColor;\n"
    "  float windowPattern=step(0.28,fract(UV.x*10.0))*step(0.32,fract(UV.y*5.0));\n"
    "  float dusk=smoothstep(18.0,20.0,time_of_day);\n"
    "  float dawn=1.0-smoothstep(5.5,7.5,time_of_day);\n"
    "  float nightFactor=max(dusk, dawn*step(time_of_day,12.0));\n"
    "  vec3 windowEmission=vec3(1.0,0.9,0.5)*windowPattern*nightFactor*0.85;\n"
    "  vec3 result=(ambient+diffuse*(1.0-roughness*0.4))*albedo + windowEmission + albedo*emissionBoost*nightFactor;\n"
    "  FragColor=vec4(result,1.0); }\n";

constexpr const char* kStVs =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV;\n"
    "uniform mat4 view, projection; out vec2 UV; out vec3 FragPos;\n"
    "void main(){ UV=aUV; FragPos=aPos; gl_Position=projection*view*vec4(aPos,1.0); }\n";

constexpr const char* kStFs =
    "#version 330 core\n"
    "in vec2 UV; in vec3 FragPos; out vec4 FragColor;\n"
    "void main(){ vec3 asphalt=vec3(0.12,0.12,0.13); vec3 paint=vec3(0.92,0.86,0.35);\n"
    "  float dash=step(0.45,fract(UV.y*8.0))*(1.0-step(0.04,abs(UV.x-0.5)));\n"
    "  vec2 gmod=min(mod(FragPos.xz,120.0),120.0-mod(FragPos.xz,120.0));\n"
    "  float inBox=(1.0-step(11.0,gmod.x))*(1.0-step(11.0,gmod.y));\n"
    "  float bandZ=(1.0-step(9.5,gmod.x))*step(10.5,gmod.y)*(1.0-step(15.5,gmod.y));\n"
    "  float bandX=(1.0-step(9.5,gmod.y))*step(10.5,gmod.x)*(1.0-step(15.5,gmod.x));\n"
    "  dash*=1.0-max(inBox,max(bandZ,bandX));\n"
    "  vec3 c=mix(asphalt,paint,dash); FragColor=vec4(c,1.0); }\n";

// Unit cube 0..1, 24 verts (pos, nrm, uv).
constexpr float kCube[] = {
    // +Z
    0,0,1,  0,0,1,  0,0,  1,0,1,  0,0,1,  1,0,  1,1,1,  0,0,1,  1,1,  0,1,1,  0,0,1,  0,1,
    // -Z
    1,0,0,  0,0,-1, 0,0,  0,0,0,  0,0,-1, 1,0,  0,1,0,  0,0,-1, 1,1,  1,1,0,  0,0,-1, 0,1,
    // +X
    1,0,1,  1,0,0,  0,0,  1,0,0,  1,0,0,  1,0,  1,1,0,  1,0,0,  1,1,  1,1,1,  1,0,0,  0,1,
    // -X
    0,0,0,  -1,0,0, 0,0,  0,0,1,  -1,0,0, 1,0,  0,1,1,  -1,0,0, 1,1,  0,1,0,  -1,0,0, 0,1,
    // +Y
    0,1,1,  0,1,0,  0,0,  1,1,1,  0,1,0,  1,0,  1,1,0,  0,1,0,  1,1,  0,1,0,  0,1,0,  0,1,
    // -Y
    0,0,0,  0,-1,0, 0,0,  1,0,0,  0,-1,0, 1,0,  1,0,1,  0,-1,0, 1,1,  0,0,1,  0,-1,0,  0,1,
};

constexpr u32 kCubeIdx[] = {
    0,1,2, 0,2,3, 4,5,6, 4,6,7, 8,9,10, 8,10,11, 12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23,
};

constexpr u32 kStreetVertCap = 8192 * 6;

void emit_aabb_quad(float* verts, u32* n, float x0, float z0, float x1, float z1, float y, float u00,
                    float v00, float u10, float v10, float u11, float v11, float u01, float v01) {
    if (*n + 6 > kStreetVertCap) {
        return;
    }
    const float p[6][5] = {
        {x0, y, z0, u00, v00}, {x1, y, z0, u10, v10}, {x1, y, z1, u11, v11},
        {x0, y, z0, u00, v00}, {x1, y, z1, u11, v11}, {x0, y, z1, u01, v01},
    };
    for (u32 t = 0; t < 6; ++t) {
        for (u32 k = 0; k < 5; ++k) {
            verts[*n * 5 + k] = p[t][k];
        }
        ++(*n);
    }
}

void model_trs(float* m, float3 pos, float sx, float sy, float sz) {
    mat_ident(m);
    m[0]  = sx;
    m[5]  = sy;
    m[10] = sz;
    m[12] = pos.x - sx * 0.5f;
    m[13] = pos.y;
    m[14] = pos.z - sz * 0.5f;
}

void model_axis(float* m, float3 pos, float sx, float sy, float sz) {
    mat_ident(m);
    m[0]  = sx;
    m[5]  = sy;
    m[10] = sz;
    m[12] = pos.x;
    m[13] = pos.y;
    m[14] = pos.z;
}

void mat_ortho(float* m, float s, float n, float f) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 1.f / s;
    m[5] = 1.f / s;
    m[10] = -2.f / (f - n);
    m[11] = 0.f;
    m[14] = -(f + n) / (f - n);
    m[15] = 1.f;
}

void mat_mul16(float* o, const float* a, const float* b) {
    float t[16];
    for (u32 c = 0; c < 4; ++c) {
        for (u32 r = 0; r < 4; ++r) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1]
                           + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(o, t, sizeof(t));
}

// Crosswalk geometry fingerprint (single source of truth; logged once from
// buildMesh so any screenshot can be traced to the exact dimensions that drew it).
// Zebra style (photo ground truth): bars run PARALLEL to traffic.
// N/S arms: bars LONG in Z (crossing depth), NARROW in X, spaced in X.
// E/W arms: bars LONG in X, NARROW in Z, spaced in Z.
constexpr float kXwalkLen = 4.5f;   // bar length along traffic (crossing depth)
constexpr float kXwalkThick = 0.9f; // bar width across the road
constexpr float kXwalkPitch = 1.8f; // 0.9 stripe + 0.9 asphalt gap
constexpr u32 kXwalkCount =
    static_cast<u32>((kCityStreetWidth - 2.0f) / 1.8f); // 10 bars, centred
constexpr float kXwalkStart = kCityStreetWidth * 0.5f + 1.0f; // 11: outside junction

void upload_solid(unsigned* vao, unsigned* ibo, unsigned* count, SolidVert* verts, u32 vn, u32* idx, u32 in) {
    unsigned vbo = 0;
    glGenVertexArrays(1, vao);
    glBindVertexArray(*vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vn * sizeof(SolidVert)), verts, GL_STATIC_DRAW);
    glGenBuffers(1, ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(in * sizeof(u32)), idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVert), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SolidVert),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SolidVert),
                          reinterpret_cast<void*>(6 * sizeof(float)));
    glBindVertexArray(0);
    *count = in;
}

void bind_inv_scale(unsigned prog, float sx, float sy, float sz) {
    glUniform3f(glGetUniformLocation(prog, "uInvScale"), 1.f / max_of(0.001f, sx), 1.f / max_of(0.001f, sy),
                1.f / max_of(0.001f, sz));
}

void set_building_uniforms(unsigned prog, const float* view, const float* proj, float3 sun,
                           float time_of_day, float3 cam) {
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "view"), 1, GL_FALSE, view);
    glUniformMatrix4fv(glGetUniformLocation(prog, "projection"), 1, GL_FALSE, proj);
    glUniform3f(glGetUniformLocation(prog, "lightDir"), sun.x, sun.y, sun.z);
    glUniform3f(glGetUniformLocation(prog, "lightColor"), 1.55f, 1.48f, 1.28f);
    glUniform1f(glGetUniformLocation(prog, "time_of_day"), time_of_day);
    glUniform1f(glGetUniformLocation(prog, "floors"), 8.f);
    glUniform1i(glGetUniformLocation(prog, "district"), 1);
    glUniform1i(glGetUniformLocation(prog, "windowStyle"), 0);
    glUniform3f(glGetUniformLocation(prog, "uCamPos"), cam.x, cam.y, cam.z);
    glUniform3f(glGetUniformLocation(prog, "uFogColor"), 0.690f, 0.769f, 0.871f);
    glUniform3f(glGetUniformLocation(prog, "uInvScale"), 1.f, 1.f, 1.f);
    glUniform1i(glGetUniformLocation(prog, "uUseTex"), 0);
    glUniform1i(glGetUniformLocation(prog, "uAlphaLeaf"), 0);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(prog, "uNormalTex"), 1);
    glUniform1i(glGetUniformLocation(prog, "uShadow"), 2);
    glUniform1i(glGetUniformLocation(prog, "uFacade"), 1);
}

void draw_box(unsigned prog, float3 p, float sx, float sy, float sz, float3 albedo, float emit) {
    float m[16];
    model_trs(m, p, sx, sy, sz);
    glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, m);
    bind_inv_scale(prog, 1.f, 1.f, 1.f);
    glUniform1i(glGetUniformLocation(prog, "uUseTex"), 0);
    glUniform1i(glGetUniformLocation(prog, "uAlphaLeaf"), 0);
    glUniform3f(glGetUniformLocation(prog, "albedo"), albedo.x, albedo.y, albedo.z);
    glUniform1f(glGetUniformLocation(prog, "emissionBoost"), emit);
    glUniform1f(glGetUniformLocation(prog, "roughness"), 0.5f);
    glUniform1i(glGetUniformLocation(prog, "uFacade"), 0);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
}

void draw_axis_mesh(unsigned prog, unsigned vao, unsigned nidx, float3 p, float sx, float sy, float sz,
                    float3 albedo, float emit, unsigned albedo_tex, int leaf) {
    float m[16];
    model_axis(m, p, sx, sy, sz);
    glBindVertexArray(vao);
    glUniformMatrix4fv(glGetUniformLocation(prog, "model"), 1, GL_FALSE, m);
    bind_inv_scale(prog, sx, sy, sz);
    glUniform1i(glGetUniformLocation(prog, "uUseTex"), albedo_tex ? 1 : 0);
    glUniform1i(glGetUniformLocation(prog, "uAlphaLeaf"), leaf);
    glUniform3f(glGetUniformLocation(prog, "albedo"), albedo.x, albedo.y, albedo.z);
    glUniform1f(glGetUniformLocation(prog, "emissionBoost"), emit);
    glUniform1i(glGetUniformLocation(prog, "uFacade"), 0);
    if (albedo_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, albedo_tex);
        glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 0);
    }
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(nidx), GL_UNSIGNED_INT, nullptr);
}

[[nodiscard]] bool near_xz(float3 a, float3 b, float r) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz < r * r;
}

float night_glow_amt(float tod) {
    const float dusk = tod <= 18.f ? 0.f : (tod >= 20.f ? 1.f : (tod - 18.f) * 0.5f);
    const float dawn = tod >= 7.5f ? 0.f : (tod <= 5.5f ? 1.f : (7.5f - tod) * 0.5f);
    return tod < 12.f ? (dusk > dawn ? dusk : dawn) : dusk;
}

float glb_fit_scale(const TreeGlb* t, float target_h) {
    if (!t) {
        return 1.f;
    }
    const float hy = t->ymax - t->ymin;
    const float hx = t->xmax - t->xmin;
    const float hz = t->zmax - t->zmin;
    float h = hy;
    if (t->z_up) {
        h = hz;
    }
    if (h < hx) {
        h = hx;
    }
    if (h < 0.25f) {
        return 1.f;
    }
    float s = target_h / h;
    if (s < 0.03f) {
        s = 0.03f;
    }
    if (s > 8.f) {
        s = 8.f;
    }
    return s;
}

float tree_up_extent(const TreeGlb* t) {
    if (!t) {
        return 1.f;
    }
    if (t->z_up == 2) {
        return t->xmax - t->xmin;
    }
    if (t->z_up == 1) {
        return t->zmax - t->zmin;
    }
    return t->ymax - t->ymin;
}

float tree_up_min(const TreeGlb* t) {
    if (!t) {
        return 0.f;
    }
    if (t->z_up == 2) {
        return t->xmin;
    }
    if (t->z_up == 1) {
        return t->zmin;
    }
    return t->ymin;
}

float tree_fit_scale(const TreeGlb* t, float target_h) {
    if (!t || t->nprims == 0) {
        return 1.f;
    }
    float h = tree_up_extent(t);
    if (h < 0.001f) {
        const float hx = t->xmax - t->xmin;
        const float hz = t->zmax - t->zmin;
        h = hx > hz ? hx : hz;
    }
    if (h < 0.001f) {
        return 1.f;
    }
    float s = target_h / h;
    if (s > 50.f) {
        s = 50.f;
    }
    if (s < 0.0008f) {
        s = 0.0008f;
    }
    return s;
}

float tree_height_m(const TreeGlb* t) {
    return tree_up_extent(t);
}

float corolla_fit_scale(const TreeGlb* t) {
    if (!t || t->nprims == 0) {
        return 1.f;
    }
    const float hx = t->xmax - t->xmin;
    const float hy = t->ymax - t->ymin;
    const float hz = t->zmax - t->zmin;
    // Longest AABB axis is length, regardless of which way the mesh is authored.
    float length = hx;
    if (hy > length) {
        length = hy;
    }
    if (hz > length) {
        length = hz;
    }
    if (length < 0.001f) {
        return 1.f;
    }
    float s = (4.2f * 2.2f * 1.4f) / length; // 1.4× the accepted 2.2× length fit
    if (s > 50.f) {
        s = 50.f;
    }
    if (s < 0.0008f) {
        s = 0.0008f;
    }
    return s;
}

bool xz_too_close(float x, float z, const float* xz, u32 n, float min_d) {
    const float m2 = min_d * min_d;
    for (u32 i = 0; i < n; ++i) {
        const float dx = x - xz[i * 2u];
        const float dz = z - xz[i * 2u + 1u];
        if (dx * dx + dz * dz < m2) {
            return true;
        }
    }
    return false;
}

void log_glb_textures(const char* name, const TreeGlb* t) {
    if (!t) {
        return;
    }
    for (u32 i = 0; i < t->nprims; ++i) {
        const TreePrim& p = t->prims[i];
        std::printf("[glb] Tree '%s' has texture: %ux%u, channels: RGBA\n", name, p.tex_w, p.tex_h);
        std::printf("[glb] Tree '%s' texture: %ux%u pixels, material: %s cutoff 0.5\n", name, p.tex_w,
                    p.tex_h, p.alpha_mask ? "alpha-mask" : "opaque");
    }
    std::fflush(stdout);
}

// Build model→world 3x3: longest AABB axis = forward (Z), shortest = up (Y), rest = right (X).
// The GLB of one car class: 0 Corolla, 1 sports, 2 SUV.
TreeGlb* car_glb_model(BuildingGlPass& p, u32 m) {
    if (m == 0u) {
        return &p.corolla_glb;
    }
    if (m == 1u) {
        return &p.sports_glb;
    }
    if (m == 2u) {
        return &p.suv_glb;
    }
    return nullptr;
}

void corolla_basis(const TreeGlb* t, float R[9], int* fwd_axis_out, int* up_axis_out) {
    const float e[3] = {t->xmax - t->xmin, t->ymax - t->ymin, t->zmax - t->zmin};
    // Longest axis = length, shortest = height, except that a wheel's axle axis is the
    // width by definition: a tall, narrow SUV would otherwise be laid on its side.
    int fwd = 0;
    int up  = 0;
    car_axis_roles(e, t->wheel_axis, &fwd, &up);
    float fu[3] = {0.f, 0.f, 0.f};
    float uu[3] = {0.f, 0.f, 0.f};
    fu[fwd] = 1.f;
    uu[up]  = 1.f;
    const float ru[3] = {fu[1] * uu[2] - fu[2] * uu[1], fu[2] * uu[0] - fu[0] * uu[2],
                         fu[0] * uu[1] - fu[1] * uu[0]};
    static const char* kAxis = "XYZ";
    std::printf("[cars] basis length=%c height=%c (stand on wheels)\n", kAxis[fwd], kAxis[up]);
    std::fflush(stdout);
    if (fwd_axis_out) {
        *fwd_axis_out = fwd;
    }
    if (up_axis_out) {
        *up_axis_out = up;
    }
    // columns = images of model X,Y,Z  (world X=right, Y=up, Z=forward)
    R[0] = ru[0];
    R[1] = uu[0];
    R[2] = fu[0];
    R[3] = ru[1];
    R[4] = uu[1];
    R[5] = fu[1];
    R[6] = ru[2];
    R[7] = uu[2];
    R[8] = fu[2];
}

void corolla_mul(const float R[9], float x, float y, float z, float* ox, float* oy, float* oz) {
    *ox = R[0] * x + R[3] * y + R[6] * z;
    *oy = R[1] * x + R[4] * y + R[7] * z;
    *oz = R[2] * x + R[5] * y + R[8] * z;
}

float corolla_model_ymin(const TreeGlb* t, const float R[9]) {
    float mn = 1.0e9f;
    const float xs[2] = {t->xmin, t->xmax};
    const float ys[2] = {t->ymin, t->ymax};
    const float zs[2] = {t->zmin, t->zmax};
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                float ox, oy, oz;
                corolla_mul(R, xs[i], ys[j], zs[k], &ox, &oy, &oz);
                if (oy < mn) {
                    mn = oy;
                }
            }
        }
    }
    return mn;
}

void corolla_yaw_mat(float* m, float x, float y, float z, float yaw, float sc, const float R[9]) {
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    corolla_mul(R, sc, 0.f, 0.f, &ax, &ay, &az);
    corolla_mul(R, 0.f, sc, 0.f, &bx, &by, &bz);
    corolla_mul(R, 0.f, 0.f, sc, &cx, &cy, &cz);
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    std::memset(m, 0, 16 * sizeof(float));
    m[0]  = c * ax + s * az;
    m[1]  = ay;
    m[2]  = -s * ax + c * az;
    m[4]  = c * bx + s * bz;
    m[5]  = by;
    m[6]  = -s * bx + c * bz;
    m[8]  = c * cx + s * cz;
    m[9]  = cy;
    m[10] = -s * cx + c * cz;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1.f;
}

void tree_yaw_mat(float* m, float x, float y, float z, float yaw, float sc, int z_up) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    std::memset(m, 0, 16 * sizeof(float));
    if (z_up == 2) {
        // Ry(yaw) * Rz(90°) * S  — X-up trunk stands up in Y.
        m[1]  = sc;
        m[4]  = -c * sc;
        m[6]  = s * sc;
        m[8]  = s * sc;
        m[10] = c * sc;
    } else if (z_up) {
        // Ry(yaw) * Rx(-90°) * S  — Z-up assets stand up in Y.
        m[0]  = c * sc;
        m[2]  = s * sc;
        m[4]  = s * sc;
        m[6]  = -c * sc;
        m[9]  = sc;
    } else {
        m[0]  = c * sc;
        m[2]  = -s * sc;
        m[5]  = sc;
        m[8]  = s * sc;
        m[10] = c * sc;
    }
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1.f;
}

// 1x1 white fallback so texture-less prims sample a safe solid color instead of
// an unbound texture unit (which renders pink). Created lazily on first use;
// after that it is just a bind call — no per-frame overhead.
unsigned g_fallback_white_tex = 0;

unsigned fallback_white_tex() {
    if (g_fallback_white_tex) {
        return g_fallback_white_tex;
    }
    const u8 px[4] = {255, 255, 255, 255};
    glGenTextures(1, &g_fallback_white_tex);
    glBindTexture(GL_TEXTURE_2D, g_fallback_white_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return g_fallback_white_tex;
}

void draw_instanced_glb(TreeGlb* g, unsigned prog) {
    if (!prog || !g || g->instance_count == 0) {
        return;
    }
    glUseProgram(prog);
    glDisable(GL_CULL_FACE); // FIX invisible walls: some GLBs have inside-out winding
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(prog, "uEmissive"), 1);
    glUniform1i(glGetUniformLocation(prog, "uShadow"), 2);
    for (u32 p = 0; p < g->nprims; ++p) {
        TreePrim& pr = g->prims[p];
        glBindVertexArray(pr.vao);

        // FIX pink textures: always bind a valid texture. Primitives whose GLB had
        // no image get the 1x1 white fallback (solid color) instead of sampling an
        // unbound unit. uUseTexture tells supporting shaders to skip detail lookup.
        const bool has_texture = (pr.tex != 0);
        const unsigned alb_tex = has_texture ? pr.tex : fallback_white_tex();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, alb_tex);
        glActiveTexture(GL_TEXTURE1);
        if (pr.tex_emit) {
            glBindTexture(GL_TEXTURE_2D, pr.tex_emit);
        } else {
            glBindTexture(GL_TEXTURE_2D, alb_tex);
        }

        glUniform1i(glGetUniformLocation(prog, "uAlphaMask"), pr.alpha_mask);
        glUniform1f(glGetUniformLocation(prog, "uAlphaCut"), pr.cutoff);
        glUniform1i(glGetUniformLocation(prog, "uUseTexture"), has_texture ? 1 : 0);
        glUniform3f(glGetUniformLocation(prog, "uSolidColor"), 1.f, 1.f, 1.f);

        // Wheel spin uniforms: harmless on trees and lamps, which read uWheelCount == 0.
        glUniform1i(glGetUniformLocation(prog, "uWheelCount"), pr.wheel_count);
        if (pr.wheel_count > 0) {
            glUniform3fv(glGetUniformLocation(prog, "uWheelCenter"), pr.wheel_count,
                         &pr.wheel_center[0][0]);
            const float axis[3] = {pr.wheel_axis == 0 ? 1.f : 0.f, pr.wheel_axis == 1 ? 1.f : 0.f,
                                   pr.wheel_axis == 2 ? 1.f : 0.f};
            glUniform3fv(glGetUniformLocation(prog, "uWheelAxis"), 1, axis);
            glUniform1f(glGetUniformLocation(prog, "uWheelRoll"), pr.wheel_roll);
        }
        if (pr.alpha_mask) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        GLenum md = GL_TRIANGLES;
        switch (pr.gl_mode) {
        case 0:
            md = GL_POINTS;
            glPointSize(4.f);
            break;
        case 1:
            md = GL_LINES;
            break;
        case 2:
            md = GL_LINE_LOOP;
            break;
        case 3:
            md = GL_LINE_STRIP;
            break;
        case 5:
            md = GL_TRIANGLE_STRIP;
            break;
        case 6:
            md = GL_TRIANGLE_FAN;
            break;
        default:
            md = GL_TRIANGLES;
            break;
        }
        glDrawElementsInstanced(md, static_cast<GLsizei>(pr.nidx), GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(g->instance_count));
    }
    glEnable(GL_CULL_FACE); // restore culling for the procedural-box passes
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void draw_instanced_glb_pass(TreeGlb* g, unsigned prog, int want_mask) {
    if (!prog || !g || g->instance_count == 0) {
        return;
    }
    TreeGlb slice = *g;
    u32 n = 0;
    for (u32 p = 0; p < g->nprims; ++p) {
        if ((g->prims[p].alpha_mask != 0) == (want_mask != 0)) {
            slice.prims[n++] = g->prims[p];
        }
    }
    slice.nprims = n;
    if (n) {
        draw_instanced_glb(&slice, prog);
    }
}

void draw_tree_glbs(TreeGlb* trees, unsigned prog) {
    if (!prog) {
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        draw_instanced_glb_pass(&trees[k], prog, 0);
    }
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        draw_instanced_glb_pass(&trees[k], prog, 1);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

[[nodiscard]] bool custom_sky_lot(const BuildingComponent* b) {
    return b && b->district == kDistrictDowntown && (b->building_id % 10u) < 4u;
}

bool load_city_tree(const char* file, TreeGlb* dst) {
    return find_and_load_tree_glb(file, dst);
}

// --- GLB building replacement bookkeeping (startup only, no per-frame cost) ---
// Procedural boxes that get a GLB replacement are skipped in draw() + shadow pass.
constexpr u32 kReplacedBldCap = 4096;
u32 g_replaced_n = 0;
u8 g_replaced_flag[kReplacedBldCap] = {0};

bool is_replaced_building(u32 id) {
    return id < kReplacedBldCap && g_replaced_flag[id] != 0;
}

void mark_replaced_building(u32 id) {
    if (id < kReplacedBldCap && !g_replaced_flag[id]) {
        g_replaced_flag[id] = 1;
        ++g_replaced_n;
    }
}

void clear_replaced_buildings() {
    std::memset(g_replaced_flag, 0, sizeof(g_replaced_flag));
    g_replaced_n = 0;
}

// Uniform scale so the GLB footprint matches the procedural lot it replaces.
// Square-fit (shortest target side / longest model side) is yaw-safe for the
// quantized 0/90/180/270 placements below and preserves aspect ratio.
float building_footprint_scale(const TreeGlb* t, float target_w, float target_d) {
    if (!t || t->nprims == 0) {
        return 1.f;
    }
    const float hx = t->xmax - t->xmin;
    const float hy = t->ymax - t->ymin;
    const float hz = t->zmax - t->zmin;
    float mw, md;
    if (t->z_up) {
        mw = hx;
        md = hy;
    } else {
        mw = hx;
        md = hz;
    }
    if (mw < 0.001f || md < 0.001f) {
        return 1.f;
    }
    const float longest = mw > md ? mw : md;
    const float shortest_target = target_w < target_d ? target_w : target_d;
    if (longest < 0.001f || shortest_target < 0.001f) {
        return 1.f;
    }
    float s = shortest_target / longest;
    if (s < 0.05f) {
        s = 0.05f;
    }
    if (s > 8.f) {
        s = 8.f;
    }
    return s;
}

// Startup audit: scan a GLB file's node list for non-identity rotations on mesh
// nodes (a baked tilt). Standalone JSON scan — does not touch the GLB loader.
// Returns the number of rotated mesh nodes found (0 = clean or unreadable).
u32 report_glb_node_rotations(const char* base, const char* label) {
    char paths[3][512];
    std::snprintf(paths[0], sizeof(paths[0]), "%s/assets/models/buildings/%s", LEONIDA_SOURCE_DIR,
                  base);
    std::snprintf(paths[1], sizeof(paths[1]), "assets/models/buildings/%s", base);
    std::snprintf(paths[2], sizeof(paths[2]), "build/assets/models/buildings/%s", base);
    FILE* f = nullptr;
    for (u32 i = 0; i < 3 && !f; ++i) {
        f = std::fopen(paths[i], "rb");
    }
    if (!f) {
        return 0;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 20 || sz > 32L * 1024L * 1024L) {
        std::fclose(f);
        return 0;
    }
    char* buf = static_cast<char*>(std::malloc(static_cast<usize>(sz) + 1));
    if (!buf) {
        std::fclose(f);
        return 0;
    }
    const usize nread = std::fread(buf, 1, static_cast<usize>(sz), f);
    std::fclose(f);
    buf[nread] = 0;
    u32 magic = 0;
    std::memcpy(&magic, buf, 4);
    if (magic != 0x46546C67u || nread < 20) {
        std::free(buf);
        return 0;
    }
    // First chunk must be JSON.
    u32 json_len = 0;
    std::memcpy(&json_len, buf + 12, 4);
    if (12 + 8 + json_len > nread) {
        std::free(buf);
        return 0;
    }
    const char* json = buf + 20;
    const char* jend = json + json_len;
    // Find the "nodes" array.
    const char* arr = nullptr;
    for (const char* p = json; p + 7 < jend; ++p) {
        if (p[0] == '"' && std::strncmp(p + 1, "nodes\"", 6) == 0) {
            const char* q = p + 7;
            while (q < jend && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r' || *q == ':')) {
                ++q;
            }
            if (q < jend && *q == '[') {
                arr = q + 1;
            }
            break;
        }
    }
    u32 rotated = 0;
    u32 total = 0;
    if (arr) {
        // Walk top-level objects of the array; strings respected for depth.
        const char* p = arr;
        while (p < jend) {
            while (p < jend && *p != '{' && *p != ']') {
                ++p;
            }
            if (p >= jend || *p == ']') {
                break;
            }
            const char* obj = p;
            int depth = 0;
            bool in_str = false;
            while (p < jend) {
                const char c = *p;
                if (in_str) {
                    if (c == '\\') {
                        p += 2;
                        continue;
                    }
                    if (c == '"') {
                        in_str = false;
                    }
                    ++p;
                    continue;
                }
                if (c == '"') {
                    in_str = true;
                    ++p;
                    continue;
                }
                if (c == '{') {
                    ++depth;
                } else if (c == '}') {
                    --depth;
                    ++p;
                    if (depth == 0) {
                        break;
                    }
                    continue;
                }
                ++p;
            }
            const char* obj_end = p; // one past '}'
            ++total;
            // rotation array inside this object?
            float q[4] = {0.f, 0.f, 0.f, 1.f};
            bool has_rot = false;
            for (const char* r = obj; r + 10 < obj_end; ++r) {
                if (r[0] == '"' && std::strncmp(r + 1, "rotation\"", 9) == 0) {
                    const char* b = r + 10;
                    while (b < obj_end && *b != '[') {
                        ++b;
                    }
                    if (b < obj_end) {
                        char* e = nullptr;
                        bool ok = true;
                        const char* s = b + 1;
                        for (u32 k = 0; k < 4; ++k) {
                            q[k] = std::strtof(s, &e);
                            if (e == s) {
                                ok = false;
                                break;
                            }
                            s = e;
                        }
                        has_rot = ok;
                    }
                    break;
                }
            }
            if (has_rot && (std::fabs(q[0]) > 1e-3f || std::fabs(q[1]) > 1e-3f ||
                            std::fabs(q[2]) > 1e-3f || std::fabs(std::fabs(q[3]) - 1.f) > 1e-3f)) {
                // name + mesh presence for the report.
                char nm[96] = {'?', 0};
                bool has_mesh = false;
                for (const char* r = obj; r + 6 < obj_end; ++r) {
                    if (r[0] == '"' && std::strncmp(r + 1, "name\"", 5) == 0) {
                        const char* s = r + 6;
                        while (s < obj_end && *s != '"') {
                            ++s;
                        }
                        if (s < obj_end) {
                            ++s;
                            u32 k = 0;
                            while (s + k < obj_end && s[k] != '"' && k + 1u < sizeof(nm)) {
                                nm[k] = s[k];
                                ++k;
                            }
                            nm[k] = 0;
                        }
                    }
                    if (r[0] == '"' && std::strncmp(r + 1, "mesh\"", 5) == 0) {
                        has_mesh = true;
                    }
                }
                std::printf("[buildings] node-rot: %s node '%s' rotation "
                            "(%.3f, %.3f, %.3f, %.3f)%s\n",
                            label, nm, static_cast<double>(q[0]), static_cast<double>(q[1]),
                            static_cast<double>(q[2]), static_cast<double>(q[3]),
                            has_mesh ? " HAS MESH — SUSPECT (re-author this asset)" : "");
                ++rotated;
            }
            (void)obj_end;
        }
        if (rotated == 0) {
            std::printf("[buildings] node-rot: %s: %u nodes scanned, no rotated mesh nodes\n",
                        label, total);
        }
    }
    std::free(buf);
    std::fflush(stdout);
    return rotated;
}

// Sidewalk darkening tint (one-line brightness knob): sampled pavers are
// near-white, so scale them to medium concrete gray. Zebra bars stay the
// brightest white in the street scene.
const float3 kSidewalkTint{0.52f, 0.52f, 0.50f};
// Lot grass brightness (one-line knob like the sidewalk tint).
const float3 kGrassTint{0.55f, 0.65f, 0.50f};

// Shared prop spacing + sidewalk helpers (all prop placement loops).
// Logic mirrors the original per-block lambdas exactly; hoisted so every prop
// type shares one taken-list (no cross-type overlaps, old or new).
constexpr u32 kPropTakenCap = 900;
float g_prop_taken_xz[kPropTakenCap * 2];
u32 g_n_prop_taken = 0;

float prop_road_dist(float v) {
    float f = std::fmod(v, kCityBlockPitch);
    if (f < 0.f) {
        f += kCityBlockPitch;
    }
    return f < kCityBlockPitch - f ? f : kCityBlockPitch - f;
}

bool prop_on_walk_band(float x, float z) {
    const float dx = prop_road_dist(x);
    const float dz = prop_road_dist(z);
    if (dx < 10.4f || dz < 10.4f) {
        return false; // asphalt
    }
    if (dx > 12.6f && dz > 12.6f) {
        return false; // lot grass
    }
    return true;
}

// Model front (+Z) toward the nearest road centre line, snapped to 90 deg.
float prop_yaw_to_road(float x, float z) {
    float fx = std::fmod(x, kCityBlockPitch);
    if (fx < 0.f) {
        fx += kCityBlockPitch;
    }
    float fz = std::fmod(z, kCityBlockPitch);
    if (fz < 0.f) {
        fz += kCityBlockPitch;
    }
    const float dx = fx < kCityBlockPitch - fx ? fx : kCityBlockPitch - fx;
    const float dz = fz < kCityBlockPitch - fz ? fz : kCityBlockPitch - fz;
    float dirx = 0.f, dirz = 0.f;
    if (dx <= dz) {
        dirx = (fx < kCityBlockPitch * 0.5f) ? -1.f : 1.f;
    } else {
        dirz = (fz < kCityBlockPitch * 0.5f) ? -1.f : 1.f;
    }
    return std::atan2(dirx, dirz);
}

// Street/block level datum, defined once: asphalt at road level, lawn and
// sidewalk tops exactly equal, 15 cm curb face between them.
constexpr float kRoadY = kCityPlateauY;
constexpr float kCurbH = 0.15f;
constexpr float kTopY = kRoadY + kCurbH;

} // namespace

bool BuildingGlPass::init() {
    ok = false;
    building_prog = street_prog = cloud_prog = 0;
    tree_prog = tree_shadow_prog = 0;
    std::memset(tree_glb, 0, sizeof(tree_glb));
    std::memset(&sky_glb, 0, sizeof(sky_glb));
    std::memset(lamp_glb, 0, sizeof(lamp_glb));
    std::memset(&corolla_glb, 0, sizeof(corolla_glb));
    std::memset(&sports_glb, 0, sizeof(sports_glb));
    std::memset(&suv_glb, 0, sizeof(suv_glb));
    corolla_glb.wheel_axis = sports_glb.wheel_axis = suv_glb.wheel_axis = -1;
    std::memset(shop_glb, 0, sizeof(shop_glb));
    std::memset(apartment_glb, 0, sizeof(apartment_glb));
    std::memset(&hydrant_glb, 0, sizeof(hydrant_glb));
    std::memset(&bench_glb, 0, sizeof(bench_glb));
    std::memset(&bin_glb, 0, sizeof(bin_glb));
    std::memset(&mailbox_glb, 0, sizeof(mailbox_glb));
    std::memset(&kiosk_glb, 0, sizeof(kiosk_glb));
    std::memset(&atm_glb, 0, sizeof(atm_glb));
    std::memset(warehouse_glb, 0, sizeof(warehouse_glb));
    std::memset(shop_mats, 0, sizeof(shop_mats));
    std::memset(apartment_mats, 0, sizeof(apartment_mats));
    std::memset(warehouse_mats, 0, sizeof(warehouse_mats));
    shop_count = apartment_count = warehouse_count = 0;
    verification_row = false;
    std::memset(&car_traffic, 0, sizeof(car_traffic));
    car_traffic_live = false;
    car_clock        = 0.f;
    std::memset(car_basis, 0, sizeof(car_basis));
    std::memset(car_scale, 0, sizeof(car_scale));
    std::memset(car_y, 0, sizeof(car_y));
    car_body_flip = 0.f;
    car_suv_flip  = 0.f;
    for (u32 m = 0; m < kCarMeshCount; ++m) {
        car_wheel_radius[m] = 0.f;
        car_yaw_off[m]      = kCarPi;
        car_fwd_axis[m]     = 0;
        car_up_axis[m]      = 1;
    }
    std::memset(car_wheel_angles, 0, sizeof(car_wheel_angles));
    glow_prog = glow_vao = glow_vbo = glow_ibo = glow_ivbo = glow_nidx = 0;
    glow_count = 0;
    cube_vao = cube_vbo = cube_ibo = 0;
    street_vao = street_vbo = 0;
    lot_vao = lot_ibo = lot_count = 0;
    ring_vao = ring_ibo = ring_count = 0;
    skirt_vao = skirt_ibo = skirt_count = 0;
    tex_sidewalk = 0;
    tex_grass = 0;
    tex_flat_n = 0;
    street_count = 0;
    num_buildings = 0;

    building_prog =
        make_program("shaders/building.vert", "shaders/building.frag", kBldVs, kBldFs, "building");
    street_prog = make_program("shaders/street.vert", "shaders/street.frag", kStVs, kStFs, "street");
    constexpr const char* kClVs =
        "#version 330 core\nuniform mat4 view,projection; uniform vec3 uCenter,uRight,uUp; uniform vec2 uSize;\n"
        "out vec2 UV; void main(){ vec2 c=vec2((gl_VertexID==1||gl_VertexID==2||gl_VertexID==4)?1.0:-1.0,"
        "(gl_VertexID>=2&&gl_VertexID!=3)?1.0:-1.0); UV=c*0.5+0.5; vec3 pos=uCenter+uRight*c.x*uSize.x+uUp*c.y*uSize.y;"
        " gl_Position=projection*view*vec4(pos,1.0);}\n";
    constexpr const char* kClFs =
        "#version 330 core\nin vec2 UV; out vec4 FragColor;\n"
        "void main(){ vec2 p=(UV-0.5)*2.0; float d=length(p); float a=smoothstep(1.0,0.2,d)*0.65; if(a<0.02) discard;"
        " FragColor=vec4(mix(vec3(0.8),vec3(1.0),1.0-d),a); }\n";
    cloud_prog = make_program("shaders/cloud.vert", "shaders/cloud.frag", kClVs, kClFs, "cloud");
    if (!building_prog || !street_prog) {
        std::printf("[gl] BUILDING SHADER FAILED\n");
        std::fflush(stdout);
        return false;
    }

    glGenVertexArrays(1, &cube_vao);
    glBindVertexArray(cube_vao);
    glGenBuffers(1, &cube_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kCube)), kCube, GL_STATIC_DRAW);
    glGenBuffers(1, &cube_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kCubeIdx)), kCubeIdx,
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          reinterpret_cast<void*>(6 * sizeof(float)));
    glBindVertexArray(0);

    {
        SolidVert sv[4096];
        u32 sidx[8192];
        u32 vn = 0, in = 0;
        mesh_taper_cyl(sv, &vn, sidx, &in, 16, 0.55f);
        upload_solid(&cyl_vao, &cyl_ibo, &cyl_count, sv, vn, sidx, in);
        vn = 0;
        in = 0;
        mesh_uv_sphere(sv, &vn, sidx, &in, 12, 16);
        upload_solid(&sph_vao, &sph_ibo, &sph_count, sv, vn, sidx, in);
        vn = 0;
        in = 0;
        mesh_cone(sv, &vn, sidx, &in, 16);
        upload_solid(&cone_vao, &cone_ibo, &cone_count, sv, vn, sidx, in);
    }
    {
        static u8 rgba[kProcTexSize * kProcTexSize * 4];
        static u8 nrm[kProcTexSize * kProcTexSize * 3];
        proc_tex_brick(rgba, nrm);
        tex_brick = gl_upload_rgba(rgba, kProcTexSize, kProcTexSize, true);
        tex_brick_n = gl_upload_rgb(nrm, kProcTexSize, kProcTexSize);
        proc_tex_concrete(rgba, nrm);
        tex_conc = gl_upload_rgba(rgba, kProcTexSize, kProcTexSize, true);
        tex_conc_n = gl_upload_rgb(nrm, kProcTexSize, kProcTexSize);
        proc_tex_asphalt(rgba, nrm);
        tex_asph = gl_upload_rgba(rgba, kProcTexSize, kProcTexSize, true);
        proc_tex_bark(rgba);
        tex_bark = gl_upload_rgba(rgba, kProcTexSize, kProcTexSize, true);
        proc_tex_leaf(rgba);
        tex_leaf = gl_upload_rgba(rgba, kProcTexSize, kProcTexSize, true);
    }
    // Sidewalk paver texture for the ring mesh (world-space tiled). Falls back
    // to flat light-gray when the PNG is missing — never crashes, never magenta.
    {
        const char* cands[3] = {"assets/textures/sidewalk.png", "./assets/textures/sidewalk.png",
                                "build/assets/textures/sidewalk.png"};
        u8* file = nullptr;
        u32 flen = 0;
        for (u32 i = 0; i < 3 && !file; ++i) {
            FILE* f = std::fopen(cands[i], "rb");
            if (!f) {
                continue;
            }
            std::fseek(f, 0, SEEK_END);
            const long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz > 20 && sz < 16L * 1024L * 1024L) {
                file = static_cast<u8*>(std::malloc(static_cast<usize>(sz)));
                if (file && std::fread(file, 1, static_cast<usize>(sz), f) == static_cast<usize>(sz)) {
                    flen = static_cast<u32>(sz);
                } else {
                    std::free(file);
                    file = nullptr;
                }
            }
            std::fclose(f);
        }
        u8* rgba = nullptr;
        u32 tw = 0, th = 0;
        if (file && decode_png_file_rgba(file, flen, &rgba, &tw, &th) && rgba && tw && th) {
            glGenTextures(1, &tex_sidewalk);
            glBindTexture(GL_TEXTURE_2D, tex_sidewalk);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(tw), static_cast<int>(th),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glGenerateMipmap(GL_TEXTURE_2D);
            std::printf("[city] sidewalk texture: %ux%u from PNG (tiled world/2 m)\n", tw, th);
            std::free(rgba);
        } else {
            tex_sidewalk = 0;
            std::printf("[city] sidewalk texture missing: flat light-gray fallback\n");
        }
        if (file) {
            std::free(file);
        }
        std::fflush(stdout);
    }
    // Lot grass texture (world-space tiled, see the lot mesh). Same rules as the
    // sidewalk PNG; plus anisotropic filtering (clamped to the reported max) so
    // the big ground planes don't shimmer at grazing angles and distance.
    {
        const char* cands[3] = {"assets/textures/grass.png", "./assets/textures/grass.png",
                                "build/assets/textures/grass.png"};
        u8* file = nullptr;
        u32 flen = 0;
        for (u32 i = 0; i < 3 && !file; ++i) {
            FILE* f = std::fopen(cands[i], "rb");
            if (!f) {
                continue;
            }
            std::fseek(f, 0, SEEK_END);
            const long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz > 20 && sz < 16L * 1024L * 1024L) {
                file = static_cast<u8*>(std::malloc(static_cast<usize>(sz)));
                if (file && std::fread(file, 1, static_cast<usize>(sz), f) == static_cast<usize>(sz)) {
                    flen = static_cast<u32>(sz);
                } else {
                    std::free(file);
                    file = nullptr;
                }
            }
            std::fclose(f);
        }
        u8* rgba = nullptr;
        u32 tw = 0, th = 0;
        if (file && decode_png_file_rgba(file, flen, &rgba, &tw, &th) && rgba && tw && th) {
            GLfloat max_aniso = 0.f;
            glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &max_aniso);
            GLfloat aniso = max_aniso;
            if (aniso > 8.f) {
                aniso = 8.f;
            }
            if (aniso < 1.f) {
                aniso = 1.f;
            }
            glGenTextures(1, &tex_grass);
            glBindTexture(GL_TEXTURE_2D, tex_grass);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, aniso);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(tw), static_cast<int>(th),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glGenerateMipmap(GL_TEXTURE_2D);
            std::printf("[city] grass texture: %ux%u from PNG (tiled world/5.0 m, aniso %.1f of max %.1f)\n",
                        tw, th, static_cast<double>(aniso), static_cast<double>(max_aniso));
            std::free(rgba);
        } else {
            tex_grass = 0;
            std::printf("[city] grass texture missing, using flat green\n");
        }
        std::printf("[city] grass tint=(%.2f,%.2f,%.2f)\n", static_cast<double>(kGrassTint.x),
                    static_cast<double>(kGrassTint.y), static_cast<double>(kGrassTint.z));
        if (file) {
            std::free(file);
        }
        std::fflush(stdout);
    }
    // 1x1 flat normal so textured ground (uUseTex=1) gets no fake relief from
    // whatever happens to sit on unit 1 (building.frag always samples uNormalTex
    // in that branch).
    {
        const u8 flat[3] = {128, 128, 255};
        glGenTextures(1, &tex_flat_n);
        glBindTexture(GL_TEXTURE_2D, tex_flat_n);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, flat);
    }
    {
        constexpr const char* kShVs =
            "#version 330 core\nlayout(location=0) in vec3 aPos; uniform mat4 model,uLightVP;\n"
            "void main(){ gl_Position=uLightVP*model*vec4(aPos,1.0); }\n";
        constexpr const char* kShFs = "#version 330 core\nvoid main(){}\n";
        shadow_prog = make_program("shaders/shadow.vert", "shaders/shadow.frag", kShVs, kShFs, "shadow");
        glGenTextures(1, &shadow_tex);
        glBindTexture(GL_TEXTURE_2D, shadow_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1024, 1024, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glGenFramebuffers(1, &shadow_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_tex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        std::printf("[gl] proc textures + shadow map + solid meshes ready\n");
        std::fflush(stdout);
    }

    glGenVertexArrays(1, &street_vao);
    glBindVertexArray(street_vao);
    glGenBuffers(1, &street_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, street_vbo);
    glBufferData(GL_ARRAY_BUFFER, 8192 * 6 * 5 * static_cast<GLsizeiptr>(sizeof(float)), nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

    constexpr const char* kTreeFbVs =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aUV;\n"
        "layout(location=3) in vec4 iM0; layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2;"
        " layout(location=6) in vec4 iM3; layout(location=8) in float iWheelAngle;\n"
        "uniform mat4 view,projection,uLightVP;\n"
        "uniform int uWheelCount; uniform vec3 uWheelCenter[4]; uniform vec3 uWheelAxis;"
        " uniform float uWheelRoll;\n"
        "out vec3 FragPos; out vec3 Normal; out vec2 UV; out vec4 LightPos; out vec4 VertColor;\n"
        "void main(){ mat4 model=mat4(iM0,iM1,iM2,iM3); vec3 p=aPos; vec3 n=aNormal;\n"
        " if(uWheelCount>0){ vec3 c=uWheelCenter[0]; float best=distance(aPos,uWheelCenter[0]);\n"
        "  for(int i=1;i<4;++i){ if(i>=uWheelCount) break; float d=distance(aPos,uWheelCenter[i]);"
        " if(d<best){ best=d; c=uWheelCenter[i]; } }\n"
        "  float a=iWheelAngle*uWheelRoll; float cs=cos(a); float sn=sin(a); vec3 v=p-c;\n"
        "  p=c+v*cs+cross(uWheelAxis,v)*sn+uWheelAxis*(dot(uWheelAxis,v)*(1.0-cs));\n"
        "  n=n*cs+cross(uWheelAxis,n)*sn+uWheelAxis*(dot(uWheelAxis,n)*(1.0-cs)); }\n"
        " vec4 wp=model*vec4(p,1.0); FragPos=wp.xyz; Normal=normalize(mat3(model)*n); UV=aUV;"
        " VertColor=vec4(1.0); LightPos=uLightVP*wp; gl_Position=projection*view*wp; }\n";
    constexpr const char* kTreeFbFs =
        "#version 330 core\n"
        "in vec3 FragPos; in vec3 Normal; in vec2 UV; in vec4 LightPos; in vec4 VertColor;\n"
        "uniform sampler2D uAlbedo; out vec4 FragColor;\n"
        "void main(){ vec4 albedo=texture(uAlbedo, UV); if(albedo.a<0.5) discard; FragColor=vec4(albedo.rgb,1.0); }\n";
    tree_prog = make_program("shaders/tree.vert", "shaders/tree.frag", kTreeFbVs, kTreeFbFs, "tree");
    constexpr const char* kTshFbVs =
        "#version 330 core\nlayout(location=0) in vec3 aPos; layout(location=2) in vec2 aUV;"
        " layout(location=3) in vec4 iM0;\n"
        "layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;"
        " layout(location=8) in float iWheelAngle;\n"
        "uniform mat4 uLightVP; uniform int uWheelCount; uniform vec3 uWheelCenter[4];"
        " uniform vec3 uWheelAxis; uniform float uWheelRoll;\n"
        "out vec2 UV; void main(){ mat4 model=mat4(iM0,iM1,iM2,iM3); vec3 p=aPos;\n"
        " if(uWheelCount>0){ vec3 c=uWheelCenter[0]; float best=distance(aPos,uWheelCenter[0]);\n"
        "  for(int i=1;i<4;++i){ if(i>=uWheelCount) break; float d=distance(aPos,uWheelCenter[i]);"
        " if(d<best){ best=d; c=uWheelCenter[i]; } }\n"
        "  float a=iWheelAngle*uWheelRoll; float cs=cos(a); float sn=sin(a); vec3 v=p-c;\n"
        "  p=c+v*cs+cross(uWheelAxis,v)*sn+uWheelAxis*(dot(uWheelAxis,v)*(1.0-cs)); }\n"
        " UV=aUV; gl_Position=uLightVP*model*vec4(p,1.0); }\n";
    constexpr const char* kTshFbFs =
        "#version 330 core\nin vec2 UV; uniform sampler2D uAlbedo;\n"
        "void main(){ if(texture(uAlbedo,UV).a<0.5) discard; }\n";
    tree_shadow_prog =
        make_program("shaders/tree_shadow.vert", "shaders/tree_shadow.frag", kTshFbVs, kTshFbFs, "tree_shadow");
    auto prep_tree = [](TreeGlb* dst) {
        for (u32 p = 0; p < dst->nprims; ++p) {
            if (dst->prims[p].has_alpha) {
                dst->prims[p].alpha_mask = 1;
                dst->prims[p].cutoff = 0.5f;
            }
        }
    };
#if ENABLE_TREES
    load_city_tree("tree.glb", &tree_glb[0]);
    prep_tree(&tree_glb[0]);
    load_city_tree("pine_tree_low-poly.glb", &tree_glb[1]);
    prep_tree(&tree_glb[1]);
    load_city_tree("coconut_tree_low_poly.glb", &tree_glb[2]);
    prep_tree(&tree_glb[2]);
    std::printf("[trees] Loaded 3 tree models from GLB files\n");
    std::printf("[trees] Alpha discard enabled for leaf materials\n");
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        const TreeGlb* t = &tree_glb[k];
        const float h = tree_height_m(t);
        const float fit = tree_fit_scale(t, k == 1 ? 9.f : 8.f);
        std::printf("[trees] Model '%s' bounds: %.2f meters tall (aabb %.2f x %.2f x %.2f z_up=%d)\n", t->label,
                    h, t->xmax - t->xmin, t->ymax - t->ymin, t->zmax - t->zmin, t->z_up);
        if (t->nprims > 0) {
            std::printf("[trees] Texture size: %ux%u, channels: 4\n", t->prims[0].tex_w, t->prims[0].tex_h);
        }
        std::printf("[trees] Scale factor applied: %.5f\n", fit);
    }
    std::fflush(stdout);
#endif
    if (load_city_tree("skyscraper-2.glb", &sky_glb)) {
        std::printf("[gl] Loaded custom skyscraper model: skyscraper-2.glb (%u verts)\n", sky_glb.nverts);
    } else {
        std::printf("[gl] custom skyscraper-2.glb not found — downtown stays procedural\n");
    }
    std::printf("[glb] Loading lamp model: gatelys_klassisk.glb\n");
    load_city_tree("gatelys_klassisk.glb", &lamp_glb[0]);
    std::printf("[glb] Loading lamp model: gatelys_moderne.glb\n");
    load_city_tree("gatelys_moderne.glb", &lamp_glb[1]);
    if (load_city_tree("low-poly_toyota_corolla_e80_sedan.glb", &corolla_glb)) {
        std::printf("[cars] Loaded Toyota Corolla E80: %u verts (should be < 10,000)\n", corolla_glb.nverts);
        const float hx = corolla_glb.xmax - corolla_glb.xmin;
        const float hy = corolla_glb.ymax - corolla_glb.ymin;
        const float hz = corolla_glb.zmax - corolla_glb.zmin;
        std::printf("[cars] Car bounds: %.2fx%.2fx%.2f meters\n", hx, hy, hz);
        {
            float Rtmp[9];
            corolla_basis(&corolla_glb, Rtmp, nullptr, nullptr);
        }
        const float sc = corolla_fit_scale(&corolla_glb);
        float L = hx;
        if (hy > L) {
            L = hy;
        }
        if (hz > L) {
            L = hz;
        }
        std::printf("[cars] Scale factor: %.5f, final size: %.2f x %.2f x %.2f meters\n", sc, L * sc, hx * sc,
                    hy * sc);
        std::printf("[cars] Scale increased by 1.4x for better visibility\n");
        std::fflush(stdout);
    } else {
        std::printf("[cars] low-poly_toyota_corolla_e80_sedan.glb not found — no parked cars\n");
        std::fflush(stdout);
    }
    if (load_city_tree("low_poly_sports_car__game_ready_vehicle.glb", &sports_glb)) {
        std::printf("[cars] Loaded sports car: %u verts\n", sports_glb.nverts);
        const float hx = sports_glb.xmax - sports_glb.xmin;
        const float hy = sports_glb.ymax - sports_glb.ymin;
        const float hz = sports_glb.zmax - sports_glb.zmin;
        std::printf("[cars] Sports car bounds: %.2fx%.2fx%.2f meters\n", hx, hy, hz);
        {
            float Rtmp[9];
            corolla_basis(&sports_glb, Rtmp, nullptr, nullptr);
        }
        const float sc = corolla_fit_scale(&sports_glb);
        float L = hx;
        if (hy > L) {
            L = hy;
        }
        if (hz > L) {
            L = hz;
        }
        std::printf("[cars] Sports scale factor: %.5f, final size: %.2f x %.2f x %.2f meters\n", sc, L * sc,
                    hx * sc, hy * sc);
        std::fflush(stdout);
    } else {
        std::printf("[cars] low_poly_sports_car__game_ready_vehicle.glb not found — Corolla only\n");
        std::fflush(stdout);
    }
    if (load_city_tree("low_poly_suv.glb", &suv_glb)) {
        std::printf("[cars] Loaded SUV: %u verts\n", suv_glb.nverts);
        const float hx = suv_glb.xmax - suv_glb.xmin;
        const float hy = suv_glb.ymax - suv_glb.ymin;
        const float hz = suv_glb.zmax - suv_glb.zmin;
        std::printf("[cars] SUV bounds: %.2fx%.2fx%.2f meters (length/height/width axes are "
                    "settled by the wheels)\n",
                    hx, hy, hz);
        const float sc = corolla_fit_scale(&suv_glb);
        std::printf("[cars] SUV scale factor: %.5f\n", sc);
        std::fflush(stdout);
    } else {
        std::printf("[cars] low_poly_suv.glb not found — the SUV class stays out of the mix\n");
        std::fflush(stdout);
    }
    std::printf("[glb] Loaded street lamp models: klassisk (%u verts), moderne (%u verts)\n",
                lamp_glb[0].nverts, lamp_glb[1].nverts);
    log_glb_textures("klassisk", &lamp_glb[0]);
    log_glb_textures("moderne", &lamp_glb[1]);

    // Load 9 new building GLB models (3 types × 3 variants each)
    std::printf("\n=== [buildings] LOADING NEW BUILDING MODELS ===\n");
    std::printf("[buildings] Looking for models in: assets/models/buildings/\n");

    auto load_building_model = [&](const char* name, TreeGlb* dst, const char* type) -> bool {
        std::printf("[buildings] Attempting to load: %s\n", name);
        if (load_city_tree(name, dst)) {
            std::printf("[buildings] SUCCESS: %s loaded (%u verts, %u prims, z_up=%d)\n",
                        type, dst->nverts, dst->nprims, dst->z_up);
            std::printf("[buildings]   Bounds: x=[%.2f, %.2f] y=[%.2f, %.2f] z=[%.2f, %.2f]\n",
                        dst->xmin, dst->xmax, dst->ymin, dst->ymax, dst->zmin, dst->zmax);
            // Log texture info for each primitive
            for (u32 p = 0; p < dst->nprims; ++p) {
                const TreePrim& pr = dst->prims[p];
                std::printf("[buildings]   Prim %u of %s: %u verts, tex=%u (%ux%u), "
                            "has_texture=%d, emit_tex=%u, has_alpha=%d, alpha_mask=%d, gl_mode=%d\n",
                            p, type, pr.nidx, pr.tex, pr.tex_w, pr.tex_h, pr.tex != 0 ? 1 : 0,
                            pr.tex_emit, pr.has_alpha, pr.alpha_mask, pr.gl_mode);
                if (pr.tex == 0) {
                    std::printf("[buildings]   WARNING: %s prim %u has NO texture: 1x1 white "
                                "fallback is bound (renders solid, not pink).\n",
                                type, p);
                }
            }
            if (dst->nprims > 0) {
                std::printf("[buildings]   First prim: %u verts, has_alpha=%d\n",
                            dst->prims[0].nidx, dst->prims[0].has_alpha);
            }
            return true;
        } else {
            std::printf("[buildings] FAILED: %s not found or invalid\n", name);
            return false;
        }
    };

    // Shop variants
    std::printf("[buildings] --- SHOP MODELS ---\n");
    load_building_model("buildings/shop_small_variant_a.glb", &shop_glb[0], "shop_small_variant_a");
    load_building_model("buildings/shop_small_variant_b.glb", &shop_glb[1], "shop_small_variant_b");
    load_building_model("buildings/shop_small_variant_c.glb", &shop_glb[2], "shop_small_variant_c");

    // Apartment variants
    std::printf("[buildings] --- APARTMENT MODELS ---\n");
    load_building_model("buildings/apartment_5story_variant_a.glb", &apartment_glb[0], "apartment_5story_variant_a");
    load_building_model("buildings/apartment_5story_variant_b.glb", &apartment_glb[1], "apartment_5story_variant_b");
    load_building_model("buildings/apartment_5story_variant_c.glb", &apartment_glb[2], "apartment_5story_variant_c");

    // Warehouse variants
    std::printf("[buildings] --- WAREHOUSE MODELS ---\n");
    load_building_model("buildings/warehouse_industrial_variant_a.glb", &warehouse_glb[0], "warehouse_industrial_variant_a");
    load_building_model("buildings/warehouse_industrial_variant_b.glb", &warehouse_glb[1], "warehouse_industrial_variant_b");
    load_building_model("buildings/warehouse_industrial_variant_c.glb", &warehouse_glb[2], "warehouse_industrial_variant_c");

    // Street props (same loader call as the shops above)
    std::printf("[buildings] --- STREET PROP MODELS ---\n");
    load_building_model("props/prop_hydrant_red.glb", &hydrant_glb, "prop_hydrant_red");
    load_building_model("props/prop_bench_wood.glb", &bench_glb, "prop_bench_wood");
    load_building_model("props/prop_bin_metal.glb", &bin_glb, "prop_bin_metal");
    load_building_model("props/prop_mailbox_usps.glb", &mailbox_glb, "prop_mailbox_usps");
    load_building_model("props/prop_newskiosk_metal.glb", &kiosk_glb, "prop_newskiosk_metal");
    load_building_model("props/prop_atm_wall.glb", &atm_glb, "prop_atm_wall");
    if (hydrant_glb.nprims == 0) {
        std::printf("[props] missing prop_hydrant_red (skipped, no fallback)\n");
    }
    if (bench_glb.nprims == 0) {
        std::printf("[props] missing prop_bench_wood (skipped, no fallback)\n");
    }
    if (bin_glb.nprims == 0) {
        std::printf("[props] missing prop_bin_metal (skipped, no fallback)\n");
    }
    if (mailbox_glb.nprims == 0) {
        std::printf("[props] missing prop_mailbox_usps (skipped, no fallback)\n");
    }
    if (kiosk_glb.nprims == 0) {
        std::printf("[props] missing prop_newskiosk_metal (skipped, no fallback)\n");
    }
    if (atm_glb.nprims == 0) {
        std::printf("[props] missing prop_atm_wall (skipped, no fallback)\n");
    }

    // Summary
    u32 shops_loaded = 0, apts_loaded = 0, whses_loaded = 0;
    for (u32 i = 0; i < 3; ++i) {
        if (shop_glb[i].nprims > 0) ++shops_loaded;
        if (apartment_glb[i].nprims > 0) ++apts_loaded;
        if (warehouse_glb[i].nprims > 0) ++whses_loaded;
    }
    std::printf("\n[buildings] === LOAD SUMMARY ===\n");
    std::printf("[buildings] Loaded %u/3 shop variants, %u/3 apartment variants, %u/3 warehouse variants\n",
                shops_loaded, apts_loaded, whses_loaded);
    std::printf("[buildings] Total: %u/9 building models have valid mesh data\n",
                shops_loaded + apts_loaded + whses_loaded);
    if (shops_loaded == 0 && apts_loaded == 0 && whses_loaded == 0) {
        std::printf("[buildings] WARNING: No building models loaded! Check file paths and GLB validity.\n");
    }

    // FIX: Force alpha_mask=0 for all building primitives to avoid pink textures
    // GLB files may have RGBA textures but OPAQUE materials - this causes blending issues
    // By forcing alpha_mask=0, we ensure glDisable(GL_BLEND) is used for all building prims
    std::printf("\n[buildings] === TEXTURE FIX ===\n");
    for (u32 i = 0; i < 3; ++i) {
        for (u32 p = 0; p < shop_glb[i].nprims; ++p) {
            shop_glb[i].prims[p].alpha_mask = 0;  // Force opaque
            std::printf("[buildings] Fix: shop variant %u prim %u: alpha_mask set to 0 (was %d)\n",
                        i, p, shop_glb[i].prims[p].alpha_mask);
        }
        for (u32 p = 0; p < apartment_glb[i].nprims; ++p) {
            apartment_glb[i].prims[p].alpha_mask = 0;
        }
        for (u32 p = 0; p < warehouse_glb[i].nprims; ++p) {
            warehouse_glb[i].prims[p].alpha_mask = 0;
        }
    }
    for (u32 p = 0; p < hydrant_glb.nprims; ++p) {
        hydrant_glb.prims[p].alpha_mask = 0;
    }
    for (u32 p = 0; p < bench_glb.nprims; ++p) {
        bench_glb.prims[p].alpha_mask = 0;
    }
    for (u32 p = 0; p < bin_glb.nprims; ++p) {
        bin_glb.prims[p].alpha_mask = 0;
    }
    for (u32 p = 0; p < mailbox_glb.nprims; ++p) {
        mailbox_glb.prims[p].alpha_mask = 0;
    }
    for (u32 p = 0; p < kiosk_glb.nprims; ++p) {
        kiosk_glb.prims[p].alpha_mask = 0;
    }
    for (u32 p = 0; p < atm_glb.nprims; ++p) {
        atm_glb.prims[p].alpha_mask = 0;
    }
    // ATM night screen: emissive comes free if any prim carries tex_emit.
    {
        bool atm_emit = false;
        for (u32 p = 0; p < atm_glb.nprims && !atm_emit; ++p) {
            atm_emit = atm_glb.prims[p].tex_emit != 0;
        }
        if (atm_glb.nprims > 0 && !atm_emit) {
            std::printf("[props] ATM has no emissive texture — screen will not glow at night\n");
        }
    }
    std::printf("[buildings] All building primitives now render as OPAQUE (no alpha blending)\n");
    // Startup texture audit (GL readback, once): mean color + hot-magenta texel
    // fraction per prim pinpoints baked placeholder textures vs. missing binds.
    // A magenta building whose prims scan clean is tinted by vertex color/COLOR_0
    // in the asset, not by sampling — also re-authored, not placement.
    {
        const TreeGlb* arrs[3] = {shop_glb, apartment_glb, warehouse_glb};
        const char* tnames[3] = {"shop", "apartment", "warehouse"};
        const char* vnames[9] = {"shop_small_variant_a", "shop_small_variant_b",
                                 "shop_small_variant_c", "apartment_5story_variant_a",
                                 "apartment_5story_variant_b", "apartment_5story_variant_c",
                                 "warehouse_industrial_variant_a", "warehouse_industrial_variant_b",
                                 "warehouse_industrial_variant_c"};
        for (u32 t = 0; t < 3u; ++t) {
            for (u32 v = 0; v < 3u; ++v) {
                const TreeGlb& g = arrs[t][v];
                const char* label = vnames[t * 3u + v];
                for (u32 p = 0; p < g.nprims; ++p) {
                    const TreePrim& pr = g.prims[p];
                    if (pr.tex == 0) {
                        std::printf("[buildings] texscan: %s prim %u: NO TEXTURE "
                                    "(white fallback bound; renders solid %s)\n",
                                    label, p, tnames[t]);
                        continue;
                    }
                    GLint tw = 0, th = 0;
                    glBindTexture(GL_TEXTURE_2D, pr.tex);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
                    if (tw <= 0 || th <= 0 || tw > 2048 || th > 2048) {
                        std::printf("[buildings] texscan: %s prim %u: unreadable size %dx%d\n",
                                    label, p, tw, th);
                        continue;
                    }
                    u8* px = static_cast<u8*>(std::malloc(static_cast<usize>(tw) * th * 4));
                    if (!px) {
                        continue;
                    }
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                    u64 sr = 0, sg = 0, sb = 0;
                    u32 mag = 0;
                    const u32 n = static_cast<u32>(tw) * static_cast<u32>(th);
                    for (u32 i = 0; i < n; ++i) {
                        const u8 r = px[i * 4u], gg = px[i * 4u + 1u], b = px[i * 4u + 2u];
                        sr += r;
                        sg += gg;
                        sb += b;
                        if (r > 230 && b > 220 && gg < 50) {
                            ++mag;
                        }
                    }
                    std::free(px);
                    std::printf("[buildings] texscan: %s prim %u %dx%d mean=(%u,%u,%u) "
                                "magenta=%u (%.2f%%)%s\n",
                                label, p, tw, th, (u32)(sr / n), (u32)(sg / n), (u32)(sb / n),
                                mag, 100.0 * (double)mag / (double)n,
                                mag > n / 20 ? " <-- BAKED PLACEHOLDER, re-author asset" : "");
                }
            }
        }
        std::fflush(stdout);
    }
    // Startup node-rotation audit: which of the 9 files bakes a tilt into a mesh node.
    {
        const char* files[9] = {
            "shop_small_variant_a.glb", "shop_small_variant_b.glb", "shop_small_variant_c.glb",
            "apartment_5story_variant_a.glb", "apartment_5story_variant_b.glb",
            "apartment_5story_variant_c.glb", "warehouse_industrial_variant_a.glb",
            "warehouse_industrial_variant_b.glb", "warehouse_industrial_variant_c.glb"};
        const char* labels[9] = {
            "shop_small_variant_a", "shop_small_variant_b", "shop_small_variant_c",
            "apartment_5story_variant_a", "apartment_5story_variant_b",
            "apartment_5story_variant_c", "warehouse_industrial_variant_a",
            "warehouse_industrial_variant_b", "warehouse_industrial_variant_c"};
        std::printf("[buildings] --- NODE ROTATION AUDIT ---\n");
        for (u32 i = 0; i < 9u; ++i) {
            report_glb_node_rotations(files[i], labels[i]);
        }
        std::fflush(stdout);
    }
    std::printf("[buildings] ==========================================\n\n");
    std::fflush(stdout);
    constexpr const char* kGlowFbVs =
        "#version 330 core\nlayout(location=0) in vec3 aPos; layout(location=3) in vec4 iM0;\n"
        "layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;\n"
        "uniform mat4 view,projection; out vec2 UV;\n"
        "void main(){ mat4 model=mat4(iM0,iM1,iM2,iM3); UV=aPos.xz; gl_Position=projection*view*model*vec4(aPos,1.0); }\n";
    constexpr const char* kGlowFbFs =
        "#version 330 core\nin vec2 UV; uniform float uGlow; out vec4 FragColor;\n"
        "void main(){ float d=length(UV); float a=smoothstep(1.0,0.12,d)*uGlow*0.62; "
        "if(a<0.012) discard; FragColor=vec4(1.0,0.843,0.0,a); }\n";
    glow_prog = make_program("shaders/lamp_glow.vert", "shaders/lamp_glow.frag", kGlowFbVs, kGlowFbFs,
                             "lamp_glow");
    {
        constexpr u32 kSeg = 20;
        float gv[(1u + kSeg) * 3u];
        unsigned gi[kSeg * 3u];
        gv[0] = 0.f;
        gv[1] = 0.f;
        gv[2] = 0.f;
        for (u32 i = 0; i < kSeg; ++i) {
            const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(kSeg);
            gv[(i + 1u) * 3u + 0u] = std::cos(a);
            gv[(i + 1u) * 3u + 1u] = 0.f;
            gv[(i + 1u) * 3u + 2u] = std::sin(a);
            gi[i * 3u + 0u]        = 0;
            gi[i * 3u + 1u]        = 1u + i;
            gi[i * 3u + 2u]        = 1u + ((i + 1u) % kSeg);
        }
        glow_nidx = kSeg * 3u;
        glGenVertexArrays(1, &glow_vao);
        glGenBuffers(1, &glow_vbo);
        glGenBuffers(1, &glow_ibo);
        glGenBuffers(1, &glow_ivbo);
        glBindVertexArray(glow_vao);
        glBindBuffer(GL_ARRAY_BUFFER, glow_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(gv)), gv, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glow_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(gi)), gi, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, glow_ivbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kTreeInstanceCap * 16 * sizeof(float)), nullptr,
                     GL_DYNAMIC_DRAW);
        const u32 stride = 16 * sizeof(float);
        for (u32 k = 0; k < 4; ++k) {
            glEnableVertexAttribArray(3 + k);
            glVertexAttribPointer(3 + k, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                                  reinterpret_cast<void*>(k * 4 * sizeof(float)));
            glVertexAttribDivisor(3 + k, 1);
        }
        glBindVertexArray(0);
    }
    std::printf("[gl] BUILDING SHADER COMPILED SUCCESSFULLY\n");
    std::fflush(stdout);
    ok = true;
    return true;
}

void BuildingGlPass::buildMesh(World& world) {
    num_buildings = 0;
    for (Entity e : world.query<BuildingComponent>()) {
        (void)e;
        ++num_buildings;
    }

    // Non-overlapping tiles: intersections + EW spans + NS spans. Full-length
    // ribbons z-fought at every crossing.
    static float verts[kStreetVertCap * 5];
    u32 n = 0;
    const float y      = kRoadY;
    const float half_w = kCityStreetWidth * 0.5f + 3.f; // 10 m asphalt + 3 m sidewalk
    const float pitch  = kCityBlockPitch;
    const u32   nline  = kCityBlocks + 1;

    // Intersections as 5 asphalt quads (centre box + 4 arm boxes): the roads run
    // straight through. Corners belong to the sidewalk ring mesh (own VAO).
    for (u32 j = 0; j < nline; ++j) {
        for (u32 i = 0; i < nline; ++i) {
            const float cx = static_cast<float>(i) * pitch;
            const float cz = static_cast<float>(j) * pitch;
            const float u_in0 = 3.f / 26.f;  // u at 10 m from centre (asphalt edge)
            const float u_in1 = 23.f / 26.f; // u at 10 m, far side
            // Asphalt centre box +-10.
            emit_aabb_quad(verts, &n, cx - 10.f, cz - 10.f, cx + 10.f, cz + 10.f, y, 0.5f, 0.f,
                           0.5f, 0.f, 0.5f, 0.f, 0.5f, 0.f);
            // Asphalt arms N/S (x +-10, z 10..13): u continues the EW spans.
            emit_aabb_quad(verts, &n, cx - 10.f, cz + 10.f, cx + 10.f, cz + 13.f, y, u_in1, 0.f,
                           u_in1, 0.f, 1.f, 0.f, 1.f, 0.f);
            emit_aabb_quad(verts, &n, cx - 10.f, cz - 13.f, cx + 10.f, cz - 10.f, y, 0.f, 0.f,
                           0.f, 0.f, u_in0, 0.f, u_in0, 0.f);
            // Asphalt arms E/W (x 10..13, z +-10): u continues the NS spans.
            emit_aabb_quad(verts, &n, cx + 10.f, cz - 10.f, cx + 13.f, cz + 10.f, y, u_in0, 0.f,
                           u_in0, 0.f, u_in1, 0.f, u_in1, 0.f);
            emit_aabb_quad(verts, &n, cx - 13.f, cz - 10.f, cx - 10.f, cz + 10.f, y, u_in0, 0.f,
                           u_in0, 0.f, u_in1, 0.f, u_in1, 0.f);
        }
    }
    for (u32 j = 0; j < nline; ++j) {
        for (u32 i = 0; i < kCityBlocks; ++i) {
            const float z  = static_cast<float>(j) * pitch;
            const float x0 = static_cast<float>(i) * pitch + half_w;
            const float x1 = static_cast<float>(i + 1) * pitch - half_w;
            const float len = (x1 - x0) / 10.f;
            // UV.x across Z, UV.y along X
            emit_aabb_quad(verts, &n, x0, z - half_w, x1, z + half_w, y, 0.f, 0.f, 0.f, len, 1.f, len, 1.f,
                           0.f);
        }
    }
    for (u32 i = 0; i < nline; ++i) {
        for (u32 j = 0; j < kCityBlocks; ++j) {
            const float x  = static_cast<float>(i) * pitch;
            const float z0 = static_cast<float>(j) * pitch + half_w;
            const float z1 = static_cast<float>(j + 1) * pitch - half_w;
            const float len = (z1 - z0) / 10.f;
            // UV.x across X, UV.y along Z
            emit_aabb_quad(verts, &n, x - half_w, z0, x + half_w, z1, y, 0.f, 0.f, 1.f, 0.f, 1.f, len, 0.f,
                           len);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, street_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(n * 5 * sizeof(float)), verts);
    street_count = n;
    std::printf("[city] gpu mesh buildings=%u street_verts=%u (non-overlapping tiles)\n", num_buildings,
                n);
    std::fflush(stdout);

    // Lot ground: one static quad per block interior. Spans road-half inset with a
    // 0.5 m overlap tucking under the road/sidewalk band (no seams), 4 cm below
    // the road quads (no z-fight; roads win the overlap), same winding as street
    // quads (drawn unculled). 400 quads = 1600 verts / 2400 indices, GL_STATIC_DRAW.
    {
        static SolidVert lot_verts[kCityBlocks * kCityBlocks * 4];
        static u32 lot_idx[kCityBlocks * kCityBlocks * 6];
        u32 lot_vn = 0, lot_in = 0;
        const float lot_y = kTopY;
        // Lot spans the FULL block [10,110]^2 and tucks UNDER the sidewalk ring
        // (ring above hides the edge): no green poke-through, no slit.
        const float inset = kCityStreetWidth * 0.5f;
        for (u32 bz = 0; bz < kCityBlocks; ++bz) {
            for (u32 bx = 0; bx < kCityBlocks; ++bx) {
                const float x0 = static_cast<float>(bx) * kCityBlockPitch + inset;
                const float x1 = static_cast<float>(bx + 1) * kCityBlockPitch - inset;
                const float z0 = static_cast<float>(bz) * kCityBlockPitch + inset;
                const float z1 = static_cast<float>(bz + 1) * kCityBlockPitch - inset;
                const u32 base = lot_vn;
                // World-space tiling (world/5.0 m): identical grass scale on every
                // block, seamless across neighbours; blades read up close.
                solid_push(lot_verts, &lot_vn, x0, lot_y, z0, 0.f, 1.f, 0.f, x0 * (1.f / 5.0f),
                           z0 * (1.f / 5.0f));
                solid_push(lot_verts, &lot_vn, x1, lot_y, z0, 0.f, 1.f, 0.f, x1 * (1.f / 5.0f),
                           z0 * (1.f / 5.0f));
                solid_push(lot_verts, &lot_vn, x1, lot_y, z1, 0.f, 1.f, 0.f, x1 * (1.f / 5.0f),
                           z1 * (1.f / 5.0f));
                solid_push(lot_verts, &lot_vn, x0, lot_y, z1, 0.f, 1.f, 0.f, x0 * (1.f / 5.0f),
                           z1 * (1.f / 5.0f));
                lot_idx[lot_in++] = base + 0;
                lot_idx[lot_in++] = base + 1;
                lot_idx[lot_in++] = base + 2;
                lot_idx[lot_in++] = base + 0;
                lot_idx[lot_in++] = base + 2;
                lot_idx[lot_in++] = base + 3;
            }
        }
        upload_solid(&lot_vao, &lot_ibo, &lot_count, lot_verts, lot_vn, lot_idx, lot_in);
        std::printf("[city] lot mesh: verts=%u (expect %u)\n", lot_in,
                    kCityBlocks * kCityBlocks * 6);
        std::fflush(stdout);
    }

    // Sidewalk ring: 4 textured strips per block (N/S span full x so corners are
    // covered — no corner quads, no holes). Same Y as the road (clean edge);
    // polygon offset at draw time wins the overlap with the road quads below.
    // UVs are world-space / 2 m with GL_REPEAT.
    {
        static SolidVert ring_verts[kCityBlocks * kCityBlocks * 16];
        static u32 ring_idx[kCityBlocks * kCityBlocks * 24];
        u32 ring_vn = 0, ring_in = 0;
        const float ring_y = kTopY;
        const float sw = 3.0f;
        for (u32 bz = 0; bz < kCityBlocks; ++bz) {
            for (u32 bx = 0; bx < kCityBlocks; ++bx) {
                const float x0 = static_cast<float>(bx) * kCityBlockPitch + 10.f;
                const float x1 = static_cast<float>(bx + 1u) * kCityBlockPitch - 10.f;
                const float z0 = static_cast<float>(bz) * kCityBlockPitch + 10.f;
                const float z1 = static_cast<float>(bz + 1u) * kCityBlockPitch - 10.f;
                // N: full x, S: full x, W/E: inner z only.
                const float q[4][4] = {{x0, z0, x1, z0 + sw},
                                       {x0, z1 - sw, x1, z1},
                                       {x0, z0 + sw, x0 + sw, z1 - sw},
                                       {x1 - sw, z0 + sw, x1, z1 - sw}};
                for (u32 s = 0; s < 4u; ++s) {
                    const float ax0 = q[s][0], az0 = q[s][1], ax1 = q[s][2], az1 = q[s][3];
                    const u32 base = ring_vn;
                    solid_push(ring_verts, &ring_vn, ax0, ring_y, az0, 0.f, 1.f, 0.f,
                               ax0 * 0.5f, az0 * 0.5f);
                    solid_push(ring_verts, &ring_vn, ax1, ring_y, az0, 0.f, 1.f, 0.f,
                               ax1 * 0.5f, az0 * 0.5f);
                    solid_push(ring_verts, &ring_vn, ax1, ring_y, az1, 0.f, 1.f, 0.f,
                               ax1 * 0.5f, az1 * 0.5f);
                    solid_push(ring_verts, &ring_vn, ax0, ring_y, az1, 0.f, 1.f, 0.f,
                               ax0 * 0.5f, az1 * 0.5f);
                    ring_idx[ring_in++] = base + 0;
                    ring_idx[ring_in++] = base + 1;
                    ring_idx[ring_in++] = base + 2;
                    ring_idx[ring_in++] = base + 0;
                    ring_idx[ring_in++] = base + 2;
                    ring_idx[ring_in++] = base + 3;
                }
            }
        }
        upload_solid(&ring_vao, &ring_ibo, &ring_count, ring_verts, ring_vn, ring_idx,
                     ring_in);
        std::printf("[city] sidewalk ring: quads=%u (expect %u)\n", ring_in / 6u,
                    kCityBlocks * kCityBlocks * 4u);
        std::printf("[city] sidewalk tint=(%.2f,%.2f,%.2f)\n", static_cast<double>(kSidewalkTint.x),
                    static_cast<double>(kSidewalkTint.y), static_cast<double>(kSidewalkTint.z));
        std::fflush(stdout);
    }

    // Solid slab skirt: vertical concrete walls around each block's raised
    // footprint ([10,110]^2), from kTopY down to kRoadY-0.10. Drawn cull-off so
    // the block reads solid from both sides; clear-color can never show under
    // an edge. Own VAO (flat gray albedo, no texture).
    {
        static SolidVert skirt_verts[kCityBlocks * kCityBlocks * 16];
        static u32 skirt_idx[kCityBlocks * kCityBlocks * 24];
        u32 skirt_vn = 0, skirt_in = 0;
        const float y_top = kTopY;
        const float y_bot = kRoadY - 0.10f;
        for (u32 bz = 0; bz < kCityBlocks; ++bz) {
            for (u32 bx = 0; bx < kCityBlocks; ++bx) {
                const float x0 = static_cast<float>(bx) * kCityBlockPitch + 10.f;
                const float x1 = static_cast<float>(bx + 1u) * kCityBlockPitch - 10.f;
                const float z0 = static_cast<float>(bz) * kCityBlockPitch + 10.f;
                const float z1 = static_cast<float>(bz + 1u) * kCityBlockPitch - 10.f;
                // wall quads: (a along edge, top/bottom). Normals face outward.
                const float w[4][6] = {
                    {x0, z0, x1, z0, 0.f, -1.f}, // south face (faces -z road)
                    {x1, z1, x0, z1, 0.f, 1.f},  // north face (faces +z road)
                    {x0, z1, x0, z0, -1.f, 0.f}, // west face (faces -x road)
                    {x1, z0, x1, z1, 1.f, 0.f},  // east face (faces +x road)
                };
                for (u32 s = 0; s < 4u; ++s) {
                    const u32 base = skirt_vn;
                    solid_push(skirt_verts, &skirt_vn, w[s][0], y_top, w[s][1], w[s][4], 0.f,
                               w[s][5], 0.f, 0.f);
                    solid_push(skirt_verts, &skirt_vn, w[s][2], y_top, w[s][3], w[s][4], 0.f,
                               w[s][5], 1.f, 0.f);
                    solid_push(skirt_verts, &skirt_vn, w[s][2], y_bot, w[s][3], w[s][4], 0.f,
                               w[s][5], 1.f, 1.f);
                    solid_push(skirt_verts, &skirt_vn, w[s][0], y_bot, w[s][1], w[s][4], 0.f,
                               w[s][5], 0.f, 1.f);
                    skirt_idx[skirt_in++] = base + 0;
                    skirt_idx[skirt_in++] = base + 1;
                    skirt_idx[skirt_in++] = base + 2;
                    skirt_idx[skirt_in++] = base + 0;
                    skirt_idx[skirt_in++] = base + 2;
                    skirt_idx[skirt_in++] = base + 3;
                }
            }
        }
        upload_solid(&skirt_vao, &skirt_ibo, &skirt_count, skirt_verts, skirt_vn, skirt_idx,
                     skirt_in);
        std::printf("[city] levels: road=%.2f curb=%.2f top=%.2f; skirt quads=%u\n",
                    static_cast<double>(kRoadY), static_cast<double>(kCurbH),
                    static_cast<double>(kTopY), skirt_in / 6u);
        std::fflush(stdout);
    }

    static float sky_mats[kTreeInstanceCap * 16];
    u32 sky_n = 0;
    if (sky_glb.nprims > 0) {
        for (Entity e : world.query<BuildingComponent>()) {
            BuildingComponent* b = world.get<BuildingComponent>(e);
            if (!custom_sky_lot(b) || sky_n >= 16) {
                continue;
            }
            const float yaw = static_cast<float>(b->building_id % 4u) * 1.5707963f;
            const float sc  = 0.95f + static_cast<float>(b->building_id % 6u) * (0.10f / 5.f);
            tree_yaw_mat(&sky_mats[sky_n * 16], b->position.x, kCityPlateauY + 0.05f, b->position.z, yaw, sc,
                         sky_glb.z_up);
            ++sky_n;
        }
        tree_glb_set_instances(&sky_glb, sky_mats, sky_n);
    }
    std::printf("[city] Placing %u custom skyscrapers in downtown\n", sky_n);
    std::fflush(stdout);

    static float lamp_mats[kLampKindCount][kTreeInstanceCap * 16];
    static float glow_mats[kTreeInstanceCap * 16];
    static float lamp_xz[kLampSpawnCap * 2];
    u32 ln[kLampKindCount] = {0, 0};
    u32 gn                 = 0;
    u32 n_lamp_xz          = 0;
    u32 lamp_log_n         = 0;
    const float lamp_fit[kLampKindCount] = {glb_fit_scale(&lamp_glb[0], 7.0f),
                                            glb_fit_scale(&lamp_glb[1], 6.5f)};
    const float road_h     = kCityStreetWidth * 0.5f;
    const float walk_w     = 3.0f;
    const float lamp_off   = road_h + walk_w * 0.5f; // 11.5 m = sidewalk center
    const float tree_off   = 12.25f;                 // sidewalk, 12.25 m from road center
    const float cross_clear = 16.0f;
    auto along_ok = [&](float t) {
        const float g = t / kCityBlockPitch;
        const float f = g - std::floor(g);
        const float d = f < 0.5f ? f * kCityBlockPitch : (1.f - f) * kCityBlockPitch;
        return d >= cross_clear;
    };
    auto dist_to_road_edge = [&](float x, float z) {
        const float nx = std::round(x / kCityBlockPitch) * kCityBlockPitch;
        const float nz = std::round(z / kCityBlockPitch) * kCityBlockPitch;
        const float ax = std::fabs(x - nx);
        const float az = std::fabs(z - nz);
        const float d  = ax < az ? ax : az;
        return d - road_h;
    };
    auto push_lamp = [&](float x, float z, int ew, float side) {
        const float droad = dist_to_road_edge(x, z);
        const int on_sw   = (droad >= 0.4f && droad <= 3.2f) ? 1 : 0;
        if (!on_sw || droad > 15.f) {
            if (lamp_log_n < 8u) {
                std::printf("[city] Lamp at (%.1f, %.1f, %.1f) - on sidewalk: NO, distance to nearest road: %.1f meters\n",
                            x, kCityPlateauY + 0.05f, z, droad);
                ++lamp_log_n;
            }
            return;
        }
        if ((ln[0] + ln[1]) >= kLampSpawnCap) {
            return;
        }
        u32 use = (ln[0] + ln[1]) & 1u;
        if (lamp_glb[use].nprims == 0) {
            use ^= 1u;
        }
        if (lamp_glb[use].nprims == 0 || ln[use] >= kTreeInstanceCap) {
            return;
        }
        // Same sidewalk. +π — both klassisk and moderne face the other way.
        float yaw = ew ? ((side > 0.f) ? -1.5707963f : 1.5707963f)
                       : ((side > 0.f) ? 0.f : 3.14159265f);
        yaw += 3.14159265f;
        const float sc  = lamp_fit[use];
        const float y0  = lamp_glb[use].z_up ? lamp_glb[use].zmin : lamp_glb[use].ymin;
        const float y   = kCityPlateauY + 0.05f - y0 * sc;
        tree_yaw_mat(&lamp_mats[use][ln[use] * 16], x, y, z, yaw, sc, lamp_glb[use].z_up);
        ++ln[use];
        if (n_lamp_xz < kLampSpawnCap) {
            lamp_xz[n_lamp_xz * 2u]     = x;
            lamp_xz[n_lamp_xz * 2u + 1] = z;
            ++n_lamp_xz;
        }
        if (gn < kTreeInstanceCap) {
            tree_yaw_mat(&glow_mats[gn * 16], x, kCityPlateauY + 0.28f, z, 0.f, 5.2f, 0);
            ++gn;
        }
        if (lamp_log_n < 12u) {
            std::printf("[city] Lamp at (%.1f, %.1f, %.1f) - on sidewalk: YES, distance to nearest road: %.1f meters\n",
                        x, y, z, droad);
            ++lamp_log_n;
        }
    };
    for (u32 j = 0; j <= kCityBlocks; ++j) {
        const float z = static_cast<float>(j) * kCityBlockPitch;
        for (float x = 24.f; x < kCityExtentM - 24.f; x += 36.f) {
            if (!along_ok(x)) {
                continue;
            }
            push_lamp(x, z + lamp_off, 1, 1.f);
            push_lamp(x, z - lamp_off, 1, -1.f);
        }
    }
    for (u32 i = 0; i <= kCityBlocks; ++i) {
        const float x = static_cast<float>(i) * kCityBlockPitch;
        for (float z = 24.f; z < kCityExtentM - 24.f; z += 36.f) {
            if (!along_ok(z)) {
                continue;
            }
            push_lamp(x + lamp_off, z, 0, 1.f);
            push_lamp(x - lamp_off, z, 0, -1.f);
        }
    }
    for (u32 k = 0; k < kLampKindCount; ++k) {
        tree_glb_set_instances(&lamp_glb[k], lamp_mats[k], ln[k]);
    }
    glow_count = gn;
    if (glow_ivbo) {
        glBindBuffer(GL_ARRAY_BUFFER, glow_ivbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(gn * 16 * sizeof(float)), glow_mats);
    }
    std::printf("[city] Placing %u street lamps (klassisk: %u, moderne: %u)\n", ln[0] + ln[1], ln[0], ln[1]);
    std::fflush(stdout);

#if ENABLE_TREES
    static float tree_mats[kTreeKindCount][kTreeInstanceCap * 16];
    static float tree_xz[kTreeSpawnCap * 2];
    u32 tn[kTreeKindCount] = {0, 0, 0};
    u32 n_tree_xz          = 0;
    u32 skip_lamp          = 0;
    u32 skip_tree          = 0;
    auto push_tree = [&](float x, float z) {
        const u32 total = tn[0] + tn[1] + tn[2];
        if (total >= kTreeSpawnCap) {
            return;
        }
        const float droad = dist_to_road_edge(x, z);
        const int on_sw   = (droad >= 0.4f && droad <= 3.2f) ? 1 : 0;
        if (!on_sw) {
            return;
        }
        if (xz_too_close(x, z, lamp_xz, n_lamp_xz, 2.0f)) {
            ++skip_lamp;
            return;
        }
        if (xz_too_close(x, z, tree_xz, n_tree_xz, 3.0f)) {
            ++skip_tree;
            return;
        }
        const u32 kind = (static_cast<u32>(x) / 24u + static_cast<u32>(z) / 24u) % 3u;
        if (tn[kind] >= kTreeInstanceCap || tree_glb[kind].nprims == 0) {
            return;
        }
        const float yaw = std::fmod(x * 0.173f + z * 0.091f, 6.2831853f);
        const u32 sh = static_cast<u32>(x) * 1664525u + static_cast<u32>(z) * 1013904223u;
        const float fit = tree_fit_scale(&tree_glb[kind], kind == 1 ? 9.f : 8.f);
        const float sc = fit * (0.85f + static_cast<float>(sh % 1000u) * (0.30f / 999.f));
        const int zup   = tree_glb[kind].z_up;
        const float y0  = tree_up_min(&tree_glb[kind]);
        const float y   = kCityPlateauY + 0.05f - y0 * sc;
        tree_yaw_mat(&tree_mats[kind][tn[kind] * 16], x, y, z, yaw, sc, zup);
        ++tn[kind];
        tree_xz[n_tree_xz * 2u]     = x;
        tree_xz[n_tree_xz * 2u + 1] = z;
        ++n_tree_xz;
    };
    for (u32 j = 0; j <= kCityBlocks; ++j) {
        const float z = static_cast<float>(j) * kCityBlockPitch;
        for (float x = 36.f; x < kCityExtentM - 36.f; x += 24.f) {
            if (!along_ok(x)) {
                continue;
            }
            const float side = (static_cast<u32>(x) % 48u < 24u) ? tree_off : -tree_off;
            push_tree(x, z + side);
        }
    }
    for (u32 i = 0; i <= kCityBlocks; ++i) {
        const float x = static_cast<float>(i) * kCityBlockPitch;
        for (float z = 36.f; z < kCityExtentM - 36.f; z += 24.f) {
            if (!along_ok(z)) {
                continue;
            }
            const float side = (static_cast<u32>(z) % 48u < 24u) ? tree_off : -tree_off;
            push_tree(x + side, z);
        }
    }
    u32 total_trees = 0;
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        tree_glb_set_instances(&tree_glb[k], tree_mats[k], tn[k]);
        total_trees += tn[k];
    }
    (void)total_trees;
    (void)skip_lamp;
    (void)skip_tree;
#endif

    // === NEW GLB BUILDING PLACEMENT (replacement of procedural lots) ===
    // Strategy (startup only, zero per-frame cost):
    //   0. Three DEBUG GLBs at fixed valid lots (1140,1140 / 1260,1260 / 1980,300).
    //   1. Iterate the existing ~572 procedural BuildingComponents (shuffled order).
    //   2. District picks the type (retail/commercial->shop, industrial->warehouse,
    //      else apartment); 35% chance gate per building.
    //   3. building hash picks the variant so all 9 GLBs get used.
    //   4. The EXISTING building's X/Z/footprint is the base (uniform fit keeps the
    //      GLB on its lot — zero overlap, zero lot errors); yaw faces the nearest
    //      road, snapped to 90° (axis-aligned, audit-clean).
    //   5. Quotas (24 shops, 90 apartments, 86 warehouses) backfill from fitting
    //      lots so the targets are hit; replaced ids go in g_replaced_flag so the
    //      procedural box is skipped in draw() + shadow.
    //   6. Matrices are uploaded once via tree_glb_set_instances (same pipeline as trees/cars).
    {
        std::printf("\n=== [buildings] PLACEMENT START ===\n");
        std::printf("[buildings] facing rule: model +Z toward nearest road, snapped to 90 deg\n");

        // Reset per-build state (buildMesh runs once at startup).
        // NOTE: verification_row is set by the sandbox between init() and buildMesh();
        // do NOT clear it here.
    shop_count = apartment_count = warehouse_count = 0;
        clear_replaced_buildings();

        // Per-variant instance matrices. Static: ~57KB per array, too big for the stack.
        // The GPU upload copies them, so locals are sufficient (no persistent CPU copy needed).
        static float shop_var_mats[3][kTreeInstanceCap * 16];
        static float apt_var_mats[3][kTreeInstanceCap * 16];
        static float whs_var_mats[3][kTreeInstanceCap * 16];
        u32 shop_var_n[3] = {0u, 0u, 0u};
        u32 apt_var_n[3] = {0u, 0u, 0u};
        u32 whs_var_n[3] = {0u, 0u, 0u};

        // FIXED lot test.
        // Roads are lines at multiples of kCityBlockPitch (0, 120, 240, ... 2400).
        // Block centers are at 60 + 120*n (60, 180, 300, ...).
        // A position is a valid lot iff it is at least 14m (10m road half + 3m
        // sidewalk + 1m margin) away from EVERY road line.
        // The OLD code used round(x/pitch)*pitch (= nearest ROAD) then treated that
        // distance as "distance from block center" and required it to be <= 45m,
        // which rejects the true centers (60m from any road). Fixed with fmod below.
        auto on_building_lot_fixed = [&](float x, float z) -> bool {
            if (x < 0.f || x > kCityExtentM || z < 0.f || z > kCityExtentM) {
                return false;
            }
            float fx = std::fmod(x, kCityBlockPitch);
            if (fx < 0.f) {
                fx += kCityBlockPitch;
            }
            float fz = std::fmod(z, kCityBlockPitch);
            if (fz < 0.f) {
                fz += kCityBlockPitch;
            }
            const float dx_road = fx < kCityBlockPitch - fx ? fx : kCityBlockPitch - fx;
            const float dz_road = fz < kCityBlockPitch - fz ? fz : kCityBlockPitch - fz;
            const float keep = kCityStreetWidth * 0.5f + 3.0f + 1.0f; // 14m
            if (dx_road < keep || dz_road < keep) {
                return false; // on road / sidewalk / intersection
            }
            return true;
        };

        // Replacement candidates: every procedural lot we may swap a GLB onto.
        // Position + footprint come from the generator, so the GLB always sits on
        // a valid lot with a matching footprint (no random XZ, no overlaps).
        struct Candidate {
            float x, z, w, d, h;
            u32 district, id;
        };
        static Candidate cands[4096];
        u32 n_cands = 0;
        u32 skipped_sky = 0, skipped_tall = 0;
        for (Entity e : world.query<BuildingComponent>()) {
            BuildingComponent* b = world.get<BuildingComponent>(e);
            if (!b) {
                continue;
            }
            if (custom_sky_lot(b)) {
                ++skipped_sky;
                continue;
            }
            // Never replace supertalls with a 1-5 story GLB (would look sunken).
            if (b->height > 55.f) {
                ++skipped_tall;
                continue;
            }
            if (n_cands < 4096) {
                cands[n_cands++] = {b->position.x, b->position.z, b->width, b->depth,
                                    b->height, b->district, b->building_id};
            }
        }
        static u8 taken[4096] = {0};
        for (u32 i = 0; i < n_cands; ++i) {
            taken[i] = 0;
        }

        // Street-facing yaw: model front (+Z) toward the nearest road line,
        // snapped to 90° so buildings stay axis-aligned. World facing of +Z
        // under Ry(yaw) is (sin yaw, 0, cos yaw); atan2 of the axis-aligned
        // road direction is already a multiple of 90° (yaw-only, audit-clean).
        auto street_facing_yaw = [](float x, float z) -> float {
            float fx = std::fmod(x, kCityBlockPitch);
            if (fx < 0.f) {
                fx += kCityBlockPitch;
            }
            float fz = std::fmod(z, kCityBlockPitch);
            if (fz < 0.f) {
                fz += kCityBlockPitch;
            }
            const float dx_road = fx < kCityBlockPitch - fx ? fx : kCityBlockPitch - fx;
            const float dz_road = fz < kCityBlockPitch - fz ? fz : kCityBlockPitch - fz;
            float dirx = 0.f, dirz = 0.f;
            if (dx_road <= dz_road) {
                dirx = (fx < kCityBlockPitch * 0.5f) ? -1.f : 1.f;
            } else {
                dirz = (fz < kCityBlockPitch * 0.5f) ? -1.f : 1.f;
            }
            return std::atan2(dirx, dirz);
        };

        // --- 3 DEBUG placements (fixed positions, proof the loader/render path works) ---
        // Fixed valid-lot coordinates (block centers: 60 + 120*n). Any procedural
        // box within 25 m is hidden via g_replaced_flag so there is zero overlap.
        struct DebugSpot {
            float x, z, w, d;
            int type; // 0 shop, 1 apartment, 2 warehouse
            const char* label;
        };
        constexpr DebugSpot kDebugSpots[3] = {
            {1140.f, 1140.f, 20.f, 14.f, 0, "shop"},
            {1260.f, 1260.f, 40.f, 30.f, 1, "apartment"},
            {1980.f, 300.f, 60.f, 40.f, 2, "warehouse"},
        };
        u32 skipped_nomesh = 0, skipped_cap = 0, skipped_fit = 0;
        u32 lot_ok = 0, lot_fail = 0;
        u32 placed_shop = 0, placed_apt = 0, placed_whs = 0;
        u32 attempted_shop = 0, attempted_apt = 0, attempted_whse = 0;

        auto place_variant_at = [&](float x, float z, float w, float d, u32 id_for_seed,
                                    TreeGlb (&arr)[3], u32* var_n,
                                    float (*var_mats)[kTreeInstanceCap * 16], float yaw,
                                    const char* what) -> bool {
            u32 variant = id_for_seed % 3u;
            if (arr[variant].nprims == 0) {
                u32 alt = 3u;
                for (u32 v = 0; v < 3u; ++v) {
                    if (arr[v].nprims > 0) {
                        alt = v;
                        break;
                    }
                }
                if (alt >= 3u) {
                    ++skipped_nomesh;
                    std::printf("[buildings] DEBUG: %s NOT placed (no mesh loaded)\n", what);
                    return false;
                }
                variant = alt;
            }
            if (on_building_lot_fixed(x, z)) {
                ++lot_ok;
            } else {
                ++lot_fail;
            }
            const TreeGlb* g = &arr[variant];
            // Doubled city-wide: every placed GLB renders at exactly 2x natural
            // size (all variants a/b/c, all types). Lots far smaller than the
            // model (<0.75 fit) still keep their procedural building.
            const float fit = building_footprint_scale(g, w, d);
            if (fit < 0.75f) {
                ++skipped_fit; // lot far smaller than the model — keep procedural
                return false;
            }
            const float sc = 2.f;
            const float y = kCityPlateauY + 0.05f - tree_up_min(g) * sc;
            if (var_n[variant] >= kTreeInstanceCap) {
                ++skipped_cap;
                return false;
            }
            tree_yaw_mat(&var_mats[variant][var_n[variant] * 16], x, y, z, yaw, sc,
                         g->z_up);
            ++var_n[variant];
            return true;
        };

        for (u32 di = 0; di < 3u; ++di) {
            const DebugSpot& s = kDebugSpots[di];
            std::printf("[buildings] DEBUG: Placing %s at FIXED position (%.1f, %.1f)\n", s.label,
                        static_cast<double>(s.x), static_cast<double>(s.z));
            TreeGlb (&arr)[3] =
                (s.type == 0) ? shop_glb : ((s.type == 2) ? warehouse_glb : apartment_glb);
            u32* var_n = (s.type == 0) ? shop_var_n : ((s.type == 2) ? whs_var_n : apt_var_n);
            float (*var_mats)[kTreeInstanceCap * 16] =
                (s.type == 0) ? shop_var_mats : ((s.type == 2) ? whs_var_mats : apt_var_mats);
            if (place_variant_at(s.x, s.z, s.w, s.d, 1000u + di, arr, var_n, var_mats, street_facing_yaw(s.x, s.z),
                                 s.label)) {
                std::printf("[buildings] DEBUG: %s placed successfully!\n", s.label);
                if (s.type == 0) {
                    ++placed_shop;
                    ++attempted_shop;
                } else if (s.type == 2) {
                    ++placed_whs;
                    ++attempted_whse;
                } else {
                    ++placed_apt;
                    ++attempted_apt;
                }
                // Hide nearby procedural boxes so the fixed GLB never overlaps one.
                for (u32 i = 0; i < n_cands; ++i) {
                    if (taken[i]) {
                        continue;
                    }
                    const float dx = cands[i].x - s.x;
                    const float dz = cands[i].z - s.z;
                    if (dx * dx + dz * dz < 25.f * 25.f) {
                        taken[i] = 1;
                        mark_replaced_building(cands[i].id);
                    }
                }
            }
        }

        std::printf("[buildings] === Mass replacement from existing buildings ===\n");

        // Deterministic 30-40% gate: hash of building_id, stable across runs.
        auto chance35 = [](u32 id) -> bool {
            u32 h = id * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return (h % 100u) < 35u;
        };
        // Deterministic shuffle: order candidates by hash so replacements spread
        // across the city instead of filling id order.
        static u32 order[4096];
        for (u32 i = 0; i < n_cands; ++i) {
            order[i] = i;
        }
        for (u32 i = 0; i < n_cands; ++i) {
            u32 h = cands[i].id * 40503u + 12345u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            const u32 j = i + (h % (n_cands - i));
            const u32 t = order[i];
            order[i] = order[j];
            order[j] = t;
        }

        // District -> type: shop/commercial, apartment/residential, warehouse/industrial.
        auto district_type = [](u32 district) -> int {
            if (district == kDistrictRetail || district == kDistrictCommercial) {
                return 0;
            }
            if (district == kDistrictIndustrial) {
                return 2;
            }
            return 1;
        };
        // Loose fit used only by the quota backfill (phase 2).
        auto fits_loose = [](const Candidate& c, int type) -> bool {
            if (type == 0) {
                return c.h < 12.f;
            }
            if (type == 1) {
                return c.h >= 10.f;
            }
            return c.h < 18.f && (c.w >= 20.f || c.d >= 20.f);
        };

        auto try_place_candidate = [&](u32 ci, int type) -> bool {
            Candidate& c = cands[ci];
            if (type == 0) {
                ++attempted_shop;
            } else if (type == 2) {
                ++attempted_whse;
            } else {
                ++attempted_apt;
            }
            TreeGlb (&arr)[3] =
                (type == 0) ? shop_glb : ((type == 2) ? warehouse_glb : apartment_glb);
            u32* var_n = (type == 0) ? shop_var_n : ((type == 2) ? whs_var_n : apt_var_n);
            float (*var_mats)[kTreeInstanceCap * 16] =
                (type == 0) ? shop_var_mats : ((type == 2) ? whs_var_mats : apt_var_mats);
            // Existing building's X/Z/scale are the base; front faces the street.
            const float yaw = street_facing_yaw(c.x, c.z);
            if (!place_variant_at(c.x, c.z, c.w, c.d, c.id + static_cast<u32>(type) * 7919u,
                                  arr, var_n, var_mats, yaw, "mass")) {
                return false;
            }
            taken[ci] = 1;
            mark_replaced_building(c.id);
            if (type == 0) {
                ++placed_shop;
            } else if (type == 2) {
                ++placed_whs;
            } else {
                ++placed_apt;
            }
            return true;
        };

        constexpr u32 kTargetShops = 24;
        constexpr u32 kTargetApts = 90;
        constexpr u32 kTargetWhse = 86;

        // Phase 1: strict district match with the 35% chance gate.
        for (u32 oi = 0; oi < n_cands; ++oi) {
            const u32 ci = order[oi];
            if (taken[ci]) {
                continue;
            }
            const int type = district_type(cands[ci].district);
            if ((type == 0 && placed_shop >= kTargetShops) ||
                (type == 1 && placed_apt >= kTargetApts) ||
                (type == 2 && placed_whs >= kTargetWhse)) {
                continue;
            }
            if (!chance35(cands[ci].id)) {
                continue;
            }
            try_place_candidate(ci, type);
            if (placed_shop >= kTargetShops && placed_apt >= kTargetApts &&
                placed_whs >= kTargetWhse) {
                break;
            }
        }
        // Phase 2: backfill remaining quotas from any fitting untaken lot (still
        // gated by the 35% chance, so the 30-40% replacement rate holds).
        for (u32 oi = 0; oi < n_cands; ++oi) {
            const u32 ci = order[oi];
            if (taken[ci]) {
                continue;
            }
            int type = -1;
            if (placed_whs < kTargetWhse && fits_loose(cands[ci], 2)) {
                type = 2;
            } else if (placed_shop < kTargetShops && fits_loose(cands[ci], 0)) {
                type = 0;
            } else if (placed_apt < kTargetApts && fits_loose(cands[ci], 1)) {
                type = 1;
            }
            if (type < 0) {
                continue;
            }
            if (!chance35(cands[ci].id ^ 0x9e3779b9u)) {
                continue;
            }
            try_place_candidate(ci, type);
            if (placed_shop >= kTargetShops && placed_apt >= kTargetApts &&
                placed_whs >= kTargetWhse) {
                break;
            }
        }
        // Phase 3: guarantee the targets — first-fit over anything left, no gate.
        for (u32 oi = 0; oi < n_cands; ++oi) {
            const u32 ci = order[oi];
            if (taken[ci]) {
                continue;
            }
            int type = -1;
            if (placed_whs < kTargetWhse) {
                type = 2;
            } else if (placed_shop < kTargetShops) {
                type = 0;
            } else if (placed_apt < kTargetApts) {
                type = 1;
            } else {
                break;
            }
            try_place_candidate(ci, type);
        }
        // Upload once via the existing instanced pipeline (same as trees/cars).
        for (u32 v = 0; v < 3u; ++v) {
            if (shop_glb[v].nprims > 0) {
                tree_glb_set_instances(&shop_glb[v], shop_var_mats[v], shop_var_n[v]);
            }
            if (apartment_glb[v].nprims > 0) {
                tree_glb_set_instances(&apartment_glb[v], apt_var_mats[v], apt_var_n[v]);
            }
            if (warehouse_glb[v].nprims > 0) {
                tree_glb_set_instances(&warehouse_glb[v], whs_var_mats[v], whs_var_n[v]);
            }
        }
        // Sandbox verification lineup (--bldg-row): one instance of each of the 9
        // variants in a row 30 m ahead of the spawn camera (x centered on 1200,
        // z=160, yaw 0, grounded). Appended after quotas; quota logs below are
        // unaffected (they use placed_*), member totals include the row.
        if (verification_row) {
            const float ref_w[3] = {14.f, 24.f, 30.f};
            const float ref_d[3] = {10.f, 18.f, 22.f};
            u32 row_placed = 0;
            for (u32 k = 0; k < 9u; ++k) {
                const u32 t = k / 3u; // 0 shops, 1 apartments, 2 warehouses
                const u32 v = k % 3u;
                TreeGlb* arr = (t == 0) ? shop_glb : ((t == 2) ? warehouse_glb : apartment_glb);
                if (arr[v].nprims == 0) {
                    continue;
                }
                float (*var_mats)[kTreeInstanceCap * 16] =
                    (t == 0) ? shop_var_mats : ((t == 2) ? whs_var_mats : apt_var_mats);
                u32* var_n =
                    (t == 0) ? shop_var_n : ((t == 2) ? whs_var_n : apt_var_n);
                if (var_n[v] >= kTreeInstanceCap) {
                    continue;
                }
                const TreeGlb* g = &arr[v];
                const float sc = building_footprint_scale(g, ref_w[t], ref_d[t]);
                const float x = 1200.f + (static_cast<float>(k) - 4.f) * 7.f;
                const float y = kCityPlateauY + 0.05f - tree_up_min(g) * sc;
                tree_yaw_mat(&var_mats[v][var_n[v] * 16], x, y, 160.f, 0.f, sc, g->z_up);
                ++var_n[v];
                ++row_placed;
            }
            for (u32 v = 0; v < 3u; ++v) {
                if (shop_glb[v].nprims > 0) {
                    tree_glb_set_instances(&shop_glb[v], shop_var_mats[v], shop_var_n[v]);
                }
                if (apartment_glb[v].nprims > 0) {
                    tree_glb_set_instances(&apartment_glb[v], apt_var_mats[v], apt_var_n[v]);
                }
                if (warehouse_glb[v].nprims > 0) {
                    tree_glb_set_instances(&warehouse_glb[v], whs_var_mats[v], whs_var_n[v]);
                }
            }
            std::printf("[buildings] verification row: %u/9 instances at z=160 (yaw 0)\n",
                        row_placed);
            std::fflush(stdout);
        }
        // Yaw-only audit (startup only): every instance must be uniform-scale +
        // Y rotation with y = plateau + 0.05 - up_min*scale. Anything else (tilt,
        // shear, non-uniform scale) is logged — a tilted GLB with a clean matrix
        // means the tilt is baked into the asset, not the placement.
        {
            const TreeGlb* arrs[3] = {shop_glb, apartment_glb, warehouse_glb};
            const u32* ns[3] = {shop_var_n, apt_var_n, whs_var_n};
            const float* mats[3] = {&shop_var_mats[0][0], &apt_var_mats[0][0],
                                    &whs_var_mats[0][0]};
            const char* names[3] = {"shop", "apartment", "warehouse"};
            u32 violations = 0;
            for (u32 t = 0; t < 3u; ++t) {
                for (u32 v = 0; v < 3u; ++v) {
                    const TreeGlb* g = &arrs[t][v];
                    for (u32 i = 0; i < ns[t][v]; ++i) {
                        const float* m = &mats[t][(v * kTreeInstanceCap + i) * 16];
                        const float cx = m[0], cy = m[1], cz = m[2];
                        const float ux = m[4], uy = m[5], uz = m[6];
                        const float fx = m[8], fy = m[9], fz = m[10];
                        const float lx = std::sqrt(cx * cx + cy * cy + cz * cz);
                        const float ly = std::sqrt(ux * ux + uy * uy + uz * uz);
                        const float lz = std::sqrt(fx * fx + fy * fy + fz * fz);
                        const float dot_xz =
                            (lx > 0.f && lz > 0.f) ? (cx * fx + cy * fy + cz * fz) / (lx * lz) : 0.f;
                        const float y_exp =
                            kCityPlateauY + 0.05f - tree_up_min(g) * (ly > 0.f ? ly : 1.f);
                        bool ok = (m[3] == 0.f && m[7] == 0.f && m[11] == 0.f && m[15] == 1.f);
                        ok = ok && lx > 0.f && std::fabs(lx - ly) < 1e-3f * lx &&
                             std::fabs(lx - lz) < 1e-3f * lx;
                        ok = ok && dot_xz > -1e-4f && dot_xz < 1e-4f;
                        if (g->z_up == 0) {
                            // Yaw-only: Y column must be straight up.
                            ok = ok && ux == 0.f && uz == 0.f && uy > 0.f;
                        }
                        ok = ok && std::fabs(m[13] - y_exp) < 0.02f;
                        if (!ok && violations < 8u) {
                            std::printf("[buildings] MATRIX VIOLATION %s variant %u inst %u at "
                                        "(%.1f, %.1f, %.1f): non-yaw rotation or bad y\n",
                                        names[t], v, i, static_cast<double>(m[12]),
                                        static_cast<double>(m[13]), static_cast<double>(m[14]));
                            ++violations;
                        } else if (!ok) {
                            ++violations;
                        }
                    }
                }
            }
            if (violations == 0) {
                std::printf("[buildings] Matrix audit: all instances yaw-only, y grounded\n");
            } else {
                std::printf("[buildings] Matrix audit: %u violating instances (see above)\n",
                            violations);
            }
            std::fflush(stdout);
        }
        // Member totals stay in sync; member mats hold the concatenated set for inspection.
        shop_count = shop_var_n[0] + shop_var_n[1] + shop_var_n[2];
        apartment_count = apt_var_n[0] + apt_var_n[1] + apt_var_n[2];
        warehouse_count = whs_var_n[0] + whs_var_n[1] + whs_var_n[2];
        {
            u32 off = 0;
            for (u32 v = 0; v < 3u && off < kTreeInstanceCap; ++v) {
                u32 n = shop_var_n[v];
                if (off + n > kTreeInstanceCap) {
                    n = kTreeInstanceCap - off;
                }
                if (n) {
                    std::memcpy(&shop_mats[off * 16], shop_var_mats[v],
                                static_cast<usize>(n) * 16 * sizeof(float));
                    off += n;
                }
            }
            off = 0;
            for (u32 v = 0; v < 3u && off < kTreeInstanceCap; ++v) {
                u32 n = apt_var_n[v];
                if (off + n > kTreeInstanceCap) {
                    n = kTreeInstanceCap - off;
                }
                if (n) {
                    std::memcpy(&apartment_mats[off * 16], apt_var_mats[v],
                                static_cast<usize>(n) * 16 * sizeof(float));
                    off += n;
                }
            }
            off = 0;
            for (u32 v = 0; v < 3u && off < kTreeInstanceCap; ++v) {
                u32 n = whs_var_n[v];
                if (off + n > kTreeInstanceCap) {
                    n = kTreeInstanceCap - off;
                }
                if (n) {
                    std::memcpy(&warehouse_mats[off * 16], whs_var_mats[v],
                                static_cast<usize>(n) * 16 * sizeof(float));
                    off += n;
                }
            }
        }

        std::printf("[buildings] === Random placement from existing buildings ===\n");
        std::printf("[buildings] Candidates: %u procedural lots (skipped %u sky, %u tall)\n",
                    n_cands, skipped_sky, skipped_tall);
        std::printf("[buildings] Lot validation (fixed math): %u on-lot, %u flagged off-lot\n",
                    lot_ok, lot_fail);
        std::printf("[buildings] Shops: attempted %u positions, placed %u (target %u)\n",
                    attempted_shop, placed_shop, kTargetShops);
        std::printf("[buildings] Apartments: attempted %u positions, placed %u (target %u)\n",
                    attempted_apt, placed_apt, kTargetApts);
        std::printf("[buildings] Warehouses: attempted %u positions, placed %u (target %u)\n",
                    attempted_whse, placed_whs, kTargetWhse);
        std::printf("[buildings] Per-variant instances: shops [%u %u %u], apartments [%u %u %u], "
                    "warehouses [%u %u %u]\n",
                    shop_var_n[0], shop_var_n[1], shop_var_n[2], apt_var_n[0], apt_var_n[1],
                    apt_var_n[2], whs_var_n[0], whs_var_n[1], whs_var_n[2]);
        if (skipped_nomesh) {
            std::printf("[buildings] Skipped %u (no mesh loaded for that type)\n", skipped_nomesh);
        }
        if (skipped_cap) {
            std::printf("[buildings] Skipped %u (instance cap %u reached)\n", skipped_cap,
                        kTreeInstanceCap);
        }
        if (skipped_fit) {
            std::printf("[buildings] Skipped %u (lot too small for natural model size)\n",
                        skipped_fit);
        }
        std::printf("[buildings] Total new GLB building instances: %u\n",
                    shop_count + apartment_count + warehouse_count);
        std::printf("[buildings] ==========================\n\n");
        std::fflush(stdout);
    }

    // === STREET PROPS (hydrant / bench / bin GLBs on the sidewalk band) ===
    // Startup only. Sidewalk band = 10.4..12.6 m from the road centre line
    // (road half 10 + 0.4 curb clearance .. +3 walkway - 0.4 lot clearance).
    // Positions are constructed inside the band, then validated; asphalt
    // (<10.4 m of any road line) and lot grass (>12.6 m of every road line)
    // are both rejected.
    {
        std::printf("\n=== [props] PLACEMENT START ===\n");
        static float bin_mats[kTreeInstanceCap * 16];
        static float hyd_mats[kTreeInstanceCap * 16];
        static float ben_mats[kTreeInstanceCap * 16];
        u32 n_bin = 0, n_hyd = 0, n_ben = 0;
        u32 bad_band = 0, too_close = 0;
        // Every placed prop (all types, both placement blocks) lands in the
        // shared taken-list: minimum spacing kills overlaps no matter which
        // loop placed first.
        g_n_prop_taken = 0;

        auto road_dist = [](float v) -> float {
            float f = std::fmod(v, kCityBlockPitch);
            if (f < 0.f) {
                f += kCityBlockPitch;
            }
            return f < kCityBlockPitch - f ? f : kCityBlockPitch - f;
        };
        auto on_walk_band = [&](float x, float z) -> bool {
            const float dx = road_dist(x);
            const float dz = road_dist(z);
            if (dx < 10.4f || dz < 10.4f) {
                return false; // asphalt
            }
            if (dx > 12.6f && dz > 12.6f) {
                return false; // lot grass
            }
            return true;
        };
        // Model front (+Z) toward the nearest road centre line.
        auto prop_yaw = [&](float x, float z) -> float {
            float fx = std::fmod(x, kCityBlockPitch);
            if (fx < 0.f) {
                fx += kCityBlockPitch;
            }
            float fz = std::fmod(z, kCityBlockPitch);
            if (fz < 0.f) {
                fz += kCityBlockPitch;
            }
            const float dx = fx < kCityBlockPitch - fx ? fx : kCityBlockPitch - fx;
            const float dz = fz < kCityBlockPitch - fz ? fz : kCityBlockPitch - fz;
            float dirx = 0.f, dirz = 0.f;
            if (dx <= dz) {
                dirx = (fx < kCityBlockPitch * 0.5f) ? -1.f : 1.f;
            } else {
                dirz = (fz < kCityBlockPitch * 0.5f) ? -1.f : 1.f;
            }
            return std::atan2(dirx, dirz);
        };
        auto hash2 = [](u32 a, u32 b) -> u32 {
            u32 h = (a * 73856093u) ^ (b * 19349663u) ^ 83492791u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return h;
        };
        // Forced real-world prop sizes (target / model bbox), slightly oversized
        // so props read at street distance: bin 1.2 m tall, hydrant 1.0 m tall,
        // bench 2.2 m long. The scale is written into every instance matrix
        // below (never just computed) and asserted per instance.
        // Bench z_up is forced to 0 (flat, as authored): the loader's long-axis
        // heuristic misfires on the 1.8 m bench (z_up=2) and tree_yaw_mat would
        // stand it on its end; no Rx needed, its local up is already +Y.
        const float sc_bin =
            2.4f / max_of(0.001f, bin_glb.ymax - bin_glb.ymin);
        const float sc_hyd =
            2.0f / max_of(0.001f, hydrant_glb.ymax - hydrant_glb.ymin);
        const float bench_len =
            (bench_glb.xmax - bench_glb.xmin) > (bench_glb.zmax - bench_glb.zmin)
                ? (bench_glb.xmax - bench_glb.xmin)
                : (bench_glb.zmax - bench_glb.zmin);
        const float sc_ben = 4.4f / max_of(0.001f, bench_len);
        std::printf("[props] scales: bin=%.3f hydrant=%.3f bench=%.3f (forced target/model bbox)\n",
                    static_cast<double>(sc_bin), static_cast<double>(sc_hyd),
                    static_cast<double>(sc_ben));
        u32 mism_logged = 0, mism_total = 0;
        // Sidewalk surface is the street mesh top (plateau + 0.25); ground props
        // 1 cm above it. The old plateau + 0.05 buried them 20 cm deep.
        auto push_prop = [&](TreeGlb* g, float* mats, u32& n, float x, float z, float sc,
                             int z_up, float target_h, const char* pname,
                             float min_d2) -> bool {
            if (!g || g->nprims == 0 || n >= kTreeInstanceCap) {
                return false;
            }
            if (!on_walk_band(x, z)) {
                ++bad_band;
                return false;
            }
            {
                bool clash = false;
                for (u32 i = 0; i < g_n_prop_taken; ++i) {
                    const float pdx = x - g_prop_taken_xz[i * 2u];
                    const float pdz = z - g_prop_taken_xz[i * 2u + 1u];
                    if (pdx * pdx + pdz * pdz < min_d2) {
                        clash = true;
                        break;
                    }
                }
                if (clash) {
                    ++too_close;
                    return false;
                }
            }
            const float up0 = (z_up == 0) ? g->ymin : tree_up_min(g);
            const float y = kCityPlateauY + 0.26f - up0 * sc;
            tree_yaw_mat(&mats[n * 16], x, y, z, prop_yaw(x, z), sc, z_up);
            // Assert the written matrix really yields the target world height.
            const float up_ext = (z_up == 0) ? (g->ymax - g->ymin)
                                 : (z_up == 1) ? (g->zmax - g->zmin)
                                               : (g->xmax - g->xmin);
            const float got_h = up_ext * sc;
            if (target_h > 0.f && std::fabs(got_h - target_h) > 0.10f * target_h) {
                ++mism_total;
                if (mism_logged < 5u) {
                    std::printf("[props] SCALE MISMATCH %s got=%.2f want=%.2f (x=%.1f z=%.1f)\n",
                                pname, static_cast<double>(got_h), static_cast<double>(target_h),
                                static_cast<double>(x), static_cast<double>(z));
                    ++mism_logged;
                }
            }
            ++n;
            if (g_n_prop_taken < kPropTakenCap) {
                g_prop_taken_xz[g_n_prop_taken * 2u] = x;
                g_prop_taken_xz[g_n_prop_taken * 2u + 1u] = z;
                ++g_n_prop_taken;
            }
            return true;
        };

        // Bins ~200: tucked into intersection corners (band offset 11.9 both axes,
        // clear of the zebra paint which spans +-5 m across each arm).
        u32 bin_tried = 0;
        if (bin_glb.nprims > 0) {
            for (u32 j = 0; j <= kCityBlocks && n_bin < 220u; ++j) {
                for (u32 i = 0; i <= kCityBlocks && n_bin < 220u; ++i) {
                    const u32 h = hash2(i, j * 31u + 7u);
                    if (h % 100u >= 45u) {
                        continue; // ~45% of intersections -> ~198 bins
                    }
                    ++bin_tried;
                    const float sx = (h & 16u) ? 11.9f : -11.9f;
                    const float sz = (h & 32u) ? 11.9f : -11.9f;
                    push_prop(&bin_glb, bin_mats, n_bin,
                              static_cast<float>(i) * kCityBlockPitch + sx,
                              static_cast<float>(j) * kCityBlockPitch + sz, sc_bin,
                              bin_glb.z_up, 2.4f, "bin", 16.f);
                }
            }
            tree_glb_set_instances(&bin_glb, bin_mats, n_bin);
        }
        // Hydrants ~100: EW curbs every ~60 m... every 480 m per road for ~105.
        u32 hyd_tried = 0;
        if (hydrant_glb.nprims > 0) {
            for (u32 j = 0; j <= kCityBlocks && n_hyd < 115u; ++j) {
                for (u32 k = 0; k < 5u && n_hyd < 115u; ++k) {
                    ++hyd_tried;
                    const float x = 60.f + static_cast<float>(k) * 480.f;
                    const float side = ((j + k) & 1u) ? 10.8f : -10.8f;
                    push_prop(&hydrant_glb, hyd_mats, n_hyd, x,
                              static_cast<float>(j) * kCityBlockPitch + side, sc_hyd,
                              hydrant_glb.z_up, 2.0f, "hydrant", 16.f);
                }
            }
            tree_glb_set_instances(&hydrant_glb, hyd_mats, n_hyd);
        }
        // Benches ~80: mid-block sidewalk, deterministic edge per block.
        u32 ben_tried = 0;
        if (bench_glb.nprims > 0) {
            for (u32 bz = 0; bz < kCityBlocks && n_ben < 90u; ++bz) {
                for (u32 bx = 0; bx < kCityBlocks && n_ben < 90u; ++bx) {
                    const u32 h = hash2(bx * 3u + 1u, bz * 7u + 2u);
                    if (h % 100u >= 20u) {
                        continue; // 20% of blocks -> ~80 benches
                    }
                    ++ben_tried;
                    float x = 0.f, z = 0.f;
                    switch (h & 3u) {
                    case 0:
                        x = static_cast<float>(bx) * kCityBlockPitch + 60.f;
                        z = static_cast<float>(bz) * kCityBlockPitch + 10.8f;
                        break;
                    case 1:
                        x = static_cast<float>(bx) * kCityBlockPitch + 60.f;
                        z = static_cast<float>(bz + 1u) * kCityBlockPitch - 10.8f;
                        break;
                    case 2:
                        x = static_cast<float>(bx) * kCityBlockPitch + 10.8f;
                        z = static_cast<float>(bz) * kCityBlockPitch + 60.f;
                        break;
                    default:
                        x = static_cast<float>(bx + 1u) * kCityBlockPitch - 10.8f;
                        z = static_cast<float>(bz) * kCityBlockPitch + 60.f;
                        break;
                    }
                    push_prop(&bench_glb, ben_mats, n_ben, x, z, sc_ben,
                              0, 1.9f,
                              "bench", 16.f); // flat as authored; ignore z_up=2 misdetect
                }
            }
            tree_glb_set_instances(&bench_glb, ben_mats, n_ben);
        }

        // Measure instance 0 of each prop in world space (corners through the
        // written matrix) — the real numbers, not the inputs.
        auto world_size = [](const TreeGlb* g, const float* m, float& w, float& d,
                             float& h) {
            float mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f, mny = 1e9f,
                  mxy = -1e9f;
            for (u32 c = 0; c < 8u; ++c) {
                const float px = (c & 1u) ? g->xmax : g->xmin;
                const float py = (c & 2u) ? g->ymax : g->ymin;
                const float pz = (c & 4u) ? g->zmax : g->zmin;
                const float wx = m[0] * px + m[4] * py + m[8] * pz + m[12];
                const float wy = m[1] * px + m[5] * py + m[9] * pz + m[13];
                const float wz = m[2] * px + m[6] * py + m[10] * pz + m[14];
                if (wx < mnx) {
                    mnx = wx;
                }
                if (wx > mxx) {
                    mxx = wx;
                }
                if (wy < mny) {
                    mny = wy;
                }
                if (wy > mxy) {
                    mxy = wy;
                }
                if (wz < mnz) {
                    mnz = wz;
                }
                if (wz > mxz) {
                    mxz = wz;
                }
            }
            w = mxx - mnx;
            d = mxz - mnz;
            h = mxy - mny;
        };
        {
            float bw = 0.f, bd = 0.f, bh = 0.f, ew = 0.f, ed = 0.f, eh = 0.f, hw = 0.f,
                  hd = 0.f, hh = 0.f;
            if (n_bin > 0) {
                world_size(&bin_glb, bin_mats, bw, bd, bh);
            }
            if (n_ben > 0) {
                world_size(&bench_glb, ben_mats, ew, ed, eh);
            }
            if (n_hyd > 0) {
                world_size(&hydrant_glb, hyd_mats, hw, hd, hh);
            }
            std::printf("[props] world size bin=%.2fx%.2fx%.2f bench=%.2fx%.2fx%.2f "
                        "hydrant=%.2fx%.2fx%.2f\n",
                        static_cast<double>(bw), static_cast<double>(bd), static_cast<double>(bh),
                        static_cast<double>(ew), static_cast<double>(ed), static_cast<double>(eh),
                        static_cast<double>(hw), static_cast<double>(hd), static_cast<double>(hh));
        }
        if (mism_total > mism_logged) {
            std::printf("[props] SCALE MISMATCH total %u instances (+%u above)\n", mism_total,
                        mism_total - mism_logged);
        }

        u32 props_loaded = 0;
        if (hydrant_glb.nprims > 0) {
            ++props_loaded;
        }
        if (bench_glb.nprims > 0) {
            ++props_loaded;
        }
        if (bin_glb.nprims > 0) {
            ++props_loaded;
        }
        std::printf("[props] loaded %d/3, instances bins=%u hydrants=%u benches=%u\n",
                    props_loaded, n_bin, n_hyd, n_ben);
        if (bad_band > 0) {
            std::printf("[props] WARNING: %u placements outside sidewalk band (rejected)\n",
                        bad_band);
        }
        if (too_close > 0) {
            std::printf("[props] spacing: %u candidates rejected within 4 m of another prop\n",
                        too_close);
        }
        std::fflush(stdout);
    }

    // === STREET PROPS 2 (mailbox / kiosk / atm on the sidewalk band) ===
    // Same pipeline as bins/hydrants/benches above: deterministic hash, tree_prog
    // + shadow pass, shared taken-list (1.5 m vs every placed prop, old and new).
    // NOTE: y uses sidewalk-top grounding (+0.26), same as the other props — the
    // street mesh surface is plateau + 0.25 and +0.05 would bury them 20 cm.
    {
        std::printf("\n=== [props] PLACEMENT 2 START ===\n");
        static float mb_mats[kTreeInstanceCap * 16];
        static float ki_mats[kTreeInstanceCap * 16];
        static float atm_mats[kTreeInstanceCap * 16];
        u32 n_mb = 0, n_ki = 0, n_atm = 0;
        // True-scale models (mailbox 0.4x1.2, kiosk 0.5x1.4, atm 0.6x1.3):
        // 2.2x oversize like the old trio so they read at street distance.
        const float sc_mb = 2.6f / max_of(0.001f, mailbox_glb.ymax - mailbox_glb.ymin);
        const float sc_ki = 3.0f / max_of(0.001f, kiosk_glb.ymax - kiosk_glb.ymin);
        const float sc_atm = 2.8f / max_of(0.001f, atm_glb.ymax - atm_glb.ymin);
        std::printf("[props] scales2: mailbox=%.3f kiosk=%.3f atm=%.3f (true-scale models)\n",
                    static_cast<double>(sc_mb), static_cast<double>(sc_ki),
                    static_cast<double>(sc_atm));
        u32 mism2_logged = 0, mism2_total = 0;

        auto hash3 = [](u32 a, u32 b) -> u32 {
            u32 h = (a * 73856093u) ^ (b * 19349663u) ^ 1965533753u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return h;
        };
        auto push_prop2 = [&](TreeGlb* g, float* mats, u32& n, float x, float z, float sc,
                              int z_up, float target_h, const char* pname) -> bool {
            if (!g || g->nprims == 0 || n >= kTreeInstanceCap) {
                return false;
            }
            if (x < 0.f || x > kCityExtentM || z < 0.f || z > kCityExtentM) {
                return false; // outside the city
            }
            if (!prop_on_walk_band(x, z)) {
                return false; // asphalt or lot grass
            }
            for (u32 i = 0; i < g_n_prop_taken; ++i) {
                const float pdx = x - g_prop_taken_xz[i * 2u];
                const float pdz = z - g_prop_taken_xz[i * 2u + 1u];
                if (pdx * pdx + pdz * pdz < 2.25f) {
                    return false; // < 1.5 m from an placed prop (any type)
                }
            }
            const float up0 = (z_up == 0) ? g->ymin : tree_up_min(g);
            const float y = kCityPlateauY + 0.26f - up0 * sc;
            tree_yaw_mat(&mats[n * 16], x, y, z, prop_yaw_to_road(x, z), sc, z_up);
            const float up_ext = (z_up == 0) ? (g->ymax - g->ymin)
                                 : (z_up == 1) ? (g->zmax - g->zmin)
                                               : (g->xmax - g->xmin);
            const float got_h = up_ext * sc;
            if (target_h > 0.f && std::fabs(got_h - target_h) > 0.10f * target_h) {
                ++mism2_total;
                if (mism2_logged < 5u) {
                    std::printf("[props] SCALE MISMATCH %s got=%.2f want=%.2f (x=%.1f z=%.1f)\n",
                                pname, static_cast<double>(got_h), static_cast<double>(target_h),
                                static_cast<double>(x), static_cast<double>(z));
                    ++mism2_logged;
                }
            }
            ++n;
            if (g_n_prop_taken < kPropTakenCap) {
                g_prop_taken_xz[g_n_prop_taken * 2u] = x;
                g_prop_taken_xz[g_n_prop_taken * 2u + 1u] = z;
                ++g_n_prop_taken;
            }
            return true;
        };

        // Mailboxes ~105: neat curb rows on every EW road (phase-shifted 240 m
        // from the hydrant rows so the two never share a slot).
        if (mailbox_glb.nprims > 0) {
            for (u32 j = 0; j <= kCityBlocks && n_mb < 115u; ++j) {
                for (u32 k = 0; k < 5u && n_mb < 115u; ++k) {
                    const float x = 300.f + static_cast<float>(k) * 480.f;
                    const float side = ((j + k) & 1u) ? 10.8f : -10.8f;
                    push_prop2(&mailbox_glb, mb_mats, n_mb, x,
                               static_cast<float>(j) * kCityBlockPitch + side, sc_mb,
                               mailbox_glb.z_up, 2.6f, "mailbox");
                }
            }
            tree_glb_set_instances(&mailbox_glb, mb_mats, n_mb);
        }
        // Kiosks ~60: N/S sidewalk edges at quarter points (benches sit at the
        // centers, hydrants at +60 mod 120 — quarters never coincide with either).
        if (kiosk_glb.nprims > 0) {
            for (u32 bz = 0; bz < kCityBlocks && n_ki < 65u; ++bz) {
                for (u32 bx = 0; bx < kCityBlocks && n_ki < 65u; ++bx) {
                    const u32 h = hash3(bx * 5u + 3u, bz * 11u + 5u);
                    if (h % 100u >= 15u) {
                        continue;
                    }
                    const float along = (h & 4u) ? 30.f : 90.f;
                    float x = static_cast<float>(bx) * kCityBlockPitch + along;
                    float z;
                    if ((bx + bz) & 1u) {
                        z = static_cast<float>(bz + 1u) * kCityBlockPitch - 10.8f;
                    } else {
                        z = static_cast<float>(bz) * kCityBlockPitch + 10.8f;
                    }
                    push_prop2(&kiosk_glb, ki_mats, n_ki, x, z, sc_ki, kiosk_glb.z_up,
                               3.0f, "kiosk");
                }
            }
            tree_glb_set_instances(&kiosk_glb, ki_mats, n_ki);
        }
        // ATMs ~50: E/W lot-edge line (12.4, back toward the building) at quarter
        // points — clear of benches (centers) and kiosks (N/S edges only).
        if (atm_glb.nprims > 0) {
            for (u32 bz = 0; bz < kCityBlocks && n_atm < 55u; ++bz) {
                for (u32 bx = 0; bx < kCityBlocks && n_atm < 55u; ++bx) {
                    const u32 h = hash3(bx * 7u + 9u, bz * 13u + 4u);
                    if (h % 100u >= 15u) {
                        continue;
                    }
                    const float along = (h & 4u) ? 30.f : 90.f;
                    float x, z;
                    if ((bx + bz) & 1u) {
                        x = static_cast<float>(bx + 1u) * kCityBlockPitch - 12.4f;
                    } else {
                        x = static_cast<float>(bx) * kCityBlockPitch + 12.4f;
                    }
                    z = static_cast<float>(bz) * kCityBlockPitch + along;
                    push_prop2(&atm_glb, atm_mats, n_atm, x, z, sc_atm, atm_glb.z_up,
                               2.8f, "atm");
                }
            }
            tree_glb_set_instances(&atm_glb, atm_mats, n_atm);
        }

        // World-size check for the new three (same W x D x H format as trio one).
        {
            float mw = 0.f, md = 0.f, mh = 0.f, kw = 0.f, kd = 0.f, kh = 0.f, aw = 0.f,
                  ad = 0.f, ah = 0.f;
            auto wsize = [](const TreeGlb* g, const float* m, float& w, float& d,
                            float& h) {
                float mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f, mny = 1e9f,
                      mxy = -1e9f;
                for (u32 c = 0; c < 8u; ++c) {
                    const float px = (c & 1u) ? g->xmax : g->xmin;
                    const float py = (c & 2u) ? g->ymax : g->ymin;
                    const float pz = (c & 4u) ? g->zmax : g->zmin;
                    const float wx = m[0] * px + m[4] * py + m[8] * pz + m[12];
                    const float wy = m[1] * px + m[5] * py + m[9] * pz + m[13];
                    const float wz = m[2] * px + m[6] * py + m[10] * pz + m[14];
                    if (wx < mnx) {
                        mnx = wx;
                    }
                    if (wx > mxx) {
                        mxx = wx;
                    }
                    if (wy < mny) {
                        mny = wy;
                    }
                    if (wy > mxy) {
                        mxy = wy;
                    }
                    if (wz < mnz) {
                        mnz = wz;
                    }
                    if (wz > mxz) {
                        mxz = wz;
                    }
                }
                w = mxx - mnx;
                d = mxz - mnz;
                h = mxy - mny;
            };
            if (n_mb > 0) {
                wsize(&mailbox_glb, mb_mats, mw, md, mh);
            }
            if (n_ki > 0) {
                wsize(&kiosk_glb, ki_mats, kw, kd, kh);
            }
            if (n_atm > 0) {
                wsize(&atm_glb, atm_mats, aw, ad, ah);
            }
            std::printf("[props] world size2 mailbox=%.2fx%.2fx%.2f kiosk=%.2fx%.2fx%.2f "
                        "atm=%.2fx%.2fx%.2f\n",
                        static_cast<double>(mw), static_cast<double>(md), static_cast<double>(mh),
                        static_cast<double>(kw), static_cast<double>(kd), static_cast<double>(kh),
                        static_cast<double>(aw), static_cast<double>(ad), static_cast<double>(ah));
        }
        if (mism2_total > mism2_logged) {
            std::printf("[props] SCALE MISMATCH total %u instances (+%u above)\n", mism2_total,
                        mism2_total - mism2_logged);
        }

        u32 props2_loaded = 0;
        if (mailbox_glb.nprims > 0) {
            ++props2_loaded;
        }
        if (kiosk_glb.nprims > 0) {
            ++props2_loaded;
        }
        if (atm_glb.nprims > 0) {
            ++props2_loaded;
        }
        std::printf("[props] loaded %d/3 more, instances mailbox=%u kiosk=%u atm=%u\n",
                    props2_loaded, n_mb, n_ki, n_atm);
        std::fflush(stdout);
    }

    // === CAR TRAFFIC (restored verbatim — untouched behavior) ===
    if (corolla_glb.nprims > 0 || sports_glb.nprims > 0 || suv_glb.nprims > 0) {
        // The models keep their accepted scale/orientation; the traffic plan only hands
        // out a lane position and a heading, which are turned into matrices every frame.
        TreeGlb*    car_glb[kCarMeshCount]  = {&corolla_glb, &sports_glb, &suv_glb};
        const char* car_name[kCarMeshCount] = {"Corolla E80", "sports car", "SUV"};
        u32         car_avail[kCarMeshCount];
        for (u32 m = 0; m < kCarMeshCount; ++m) {
            car_avail[m] = car_glb[m]->nprims > 0 ? 1u : 0u;
            if (car_avail[m] == 0u) {
                continue;
            }
            corolla_basis(car_glb[m], car_basis[m], &car_fwd_axis[m], &car_up_axis[m]);
            car_scale[m] = corolla_fit_scale(car_glb[m]);
            if (m == 1u) {
                car_scale[m] *= 2.f; // sports car reads twice as big
            }
            car_y[m] = kCityPlateauY + 0.30f -
                       corolla_model_ymin(car_glb[m], car_basis[m]) * car_scale[m];
        }

        // Body yaw offset: 0 means the model's nose already points along the length axis
        // that the basis maps onto the ring heading, 180 deg means it points the other way.
        // The Corolla and the sports car are settled on the Mac. The SUV is new, so its
        // nose is read from where its front wheels sit; if its names never said which end
        // is which, it falls back to the Corolla's answer, and G in the sandbox flips it.
        car_yaw_off[0] = kCarPi;       // confirmed on the Mac
        car_yaw_off[1] = 2.f * kCarPi; // confirmed on the Mac
        car_yaw_off[2] = (suv_glb.nose_sign < 0) ? kCarPi : 0.f;
        if (car_avail[2] && suv_glb.nose_sign == 0) {
            car_yaw_off[2] = kCarPi;
        }
        for (u32 m = 0; m < kCarMeshCount; ++m) {
            if (car_avail[m] == 0u) {
                continue;
            }
            const bool  read_it = (m == 2u && car_glb[m]->nose_sign != 0);
            const float deg     = static_cast<double>(car_yaw_off[m] * 180.f / kCarPi);
            std::printf("[cars] %s: nose faces %c%c along the length axis (%s), body yaw offset "
                        "%.0f deg\n",
                        car_name[m], car_glb[m]->nose_sign >= 0 ? '+' : '-', "XYZ"[car_fwd_axis[m]],
                        read_it ? "read from where the front wheels sit" : "confirmed on the Mac",
                        deg);
        }

        // Wheels: the loader found the wheel meshes, so give each the axle it spins about
        // and work out the spin rate in world metres.
        for (u32 m = 0; m < kCarMeshCount; ++m) {
            TreeGlb* g = car_glb[m];
            if (car_avail[m] == 0u || car_scale[m] <= 0.f) {
                continue;
            }
            car_wheel_radius[m] = g->wheel_radius * car_scale[m];
            int   wheel_axis_log = -1;
            float wheel_roll_log = 0.f;
            for (u32 p = 0; p < g->nprims; ++p) {
                TreePrim& pr = g->prims[p];
                if (pr.wheel_count > 0) {
                    pr.wheel_roll = car_wheel_roll_dir(car_fwd_axis[m], car_up_axis[m],
                                                       pr.wheel_axis);
                    if (wheel_axis_log < 0) {
                        wheel_axis_log = pr.wheel_axis; // the tyre primitive, not prim 0
                        wheel_roll_log = pr.wheel_roll;
                    }
                }
            }
            if (g->wheel_prim_count == 0u) {
                std::printf("[cars] %s: no wheel meshes found — the body drives but the wheels stay "
                            "static. See the [glb] primitive lines above; if the wheel materials have "
                            "other names, those names are what j_looks_like_wheel needs.\n",
                            car_name[m]);
            } else {
                std::printf("[cars] %s: %u wheel part(s) in %u primitive(s) spin about %c, tyre "
                            "radius %.3f m world, roll %+.0f\n",
                            car_name[m], g->wheel_count, g->wheel_prim_count, "XYZ"[wheel_axis_log & 3],
                            static_cast<double>(car_wheel_radius[m]),
                            static_cast<double>(wheel_roll_log));
            }
            std::fflush(stdout);
        }
        car_traffic_build(&car_traffic, kCityBlockPitch, car_avail);
        if (car_traffic.agent_count > 0) {
            car_traffic_live = true;
            car_clock        = 0.f;
            update_car_instances(0.f); // the first frame already has traffic on the streets
            float cls_min[kCarMeshCount];
            float cls_max[kCarMeshCount];
            for (u32 m = 0; m < kCarMeshCount; ++m) {
                cls_min[m] = 1.0e9f;
                cls_max[m] = 0.f;
            }
            for (u32 r = 0; r < car_traffic.ring_count; ++r) {
                const CarRing& ring = car_traffic.rings[r];
                const u32      c    = static_cast<u32>(ring.cls) < kCarMeshCount
                                          ? static_cast<u32>(ring.cls)
                                          : 0u;
                cls_min[c] = min_of(cls_min[c], ring.speed);
                cls_max[c] = max_of(cls_max[c], ring.speed);
            }
            std::printf("[cars] Traffic: %u disjoint loops over the 2.4 km grid, %u vehicles driving\n",
                        car_traffic.ring_count, car_traffic.agent_count);
            std::printf("[cars] Traffic: Corolla %.1f-%.1f m/s (%u), sports %.1f-%.1f m/s (%u), "
                        "SUV %.1f-%.1f m/s (%u)\n",
                        static_cast<double>(cls_min[0]), static_cast<double>(cls_max[0]),
                        car_traffic.mesh_count[0], static_cast<double>(cls_min[1]),
                        static_cast<double>(cls_max[1]), car_traffic.mesh_count[1],
                        static_cast<double>(cls_min[2]), static_cast<double>(cls_max[2]),
                        car_traffic.mesh_count[2]);
            float spacing_min = 1.0e9f;
            for (u32 r = 0; r < car_traffic.ring_count; ++r) {
                const CarRing& ring = car_traffic.rings[r];
                if (ring.cars > 0u) {
                    spacing_min = min_of(spacing_min, ring.perimeter / static_cast<float>(ring.cars));
                }
            }
            std::printf("[cars] Traffic: one speed per loop and at least %.0f m spacing, and the loops "
                        "share no asphalt, so no collision is possible without any per-frame physics\n",
                        static_cast<double>(spacing_min));
            std::printf("[cars] Traffic: loop 0 runs beside the sandbox camera x = 1200..1320 m, "
                        "z = 0..240 m; press F to flip the car bodies, G to flip the SUV alone\n");
            std::fflush(stdout);
        }
    }
    std::printf("[city] street mesh: prog=%u verts=%u vao=%u\n", street_prog, street_count,
                street_vao);
    std::printf("[xwalk] geometry: len=%.1f thick=%.1f pitch=%.1f (N/S long-Z, E/W long-X)\n",
                static_cast<double>(kXwalkLen), static_cast<double>(kXwalkThick),
                static_cast<double>(kXwalkPitch));
    std::fflush(stdout);
} // end buildMesh

void BuildingGlPass::update_car_instances(float clock_s) {
    if (!car_traffic_live) {
        return;
    }
    float dt = clock_s - car_clock;
    if (dt < 0.f || dt > 0.25f) {
        dt = 0.f; // first frame, or a long stall: never teleport the fleet
    }
    car_clock = clock_s;
    car_traffic_step(&car_traffic, dt);

    u32 n[kCarMeshCount] = {0u, 0u, 0u};
    for (u32 i = 0; i < car_traffic.agent_count; ++i) {
        const CarAgent& a = car_traffic.agents[i];
        float x = 0.f, z = 0.f, yaw = 0.f;
        car_agent_pose(&car_traffic, i, &x, &z, &yaw);
        const u32 m = a.mesh < kCarMeshCount ? a.mesh : 0u;
        if (car_glb_model(*this, m) == nullptr) {
            continue;
        }
        if (n[m] >= kCarAgentCap) {
            continue;
        }
        // Wheel spin: arc length / tyre radius, wrapped so a long session keeps precision.
        // One angle serves every wheel primitive of the car, because they are one axle.
        const float wrad = car_wheel_radius[m];
        if (wrad > 1.0e-4f) {
            const float ang = car_agent_s(&car_traffic, i) / wrad;
            car_wheel_angles[m][n[m]] = std::fmod(ang, 2.f * kCarPi);
        } else {
            car_wheel_angles[m][n[m]] = 0.f;
        }
        // The AABB basis maps each model's longest axis onto the ring heading, and on a
        // car that axis is its length, so the body needs a yaw offset to face along the
        // direction of travel. Those offsets are set at build time (the Corolla's and the
        // sports car's are confirmed on the Mac, the SUV's is read from its front wheels);
        // car_body_flip and car_suv_flip stay as debug aids, on F and G in the sandbox.
        float mesh_yaw = yaw + car_yaw_off[m] + car_body_flip;
        if (m == 2u) {
            mesh_yaw += car_suv_flip;
        }
        corolla_yaw_mat(&car_mats[m][n[m] * 16u], x, car_y[m], z, mesh_yaw, car_scale[m],
                        car_basis[m]);
        ++n[m];
    }
    for (u32 m = 0; m < kCarMeshCount; ++m) {
        car_traffic.mesh_count[m] = n[m];
        TreeGlb* g = car_glb_model(*this, m);
        if (g != nullptr && g->nprims > 0 && n[m] > 0) {
            tree_glb_set_instances(g, car_mats[m], n[m]);
            tree_glb_set_wheel_angles(g, car_wheel_angles[m], n[m]);
        }
    }
}

void BuildingGlPass::draw(World& world, float3 camera_pos, float3 camera_target, int width, int height,
                          float time_of_day, float3 sun_dir, float clock_s) {
    if (!ok) {
        return;
    }
    update_car_instances(clock_s); // before the shadow pass, so car shadows match the cars
    float view[16], proj[16];
    mat_look(view, camera_pos, camera_target, float3{0.f, 1.f, 0.f});
    mat_persp(proj, 1.22173047f, static_cast<float>(width) / max_of(1, height), 0.15f, 8000.f);
    (void)sun_dir;
    const float3 sun = float3{0.35f, 0.88f, 0.32f};

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    if (cloud_prog) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glBindVertexArray(cube_vao);
        glUseProgram(cloud_prog);
        glUniformMatrix4fv(glGetUniformLocation(cloud_prog, "view"), 1, GL_FALSE, view);
        glUniformMatrix4fv(glGetUniformLocation(cloud_prog, "projection"), 1, GL_FALSE, proj);
        float3 look = float3_normalize_or(float3_sub(camera_target, camera_pos), float3{0.f, 0.f, 1.f});
        float3 right = float3_normalize_or(float3_cross(look, float3{0.f, 1.f, 0.f}), float3{1.f, 0.f, 0.f});
        float3 upv{0.f, 1.f, 0.f};
        glUniform3f(glGetUniformLocation(cloud_prog, "uRight"), right.x, right.y, right.z);
        glUniform3f(glGetUniformLocation(cloud_prog, "uUp"), upv.x, upv.y, upv.z);
        for (u32 i = 0; i < 28; ++i) {
            const float seed = static_cast<float>(i) * 17.13f;
            float x = std::fmod(120.f + seed * 73.f + clock_s * 0.3f, 2800.f);
            float z = 80.f + std::fmod(seed * 91.f, 2300.f);
            float y = 420.f + std::fmod(seed * 37.f, 380.f);
            float sx = 140.f + std::fmod(seed * 11.f, 160.f);
            float sy = 40.f + std::fmod(seed * 7.f, 50.f);
            glUniform3f(glGetUniformLocation(cloud_prog, "uCenter"), x, y, z);
            glUniform2f(glGetUniformLocation(cloud_prog, "uSize"), sx, sy);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    set_building_uniforms(building_prog, view, proj, sun, time_of_day, camera_pos);
    float lview[16], lproj[16], light_vp[16];
    float3 lpos = float3_add(camera_pos, float3_scale(sun, 70.f));
    mat_look(lview, lpos, camera_pos, float3{0.f, 1.f, 0.f});
    mat_ortho(lproj, 95.f, 1.f, 220.f);
    mat_mul16(light_vp, lproj, lview);
    if (shadow_fbo && shadow_prog) {
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
        glViewport(0, 0, 1024, 1024);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.5f, 4.f);
        glUseProgram(shadow_prog);
        glUniformMatrix4fv(glGetUniformLocation(shadow_prog, "uLightVP"), 1, GL_FALSE, light_vp);
        glBindVertexArray(cube_vao);
        float sm[16];
        u32 sc = 0;
        for (Entity e : world.query<BuildingComponent>()) {
            BuildingComponent* b = world.get<BuildingComponent>(e);
            if (!b || !near_xz(b->position, camera_pos, 140.f) || custom_sky_lot(b)) {
                continue;
            }
            if (is_replaced_building(b->building_id)) {
                continue; // GLB replaces this box; no double shadow
            }
            model_trs(sm, b->position, b->width, b->height, b->depth);
            glUniformMatrix4fv(glGetUniformLocation(shadow_prog, "model"), 1, GL_FALSE, sm);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            if (++sc > 220) {
                break;
            }
        }
        if (tree_shadow_prog) {
            glUseProgram(tree_shadow_prog);
            glUniformMatrix4fv(glGetUniformLocation(tree_shadow_prog, "uLightVP"), 1, GL_FALSE, light_vp);
            if (sky_glb.instance_count > 0) {
                draw_instanced_glb(&sky_glb, tree_shadow_prog);
            }
            draw_tree_glbs(tree_glb, tree_shadow_prog);
            for (u32 m = 0; m < kCarMeshCount; ++m) {
                TreeGlb* cg = car_glb_model(*this, m);
                if (cg != nullptr && cg->instance_count > 0) {
                    draw_instanced_glb(cg, tree_shadow_prog);
                }
            }
            if (sports_glb.instance_count > 0) {
                draw_instanced_glb(&sports_glb, tree_shadow_prog);
            }
            // New GLB buildings cast shadows too.
            for (u32 v = 0; v < 3u; ++v) {
                if (shop_glb[v].instance_count > 0) {
                    draw_instanced_glb(&shop_glb[v], tree_shadow_prog);
                }
                if (apartment_glb[v].instance_count > 0) {
                    draw_instanced_glb(&apartment_glb[v], tree_shadow_prog);
                }
                if (warehouse_glb[v].instance_count > 0) {
                    draw_instanced_glb(&warehouse_glb[v], tree_shadow_prog);
                }
            }
            // Street props cast shadows too.
            if (bin_glb.instance_count > 0) {
                draw_instanced_glb(&bin_glb, tree_shadow_prog);
            }
            if (hydrant_glb.instance_count > 0) {
                draw_instanced_glb(&hydrant_glb, tree_shadow_prog);
            }
            if (bench_glb.instance_count > 0) {
                draw_instanced_glb(&bench_glb, tree_shadow_prog);
            }
            if (mailbox_glb.instance_count > 0) {
                draw_instanced_glb(&mailbox_glb, tree_shadow_prog);
            }
            if (kiosk_glb.instance_count > 0) {
                draw_instanced_glb(&kiosk_glb, tree_shadow_prog);
            }
            if (atm_glb.instance_count > 0) {
                draw_instanced_glb(&atm_glb, tree_shadow_prog);
            }
            glUseProgram(shadow_prog);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, max_of(1, width), max_of(1, height));
        glUseProgram(building_prog);
    }
    glUniformMatrix4fv(glGetUniformLocation(building_prog, "uLightVP"), 1, GL_FALSE, light_vp);
    glUniform2f(glGetUniformLocation(building_prog, "uRes"), static_cast<float>(width),
                static_cast<float>(height));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    glUniform1i(glGetUniformLocation(building_prog, "uShadow"), 2);
    glUseProgram(street_prog);
    glUniformMatrix4fv(glGetUniformLocation(street_prog, "view"), 1, GL_FALSE, view);
    glUniformMatrix4fv(glGetUniformLocation(street_prog, "projection"), 1, GL_FALSE, proj);
    glUniformMatrix4fv(glGetUniformLocation(street_prog, "uLightVP"), 1, GL_FALSE, light_vp);
    glUniform3f(glGetUniformLocation(street_prog, "uCamPos"), camera_pos.x, camera_pos.y, camera_pos.z);
    glUniform3f(glGetUniformLocation(street_prog, "uFogColor"), 0.690f, 0.769f, 0.871f);
    glUniform2f(glGetUniformLocation(street_prog, "uRes"), static_cast<float>(width),
                static_cast<float>(height));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_tex);
    glUniform1i(glGetUniformLocation(street_prog, "uShadow"), 2);
    // Street quads are single-sided (down-facing winding); draw them unculled so the
    // ground never depends on cull state leaked from the instanced/shadow passes.
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindVertexArray(street_vao);
    if (street_prog && street_count > 0) {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(street_count));
    }
    // Lot ground: one static draw, grass green, unlit-detail off. Inherits fog and
    // shadow-map reception from the already-set building_prog uniforms, so trees
    // and buildings shadow the grass for free.
    glUseProgram(building_prog);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindVertexArray(lot_vao);
    {
        float ident[16];
        mat_ident(ident);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, ident);
        bind_inv_scale(building_prog, 1.f, 1.f, 1.f);
        glUniform1i(glGetUniformLocation(building_prog, "uAlphaLeaf"), 0);
        glUniform1i(glGetUniformLocation(building_prog, "uFacade"), 0);
        glUniform1f(glGetUniformLocation(building_prog, "roughness"), 0.9f);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 0.f);
        glUniform1i(glGetUniformLocation(building_prog, "district"), 2);
        // windowStyle 2 marks lot grass (dead when uFacade==0): gates the
        // mottle/desat block in the shader. Ring uses 0.
        glUniform1i(glGetUniformLocation(building_prog, "windowStyle"), 2);
        // building.frag scales albedo UV by (1.6, floors*0.28): floors=6 keeps the
        // world/5.0 m tiles ~square, like the ring mesh.
        glUniform1f(glGetUniformLocation(building_prog, "floors"), 6.f);
        if (tex_grass) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex_grass);
            glUniform1i(glGetUniformLocation(building_prog, "uAlbedo"), 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, tex_flat_n);
            glUniform1i(glGetUniformLocation(building_prog, "uNormalTex"), 1);
            glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 1);
            glUniform3f(glGetUniformLocation(building_prog, "albedo"), kGrassTint.x,
                        kGrassTint.y, kGrassTint.z);
        } else {
            glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 0);
            glUniform3f(glGetUniformLocation(building_prog, "albedo"), 0.16f, 0.30f, 0.14f);
        }
        if (lot_count > 0) {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(lot_count), GL_UNSIGNED_INT,
                           nullptr);
        }
    }
    // Sidewalk ring: textured pavers (or flat gray fallback). Same Y as the road
    // with polygon offset winning the overlap; fog/shadow uniforms inherited.
    // building.frag scales albedo UV by (1.6, floors*0.28): floors=6 keeps the
    // world/2 m tiles ~square; uFacade=0 keeps walls/windows off.
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -1.f);
    glBindVertexArray(ring_vao);
    {
        float ident[16];
        mat_ident(ident);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, ident);
        bind_inv_scale(building_prog, 1.f, 1.f, 1.f);
        glUniform1i(glGetUniformLocation(building_prog, "uAlphaLeaf"), 0);
        glUniform1i(glGetUniformLocation(building_prog, "uFacade"), 0);
        glUniform1f(glGetUniformLocation(building_prog, "roughness"), 0.9f);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 0.f);
        glUniform1f(glGetUniformLocation(building_prog, "floors"), 6.f);
        glUniform1i(glGetUniformLocation(building_prog, "district"), 2);
        glUniform1i(glGetUniformLocation(building_prog, "windowStyle"), 0);
        if (tex_sidewalk) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex_sidewalk);
            glUniform1i(glGetUniformLocation(building_prog, "uAlbedo"), 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, tex_flat_n);
            glUniform1i(glGetUniformLocation(building_prog, "uNormalTex"), 1);
            glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 1);
            glUniform3f(glGetUniformLocation(building_prog, "albedo"), kSidewalkTint.x,
                        kSidewalkTint.y, kSidewalkTint.z);
        } else {
            glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 0);
            glUniform3f(glGetUniformLocation(building_prog, "albedo"), 0.55f, 0.55f, 0.55f);
        }
        if (ring_count > 0) {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ring_count), GL_UNSIGNED_INT,
                           nullptr);
        }
    }
    // Curb-face walls: flat concrete gray, untextured, double-sided via cull-off.
    glBindVertexArray(skirt_vao);
    {
        float ident[16];
        mat_ident(ident);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, ident);
        bind_inv_scale(building_prog, 1.f, 1.f, 1.f);
        glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 0);
        glUniform1i(glGetUniformLocation(building_prog, "uAlphaLeaf"), 0);
        glUniform1i(glGetUniformLocation(building_prog, "uFacade"), 0);
        glUniform3f(glGetUniformLocation(building_prog, "albedo"), 0.5f, 0.5f, 0.5f);
        glUniform1f(glGetUniformLocation(building_prog, "roughness"), 0.9f);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 0.f);
        glUniform1f(glGetUniformLocation(building_prog, "floors"), 1.f);
        glUniform1i(glGetUniformLocation(building_prog, "district"), 2);
        glUniform1i(glGetUniformLocation(building_prog, "windowStyle"), 0);
        if (skirt_count > 0) {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(skirt_count), GL_UNSIGNED_INT,
                           nullptr);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE); // restore culling for the procedural-box passes
    glUseProgram(building_prog);
    glBindVertexArray(cube_vao);
    float model[16];
    u32 drawn = 0;
    for (Entity e : world.query<BuildingComponent>()) {
        BuildingComponent* b = world.get<BuildingComponent>(e);
        if (!b || custom_sky_lot(b)) {
            continue;
        }
        if (is_replaced_building(b->building_id)) {
            continue; // replaced by a GLB instance
        }
        const float dx = b->position.x - camera_pos.x;
        const float dz = b->position.z - camera_pos.z;
        if (dx * dx + dz * dz > 1400.f * 1400.f) {
            continue;
        }
        model_trs(model, b->position, b->width, b->height, b->depth);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
        glUniform3f(glGetUniformLocation(building_prog, "albedo"), b->albedo_color.x, b->albedo_color.y,
                    b->albedo_color.z);
        glUniform1f(glGetUniformLocation(building_prog, "roughness"), b->roughness);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 0.f);
        glUniform1f(glGetUniformLocation(building_prog, "floors"), static_cast<float>(b->num_floors));
        glUniform1i(glGetUniformLocation(building_prog, "district"), static_cast<int>(b->district));
        glUniform1i(glGetUniformLocation(building_prog, "windowStyle"), static_cast<int>(b->window_style));
        unsigned alb = tex_brick;
        unsigned nrm = tex_brick_n;
        if (b->district == 0) {
            alb = tex_conc;
            nrm = tex_conc_n;
        } else if (b->district == 1 || b->district == 4) {
            alb = tex_conc;
            nrm = tex_conc_n;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, alb);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, nrm);
        glUniform1i(glGetUniformLocation(building_prog, "uFacade"), 1);
        glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 1);
        glUniform1i(glGetUniformLocation(building_prog, "uAlbedo"), 0);
        glUniform1i(glGetUniformLocation(building_prog, "uNormalTex"), 1);
        bind_inv_scale(building_prog, 1.f, 1.f, 1.f);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 0);
        if (b->roof_style == 1) {
            draw_box(building_prog, float3{b->position.x, b->position.y + b->height, b->position.z},
                     b->width * 1.02f, 1.6f, b->depth * 0.55f, float3{0.45f, 0.22f, 0.16f}, 0.f);
            draw_box(building_prog,
                     float3{b->position.x, b->position.y + b->height + 1.5f, b->position.z}, b->width * 0.7f,
                     1.4f, b->depth * 0.28f, float3{0.42f, 0.20f, 0.14f}, 0.f);
            draw_box(building_prog,
                     float3{b->position.x, b->position.y + b->height + 2.6f, b->position.z}, b->width * 0.35f,
                     1.1f, b->depth * 0.12f, float3{0.40f, 0.18f, 0.12f}, 0.f);
        } else if (b->roof_style == 2) {
            float3 step{b->position.x, b->position.y + b->height, b->position.z};
            model_trs(model, step, b->width * 0.62f, 10.f, b->depth * 0.62f);
            glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
        if (b->district == 2) {
            for (u32 k = 0; k < 4; ++k) {
                const float fy = b->position.y + 4.5f + static_cast<float>(k) * 3.6f;
                if (fy > b->position.y + b->height - 3.f) {
                    break;
                }
                draw_box(building_prog,
                         float3{b->position.x, fy, b->position.z - b->depth * 0.5f - 0.7f}, b->width * 0.18f,
                         0.8f, 1.2f, float3{0.55f, 0.55f, 0.52f}, 0.f);
            }
        }
        if (b->district == 5) {
            draw_box(building_prog,
                     float3{b->position.x, b->position.y + 3.1f, b->position.z - b->depth * 0.5f - 0.9f},
                     b->width * 0.9f, 0.18f, 1.6f, float3{0.72f, 0.18f, 0.14f}, 0.f);
        }
        if (b->district == 4) {
            draw_box(building_prog,
                     float3{b->position.x + b->width * 0.5f + 1.2f, b->position.y, b->position.z}, 2.4f, 1.4f,
                     6.f, float3{0.28f, 0.28f, 0.26f}, 0.f);
        }
        if (b->district == 0) {
            draw_box(building_prog,
                     float3{b->position.x, b->position.y + b->height + 6.f, b->position.z}, 0.18f, 8.f, 0.18f,
                     float3{0.3f, 0.3f, 0.32f}, 0.f);
        }
        if (b->height > 14.f) {
            float3 hvac{b->position.x + b->width * 0.18f, b->position.y + b->height,
                        b->position.z - b->depth * 0.16f};
            model_trs(model, hvac, 3.4f, 2.2f, 2.6f);
            glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
            glUniform3f(glGetUniformLocation(building_prog, "albedo"), 0.34f, 0.34f, 0.35f);
            glUniform1f(glGetUniformLocation(building_prog, "floors"), 1.f);
            glUniform1i(glGetUniformLocation(building_prog, "district"), 2);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
        if (b->district == 0 && (b->building_id % 6u) == 0u) {
            float3 tower{b->position.x - b->width * 0.2f, b->position.y + b->height,
                         b->position.z + b->depth * 0.12f};
            model_trs(model, tower, 2.2f, 4.5f, 2.2f);
            glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
            glUniform3f(glGetUniformLocation(building_prog, "albedo"), 0.40f, 0.36f, 0.32f);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        }
        ++drawn;
    }
    const float night = night_glow_amt(time_of_day);
    if (tree_prog) {
        glUseProgram(tree_prog);
        glUniformMatrix4fv(glGetUniformLocation(tree_prog, "view"), 1, GL_FALSE, view);
        glUniformMatrix4fv(glGetUniformLocation(tree_prog, "projection"), 1, GL_FALSE, proj);
        glUniformMatrix4fv(glGetUniformLocation(tree_prog, "uLightVP"), 1, GL_FALSE, light_vp);
        glUniform3f(glGetUniformLocation(tree_prog, "lightDir"), sun.x, sun.y, sun.z);
        glUniform3f(glGetUniformLocation(tree_prog, "uCamPos"), camera_pos.x, camera_pos.y, camera_pos.z);
        glUniform3f(glGetUniformLocation(tree_prog, "uFogColor"), 0.690f, 0.769f, 0.871f);
        glUniform2f(glGetUniformLocation(tree_prog, "uRes"), static_cast<float>(width),
                    static_cast<float>(height));
        glUniform1f(glGetUniformLocation(tree_prog, "uNightGlow"), 0.f);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, shadow_tex);
        glUniform1f(glGetUniformLocation(tree_prog, "uNightGlow"), night);
        draw_instanced_glb(&lamp_glb[0], tree_prog);
        draw_instanced_glb(&lamp_glb[1], tree_prog);
        glUniform1f(glGetUniformLocation(tree_prog, "uNightGlow"), 0.f);
        draw_tree_glbs(tree_glb, tree_prog);
        draw_instanced_glb(&sky_glb, tree_prog);
        // New GLB buildings: same instanced pipeline as trees/lamps/cars.
        for (u32 v = 0; v < 3u; ++v) {
            if (shop_glb[v].instance_count > 0) {
                draw_instanced_glb(&shop_glb[v], tree_prog);
            }
            if (apartment_glb[v].instance_count > 0) {
                draw_instanced_glb(&apartment_glb[v], tree_prog);
            }
            if (warehouse_glb[v].instance_count > 0) {
                draw_instanced_glb(&warehouse_glb[v], tree_prog);
            }
        }
        // Street props ride the same opaque instanced pass (uNightGlow off).
        if (bin_glb.instance_count > 0) {
            draw_instanced_glb(&bin_glb, tree_prog);
        }
        if (hydrant_glb.instance_count > 0) {
            draw_instanced_glb(&hydrant_glb, tree_prog);
        }
        if (bench_glb.instance_count > 0) {
            draw_instanced_glb(&bench_glb, tree_prog);
        }
        if (mailbox_glb.instance_count > 0) {
            draw_instanced_glb(&mailbox_glb, tree_prog);
        }
        if (kiosk_glb.instance_count > 0) {
            draw_instanced_glb(&kiosk_glb, tree_prog);
        }
        if (atm_glb.instance_count > 0) {
            draw_instanced_glb(&atm_glb, tree_prog);
        }
        glUniform1f(glGetUniformLocation(tree_prog, "uNightGlow"), 0.f);
        for (u32 m = 0; m < kCarMeshCount; ++m) {
            TreeGlb* cg = car_glb_model(*this, m);
            if (cg != nullptr && cg->nprims > 0) {
                draw_instanced_glb(cg, tree_prog);
            }
        }
        glUseProgram(building_prog);
    }
    if (glow_prog && glow_count > 0 && night > 0.01f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
        glUseProgram(glow_prog);
        glUniformMatrix4fv(glGetUniformLocation(glow_prog, "view"), 1, GL_FALSE, view);
        glUniformMatrix4fv(glGetUniformLocation(glow_prog, "projection"), 1, GL_FALSE, proj);
        glUniform1f(glGetUniformLocation(glow_prog, "uGlow"), night);
        glBindVertexArray(glow_vao);
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(glow_nidx), GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(glow_count));
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glUseProgram(building_prog);
    }
    glBindVertexArray(cube_vao);
    glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 0);
    glUniform1i(glGetUniformLocation(building_prog, "uAlphaLeaf"), 0);
    // Zebra bars sit near-flush (centre +0.25, 0.02 thick: 1 cm embedded, 1 cm
    // proud) so grazing views show no parallax overshoot past the curb.
    // Polygon offset around this loop only wins any residual depth ties.
    const float stripe_y = kRoadY + 0.02f;
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -1.f);
    for (u32 j = 0; j <= kCityBlocks; ++j) {
        for (u32 i = 0; i <= kCityBlocks; ++i) {
            const float cx = static_cast<float>(i) * kCityBlockPitch;
            const float cz = static_cast<float>(j) * kCityBlockPitch;
            if (!near_xz(float3{cx, 0.f, cz}, camera_pos, 180.f)) {
                continue;
            }
            const float start = kXwalkStart; // 11: just outside the junction box
            const float span = (static_cast<float>(kXwalkCount) - 1.f) * kXwalkPitch;
            const float3 xw{0.94f, 0.94f, 0.94f};
            for (u32 s = 0; s < kXwalkCount; ++s) {
                const float o = -span * 0.5f + static_cast<float>(s) * kXwalkPitch;
                // N/S approaches: bars long in Z (with traffic), spaced in X.
                draw_box(building_prog, float3{cx + o, stripe_y, cz + start + kXwalkLen * 0.5f}, kXwalkThick, 0.02f, kXwalkLen, xw, 0.f);
                draw_box(building_prog, float3{cx + o, stripe_y, cz - start - kXwalkLen * 0.5f}, kXwalkThick, 0.02f, kXwalkLen, xw, 0.f);
                // E/W approaches: bars long in X, spaced in Z.
                draw_box(building_prog, float3{cx + start + kXwalkLen * 0.5f, stripe_y, cz + o}, kXwalkLen, 0.02f, kXwalkThick, xw, 0.f);
                draw_box(building_prog, float3{cx - start - kXwalkLen * 0.5f, stripe_y, cz + o}, kXwalkLen, 0.02f, kXwalkThick, xw, 0.f);
            }
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    // NOTE: procedural street furniture (hydrant cylinders, mailbox, bench boxes,
    // shrubs, sign poles) was deleted here — replaced by instanced GLB props.
    static bool logged_detail = false;
    if (!logged_detail) {
        std::printf("[city] street kit lamps_instanced=%u+%u trees_instanced=%u+%u+%u "
                    "shops=%u+%u+%u apts=%u+%u+%u whs=%u+%u+%u\n",
                    lamp_glb[0].instance_count, lamp_glb[1].instance_count, tree_glb[0].instance_count,
                    tree_glb[1].instance_count, tree_glb[2].instance_count,
                    shop_glb[0].instance_count, shop_glb[1].instance_count, shop_glb[2].instance_count,
                    apartment_glb[0].instance_count, apartment_glb[1].instance_count,
                    apartment_glb[2].instance_count, warehouse_glb[0].instance_count,
                    warehouse_glb[1].instance_count, warehouse_glb[2].instance_count);
        std::fflush(stdout);
        logged_detail = true;
    }
    glBindVertexArray(0);
    static bool logged = false;
    if (!logged) {
        std::printf("[gl] drawing %u buildings (GLSL bound) + %u GLB buildings\n", drawn,
                    shop_count + apartment_count + warehouse_count);
        std::fflush(stdout);
        logged = true;
    }
}

void BuildingGlPass::shutdown() {
    if (cube_vao) {
        glDeleteVertexArrays(1, &cube_vao);
    }
    if (cube_vbo) {
        glDeleteBuffers(1, &cube_vbo);
    }
    if (cube_ibo) {
        glDeleteBuffers(1, &cube_ibo);
    }
    if (street_vao) {
        glDeleteVertexArrays(1, &street_vao);
    }
    if (street_vbo) {
        glDeleteBuffers(1, &street_vbo);
    }
    if (lot_vao) {
        glDeleteVertexArrays(1, &lot_vao);
        lot_vao = 0;
    }
    if (lot_ibo) {
        glDeleteBuffers(1, &lot_ibo);
        lot_ibo = 0;
    }
    lot_count = 0;
    if (ring_vao) {
        glDeleteVertexArrays(1, &ring_vao);
        ring_vao = 0;
    }
    if (ring_ibo) {
        glDeleteBuffers(1, &ring_ibo);
        ring_ibo = 0;
    }
    ring_count = 0;
    if (skirt_vao) {
        glDeleteVertexArrays(1, &skirt_vao);
        skirt_vao = 0;
    }
    if (skirt_ibo) {
        glDeleteBuffers(1, &skirt_ibo);
        skirt_ibo = 0;
    }
    skirt_count = 0;
    if (tex_sidewalk) {
        glDeleteTextures(1, &tex_sidewalk);
        tex_sidewalk = 0;
    }
    if (tex_grass) {
        glDeleteTextures(1, &tex_grass);
        tex_grass = 0;
    }
    if (tex_flat_n) {
        glDeleteTextures(1, &tex_flat_n);
        tex_flat_n = 0;
    }
    if (building_prog) {
        glDeleteProgram(building_prog);
    }
    if (cloud_prog) {
        glDeleteProgram(cloud_prog);
        cloud_prog = 0;
    }
    if (shadow_prog) {
        glDeleteProgram(shadow_prog);
        shadow_prog = 0;
    }
    if (street_prog) {
        glDeleteProgram(street_prog);
    }
    if (tree_prog) {
        glDeleteProgram(tree_prog);
        tree_prog = 0;
    }
    if (tree_shadow_prog) {
        glDeleteProgram(tree_shadow_prog);
        tree_shadow_prog = 0;
    }
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        tree_glb_shutdown(&tree_glb[k]);
    }
    tree_glb_shutdown(&sky_glb);
    for (u32 k = 0; k < kLampKindCount; ++k) {
        tree_glb_shutdown(&lamp_glb[k]);
    }
    tree_glb_shutdown(&corolla_glb);
    tree_glb_shutdown(&sports_glb);
    tree_glb_shutdown(&suv_glb);
    for (u32 k = 0; k < 3u; ++k) {
        tree_glb_shutdown(&shop_glb[k]);
        tree_glb_shutdown(&apartment_glb[k]);
        tree_glb_shutdown(&warehouse_glb[k]);
    }
    tree_glb_shutdown(&hydrant_glb);
    tree_glb_shutdown(&bench_glb);
    tree_glb_shutdown(&bin_glb);
    tree_glb_shutdown(&mailbox_glb);
    tree_glb_shutdown(&kiosk_glb);
    tree_glb_shutdown(&atm_glb);
    if (g_fallback_white_tex) {
        glDeleteTextures(1, &g_fallback_white_tex);
        g_fallback_white_tex = 0;
    }
    if (glow_prog) {
        glDeleteProgram(glow_prog);
        glow_prog = 0;
    }
    if (glow_vao) {
        glDeleteVertexArrays(1, &glow_vao);
        glow_vao = 0;
    }
    if (glow_vbo) {
        glDeleteBuffers(1, &glow_vbo);
        glow_vbo = 0;
    }
    if (glow_ibo) {
        glDeleteBuffers(1, &glow_ibo);
        glow_ibo = 0;
    }
    if (glow_ivbo) {
        glDeleteBuffers(1, &glow_ivbo);
        glow_ivbo = 0;
    }
    building_prog = street_prog = 0;
}

} // namespace engine
