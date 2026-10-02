#include "render/BuildingGlPass.h"

#include "ecs/World.h"
#include "objects/StreetLight.h"
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
    "uniform mat4 view, projection; out vec2 UV;\n"
    "void main(){ UV=aUV; gl_Position=projection*view*vec4(aPos,1.0); }\n";

constexpr const char* kStFs =
    "#version 330 core\n"
    "in vec2 UV; out vec4 FragColor;\n"
    "void main(){ vec3 asphalt=vec3(0.12,0.12,0.13); vec3 sidewalk=vec3(0.42,0.41,0.38); vec3 paint=vec3(0.92,0.86,0.35);\n"
    "  float edge=step(0.88,abs(UV.x*2.0-1.0));\n"
    "  float dash=step(0.45,fract(UV.y*8.0))*(1.0-step(0.04,abs(UV.x-0.5)));\n"
    "  vec3 c=mix(asphalt,sidewalk,edge); c=mix(c,paint,dash); FragColor=vec4(c,1.0); }\n";

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
    float s = (4.2f * 2.2f) / length;
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
void corolla_basis(const TreeGlb* t, float R[9]) {
    const float e[3] = {t->xmax - t->xmin, t->ymax - t->ymin, t->zmax - t->zmin};
    int fwd = 0;
    if (e[1] > e[fwd]) {
        fwd = 1;
    }
    if (e[2] > e[fwd]) {
        fwd = 2;
    }
    int up = (fwd == 0) ? 1 : 0;
    for (int i = 0; i < 3; ++i) {
        if (i != fwd && e[i] < e[up]) {
            up = i;
        }
    }
    float fu[3] = {0.f, 0.f, 0.f};
    float uu[3] = {0.f, 0.f, 0.f};
    fu[fwd] = 1.f;
    uu[up]  = 1.f;
    const float ru[3] = {fu[1] * uu[2] - fu[2] * uu[1], fu[2] * uu[0] - fu[0] * uu[2],
                         fu[0] * uu[1] - fu[1] * uu[0]};
    static const char* kAxis = "XYZ";
    std::printf("[cars] basis length=%c height=%c (stand on wheels)\n", kAxis[fwd], kAxis[up]);
    std::fflush(stdout);
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

void draw_instanced_glb(TreeGlb* g, unsigned prog) {
    if (!prog || !g || g->instance_count == 0) {
        return;
    }
    glUseProgram(prog);
    glDisable(GL_CULL_FACE);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(prog, "uEmissive"), 1);
    glUniform1i(glGetUniformLocation(prog, "uShadow"), 2);
    for (u32 p = 0; p < g->nprims; ++p) {
        TreePrim& pr = g->prims[p];
        glBindVertexArray(pr.vao);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pr.tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, pr.tex_emit ? pr.tex_emit : pr.tex);
        glUniform1i(glGetUniformLocation(prog, "uAlphaMask"), pr.alpha_mask);
        glUniform1f(glGetUniformLocation(prog, "uAlphaCut"), pr.cutoff);
        glUniform1i(glGetUniformLocation(prog, "uUseTexture"), 1);
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

} // namespace

bool BuildingGlPass::init() {
    ok = false;
    building_prog = street_prog = cloud_prog = 0;
    tree_prog = tree_shadow_prog = 0;
    std::memset(tree_glb, 0, sizeof(tree_glb));
    std::memset(&sky_glb, 0, sizeof(sky_glb));
    std::memset(lamp_glb, 0, sizeof(lamp_glb));
    std::memset(&corolla_glb, 0, sizeof(corolla_glb));
    glow_prog = glow_vao = glow_vbo = glow_ibo = glow_ivbo = glow_nidx = 0;
    glow_count = 0;
    cube_vao = cube_vbo = cube_ibo = 0;
    street_vao = street_vbo = 0;
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
        "layout(location=7) in vec4 aColor;\n"
        "layout(location=3) in vec4 iM0; layout(location=4) in vec4 iM1;\n"
        "layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;\n"
        "uniform mat4 view,projection,uLightVP;\n"
        "out vec3 FragPos; out vec3 Normal; out vec2 UV; out vec4 LightPos; out vec4 VertColor;\n"
        "void main(){ mat4 model=mat4(iM0,iM1,iM2,iM3); vec4 wp=model*vec4(aPos,1.0);\n"
        " FragPos=wp.xyz; Normal=mat3(model)*aNormal; UV=aUV; VertColor=vec4(1.0);\n"
        " LightPos=uLightVP*wp; gl_Position=projection*view*wp; }\n";
    constexpr const char* kTreeFbFs =
        "#version 330 core\n"
        "in vec3 FragPos; in vec3 Normal; in vec2 UV; in vec4 LightPos; in vec4 VertColor;\n"
        "uniform sampler2D uAlbedo; out vec4 FragColor;\n"
        "void main(){ vec4 albedo=texture(uAlbedo, UV); if(albedo.a<0.5) discard; FragColor=vec4(albedo.rgb,1.0); }\n";
    tree_prog = make_program("shaders/tree.vert", "shaders/tree.frag", kTreeFbVs, kTreeFbFs, "tree");
    constexpr const char* kTshFbVs =
        "#version 330 core\nlayout(location=0) in vec3 aPos; layout(location=3) in vec4 iM0;\n"
        "layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;\n"
        "uniform mat4 uLightVP; void main(){ mat4 model=mat4(iM0,iM1,iM2,iM3); gl_Position=uLightVP*model*vec4(aPos,1.0); }\n";
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
            corolla_basis(&corolla_glb, Rtmp);
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
        std::fflush(stdout);
    } else {
        std::printf("[cars] low-poly_toyota_corolla_e80_sedan.glb not found — no parked cars\n");
        std::fflush(stdout);
    }
    std::printf("[glb] Loaded street lamp models: klassisk (%u verts), moderne (%u verts)\n",
                lamp_glb[0].nverts, lamp_glb[1].nverts);
    log_glb_textures("klassisk", &lamp_glb[0]);
    log_glb_textures("moderne", &lamp_glb[1]);
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
    const float y      = kCityPlateauY + 0.25f;
    const float half_w = kCityStreetWidth * 0.5f + 3.f; // 10 m asphalt + 3 m sidewalk
    const float pitch  = kCityBlockPitch;
    const u32   nline  = kCityBlocks + 1;

    for (u32 j = 0; j < nline; ++j) {
        for (u32 i = 0; i < nline; ++i) {
            const float cx = static_cast<float>(i) * pitch;
            const float cz = static_cast<float>(j) * pitch;
            emit_aabb_quad(verts, &n, cx - half_w, cz - half_w, cx + half_w, cz + half_w, y, 0.22f, 0.f,
                           0.22f, 0.f, 0.22f, 0.f, 0.22f, 0.f);
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
    if (corolla_glb.nprims > 0) {
        static float corolla_mats[kCorollaSpawnCap * 16];
        static float corolla_xz[kCorollaSpawnCap * 2];
        u32 sn       = 0;
        u32 n_corolla_xz = 0;
        float R[9];
        corolla_basis(&corolla_glb, R);
        const float sc   = corolla_fit_scale(&corolla_glb);
        const float y0   = corolla_model_ymin(&corolla_glb, R);
        const float y    = kCityPlateauY + 0.30f - y0 * sc; // road top + 0.05 m
        const float park = 7.2f; // curb lane on 20 m asphalt, not the 11.5 m sidewalk
        auto push_suv = [&](float x, float z, float along) {
            if (sn >= kCorollaSpawnCap) {
                return;
            }
            const float nx = std::round(x / kCityBlockPitch) * kCityBlockPitch;
            const float nz = std::round(z / kCityBlockPitch) * kCityBlockPitch;
            const float dx = x - nx;
            const float dz = z - nz;
            if (dx * dx + dz * dz < 15.f * 15.f) {
                return;
            }
            if (xz_too_close(x, z, corolla_xz, n_corolla_xz, 8.0f)) {
                return;
            }
            const u32 flip = static_cast<u32>(x * 17.f + z * 31.f) & 1u;
            // AABB longest axis is world +Z; roads need that axis along the street → +90° yaw.
            const float yaw = along + 1.5707963f + (flip ? 3.14159265f : 0.f);
            corolla_yaw_mat(&corolla_mats[sn * 16], x, y, z, yaw, sc, R);
            corolla_xz[n_corolla_xz * 2u]     = x;
            corolla_xz[n_corolla_xz * 2u + 1] = z;
            ++n_corolla_xz;
            ++sn;
        };
        for (u32 j = 0; j <= kCityBlocks && sn < kCorollaSpawnCap; ++j) {
            const float z = static_cast<float>(j) * kCityBlockPitch;
            for (float x = 80.f; x < kCityExtentM - 80.f && sn < kCorollaSpawnCap; x += 48.f) {
                const float side = (static_cast<u32>(x) % 96u < 48u) ? park : -park;
                push_suv(x, z + side, 0.f);
            }
        }
        for (u32 i = 0; i <= kCityBlocks && sn < kCorollaSpawnCap; ++i) {
            const float x = static_cast<float>(i) * kCityBlockPitch;
            for (float z = 80.f; z < kCityExtentM - 80.f && sn < kCorollaSpawnCap; z += 48.f) {
                const float side = (static_cast<u32>(z) % 96u < 48u) ? park : -park;
                push_suv(x + side, z, 1.5707963f);
            }
        }
        tree_glb_set_instances(&corolla_glb, corolla_mats, sn);
        std::printf("[cars] Yaw +90 deg so length follows the road\n");
        std::printf("[cars] Parked %u Corolla E80 along roads\n", sn);
        std::fflush(stdout);
    }
}

void BuildingGlPass::draw(World& world, float3 camera_pos, float3 camera_target, int width, int height,
                          float time_of_day, float3 sun_dir, float clock_s) {
    if (!ok) {
        return;
    }
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
            if (corolla_glb.instance_count > 0) {
                draw_instanced_glb(&corolla_glb, tree_shadow_prog);
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
    glBindVertexArray(street_vao);
    if (street_count > 0) {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(street_count));
    }
    glUseProgram(building_prog);
    glBindVertexArray(cube_vao);
    float model[16];
    u32 drawn = 0;
    for (Entity e : world.query<BuildingComponent>()) {
        BuildingComponent* b = world.get<BuildingComponent>(e);
        if (!b || custom_sky_lot(b)) {
            continue;
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
    const float3 metal{0.18f, 0.18f, 0.20f};
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
        glUniform1f(glGetUniformLocation(tree_prog, "uNightGlow"), 0.f);
        draw_instanced_glb(&corolla_glb, tree_prog);
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
    const float sw = kCityStreetWidth * 0.5f + 1.6f;
    const float gy = kCityPlateauY;
    glBindVertexArray(cube_vao);
    glUniform1i(glGetUniformLocation(building_prog, "uUseTex"), 0);
    glUniform1i(glGetUniformLocation(building_prog, "uAlphaLeaf"), 0);
    const float stripe_y = kCityPlateauY + 0.27f;
    for (u32 j = 0; j <= kCityBlocks; ++j) {
        for (u32 i = 0; i <= kCityBlocks; ++i) {
            const float cx = static_cast<float>(i) * kCityBlockPitch;
            const float cz = static_cast<float>(j) * kCityBlockPitch;
            if (!near_xz(float3{cx, 0.f, cz}, camera_pos, 180.f)) {
                continue;
            }
            const float start = 10.35f;
            const float3 xw{0.94f, 0.94f, 0.94f};
            for (u32 s = 0; s < 6; ++s) {
                const float o = static_cast<float>(s) * 0.72f;
                draw_box(building_prog, float3{cx, stripe_y, cz + start + o}, 10.f, 0.03f, 0.50f, xw, 0.f);
                draw_box(building_prog, float3{cx, stripe_y, cz - start - o}, 10.f, 0.03f, 0.50f, xw, 0.f);
                draw_box(building_prog, float3{cx + start + o, stripe_y, cz}, 0.50f, 0.03f, 10.f, xw, 0.f);
                draw_box(building_prog, float3{cx - start - o, stripe_y, cz}, 0.50f, 0.03f, 10.f, xw, 0.f);
            }
        }
    }
    for (u32 bz = 0; bz < kCityBlocks; ++bz) {
        for (u32 bx = 0; bx < kCityBlocks; ++bx) {
            const float lx = (static_cast<float>(bx) + 0.5f) * kCityBlockPitch;
            const float lz = (static_cast<float>(bz) + 0.5f) * kCityBlockPitch;
            if (!near_xz(float3{lx, 0.f, lz}, camera_pos, 160.f)) {
                continue;
            }
            const float sx = static_cast<float>(bx) * kCityBlockPitch + sw;
            const float sz = static_cast<float>(bz) * kCityBlockPitch + sw;
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 4.f, gy, sz + 6.f}, 0.40f, 0.90f,
                           0.40f, float3{0.18f, 0.22f, 0.16f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 9.f, gy, sz + 6.f}, 0.40f, 0.90f,
                           0.40f, float3{0.20f, 0.20f, 0.20f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 14.f, gy, sz + 6.f}, 0.38f, 0.85f,
                           0.38f, float3{0.16f, 0.20f, 0.15f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 3.f, gy, sz + 2.f}, 0.16f, 0.70f, 0.16f,
                           float3{0.85f, 0.10f, 0.08f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 3.f, gy + 0.62f, sz + 2.f}, 0.20f,
                           0.12f, 0.20f, float3{0.75f, 0.08f, 0.06f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 2.f, gy, sz + 18.f}, 0.16f, 0.70f,
                           0.16f, float3{0.85f, 0.10f, 0.08f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 20.f, gy, sz + 2.f}, 0.16f, 0.70f,
                           0.16f, float3{0.85f, 0.10f, 0.08f}, 0.f, 0, 0);
            draw_axis_mesh(building_prog, cyl_vao, cyl_count, float3{sx + 1.5f, gy, sz + 1.5f}, 0.06f, 2.5f,
                           0.06f, metal, 0.f, 0, 0);
            glBindVertexArray(cube_vao);
            draw_box(building_prog, float3{sx + 1.5f, gy + 2.45f, sz + 1.5f}, 0.62f, 0.42f, 0.08f,
                     float3{0.12f, 0.45f, 0.28f}, 0.f);
            draw_box(building_prog, float3{sx + 22.f, gy, sz + 8.f}, 0.50f, 1.15f, 0.38f, float3{0.12f, 0.28f, 0.62f},
                     0.f);
            if (bx + bz > 14) {
                draw_box(building_prog, float3{sx + 10.f, gy, sz + 14.f}, 1.55f, 0.42f, 0.48f,
                         float3{0.45f, 0.28f, 0.14f}, 0.f);
                draw_box(building_prog, float3{sx + 10.f, gy + 0.38f, sz + 14.f}, 1.50f, 0.06f, 0.44f,
                         float3{0.22f, 0.22f, 0.22f}, 0.f);
            }
        }
    }
    static bool logged_detail = false;
    if (!logged_detail) {
        std::printf("[city] street kit lamps_instanced=%u+%u trees_instanced=%u+%u+%u\n",
                    lamp_glb[0].instance_count, lamp_glb[1].instance_count, tree_glb[0].instance_count,
                    tree_glb[1].instance_count, tree_glb[2].instance_count);
        std::fflush(stdout);
        logged_detail = true;
    }
    glBindVertexArray(0);
    static bool logged = false;
    if (!logged) {
        std::printf("[gl] drawing %u buildings (GLSL bound)\n", drawn);
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
