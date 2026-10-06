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

void draw_instanced_glb(TreeGlb* g, unsigned prog) {
    if (!prog || !g || g->instance_count == 0) {
        return;
    }
    glUseProgram(prog);
    glDisable(GL_CULL_FACE);  // Disable backface culling - fixes invisible walls
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(prog, "uEmissive"), 1);
    glUniform1i(glGetUniformLocation(prog, "uShadow"), 2);
    for (u32 p = 0; p < g->nprims; ++p) {
        TreePrim& pr = g->prims[p];
        glBindVertexArray(pr.vao);

        // Check if texture is valid
        bool has_texture = (pr.tex != 0);
        if (!has_texture) {
            std::printf("[buildings] WARNING: Primitive %u has NO texture (tex=0), using fallback color\n", p);
        }

        glActiveTexture(GL_TEXTURE0);
        if (has_texture) {
            glBindTexture(GL_TEXTURE_2D, pr.tex);
        } else {
            // Bind a 1x1 fallback texture or leave unbound (shader should handle this)
            // For now, we'll just leave it unbound and hope the shader has a fallback
        }
        glActiveTexture(GL_TEXTURE1);
        if (pr.tex_emit && has_texture) {
            glBindTexture(GL_TEXTURE_2D, pr.tex_emit);
        } else if (has_texture) {
            glBindTexture(GL_TEXTURE_2D, pr.tex);
        }

        glUniform1i(glGetUniformLocation(prog, "uAlphaMask"), pr.alpha_mask);
        glUniform1f(glGetUniformLocation(prog, "uAlphaCut"), pr.cutoff);
        glUniform1i(glGetUniformLocation(prog, "uUseTexture"), has_texture ? 1 : 0);  // Disable texture if missing

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
    std::memset(&sports_glb, 0, sizeof(sports_glb));
    std::memset(&suv_glb, 0, sizeof(suv_glb));
    corolla_glb.wheel_axis = sports_glb.wheel_axis = suv_glb.wheel_axis = -1;
    std::memset(shop_glb, 0, sizeof(shop_glb));
    std::memset(apartment_glb, 0, sizeof(apartment_glb));
    std::memset(warehouse_glb, 0, sizeof(warehouse_glb));
    std::memset(shop_mats, 0, sizeof(shop_mats));
    std::memset(apartment_mats, 0, sizeof(apartment_mats));
    std::memset(warehouse_mats, 0, sizeof(warehouse_mats));
    shop_count = apartment_count = warehouse_count = 0;
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
                std::printf("[buildings]   Prim %u: %u verts, tex=%u (%ux%u), emit_tex=%u, "
                            "has_alpha=%d, alpha_mask=%d, gl_mode=%d\n",
                            p, pr.nidx, pr.tex, pr.tex_w, pr.tex_h, pr.tex_emit,
                            pr.has_alpha, pr.alpha_mask, pr.gl_mode);
                if (pr.tex == 0) {
                    std::printf("[buildings]   WARNING: Prim %u has NO texture bound! Will render pink.\n", p);
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
    std::printf("[buildings] All building primitives now render as OPAQUE (no alpha blending)\n");
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

    // === NEW GLB BUILDING PLACEMENT ===
    // DEBUGGING: Verbose logging + simplified fixed positions for verification
    {
        std::printf("\n=== [buildings] PLACEMENT START ===\n");

        static float new_bld_xz[kTreeSpawnCap * 2];
        u32 n_new_bld_xz = 0;
        auto bld_xz_too_close = [&](float x, float z, float min_d) -> bool {
            const float m2 = min_d * min_d;
            for (u32 i = 0; i < n_new_bld_xz; ++i) {
                const float dx = x - new_bld_xz[i * 2u];
                const float dz = z - new_bld_xz[i * 2u + 1u];
                if (dx * dx + dz * dz < m2) {
                    return true;
                }
            }
            return false;
        };
        auto bld_on_sidewalk = [&](float x, float z) -> bool {
            const float droad = dist_to_road_edge(x, z);
            return (droad >= 0.4f && droad <= 3.2f);
        };
        // Check if position is on a building lot (inside a block, not on road/sidewalk)
        auto on_building_lot = [&](float x, float z) -> bool {
            // A building lot is inside a city block, away from roads and sidewalks
            // Block size = kCityBlockPitch (120m), road = kCityStreetWidth (20m), sidewalk = 3m
            const float road_half = kCityStreetWidth * 0.5f;  // 10m
            const float sidewalk = 3.0f;
            const float block_inner = road_half + sidewalk + 2.0f; // 15m from block center

            // Find nearest block center
            const float bx = std::round(x / kCityBlockPitch) * kCityBlockPitch;
            const float bz = std::round(z / kCityBlockPitch) * kCityBlockPitch;

            // Distance from block center
            const float dx = std::fabs(x - bx);
            const float dz = std::fabs(z - bz);

            // Must be inside the block (not in road/intersection)
            // Block half-size = kCityBlockPitch/2 = 60m
            // Road takes 10m from edge, sidewalk 3m more = 13m from edge
            const float max_dist_from_center = kCityBlockPitch * 0.5f - block_inner;
            if (dx > max_dist_from_center || dz > max_dist_from_center) {
                return false; // On road, sidewalk, or intersection
            }

            // Also check we're not too close to block edges (for building placement)
            const float min_dist_from_edge = 5.0f; // 5m from block edge for building spacing
            if (dx < min_dist_from_edge || dz < min_dist_from_edge) {
                return false; // Too close to edge
            }

            return true;
        };
        auto dist_to_any_lamp = [&](float x, float z) -> float {
            float best = 1.0e9f;
            for (u32 i = 0; i < n_lamp_xz; ++i) {
                const float dx = x - lamp_xz[i * 2u];
                const float dz = z - lamp_xz[i * 2u + 1u];
                const float d2 = dx * dx + dz * dz;
                if (d2 < best) best = d2;
            }
            return std::sqrt(best);
        };

        // Collect building positions by district
        struct BldPos { float x, z; u32 district; u32 building_id; };
        static BldPos bld_positions[kTreeSpawnCap];
        u32 n_bld_positions = 0;

        for (Entity e : world.query<BuildingComponent>()) {
            BuildingComponent* b = world.get<BuildingComponent>(e);
            if (!b) continue;
            if (custom_sky_lot(b)) continue;
            bld_positions[n_bld_positions++] = {b->position.x, b->position.z, b->district, b->building_id};
        }

        std::printf("[buildings] Found %u procedural buildings to potentially replace\n", n_bld_positions);

        // Decide how many buildings to place: target counts
        const u32 target_shops = 24;
        const u32 target_apts = 90;
        const u32 target_whses = 86;
        u32 shops_placed = 0, apts_placed = 0, whses_placed = 0;

        std::printf("[buildings] === PLACEMENT ===\n");
        std::printf("[buildings] Target: %u shops, %u apartments, %u warehouses\n", target_shops, target_apts, target_whses);
        std::printf("[buildings] City layout: %u m blocks, %u m roads\n", (u32)kCityBlockPitch, (u32)kCityStreetWidth);
        std::printf("[buildings] Block centers at: 60, 180, 300... (60 + 120*n)\n");
        std::printf("[buildings] Strategy: Place at block center + random offset (-30 to +30m)\n");

        // Get existing building positions for overlap check
        struct ExistingBld { float x, z; };
        static ExistingBld existing_bdls[kTreeSpawnCap];
        u32 n_existing = 0;
        for (Entity e : world.query<BuildingComponent>()) {
            BuildingComponent* b = world.get<BuildingComponent>(e);
            if (!b) continue;
            if (custom_sky_lot(b)) continue;
            existing_bdls[n_existing++] = {b->position.x, b->position.z};
        }
        std::printf("[buildings] Found %u existing buildings for overlap check\n", n_existing);
        auto on_road = [&](float x, float z) -> bool {
            const float road_half = kCityStreetWidth * 0.5f;  // 10m
            // Check if near a road line (multiples of 120)
            const float dx_road = std::fmod(x, kCityBlockPitch);
            const float dz_road = std::fmod(z, kCityBlockPitch);
            // On road if within 10m of a road line
            const bool on_x_road = (dx_road < road_half || dx_road > kCityBlockPitch - road_half);
            const bool on_z_road = (dz_road < road_half || dz_road > kCityBlockPitch - road_half);
            return on_x_road || on_z_road;
        };

        // Helper: check spacing between new buildings
        auto too_close_new = [&](float x, float z) -> bool {
            const float min_dist = 15.0f;
            const float m2 = min_dist * min_dist;
            for (u32 i = 0; i < n_new_bld_xz; ++i) {
                const float dx = x - new_bld_xz[i * 2u];
                const float dz = z - new_bld_xz[i * 2u + 1u];
                if (dx * dx + dz * dz < m2) {
                    return true;
                }
            }
            return false;
        };

        // Random number generator (simple LCG)
        auto rand_float = [&](float min, float max) -> float {
            static u32 seed = 12345;
            seed = seed * 1664525u + 1013904223u;
            const float r = static_cast<float>(seed) / static_cast<float>(0xFFFFFFFFu);
            return min + r * (max - min);
        };

        // Place buildings at block centers with random offsets
        auto try_place = [&](u32 block_i, u32 block_j, u32& placed, u32 target, u32& attempts,
                            TreeGlb (&glb_array)[3], u32& count, const char* type_name, float* mats_array,
                            u32 district_pref) -> bool {
            if (placed >= target) return false;

            // Calculate block center
            const float center_x = 60.f + block_i * kCityBlockPitch;
            const float center_z = 60.f + block_j * kCityBlockPitch;

            // Check bounds
            if (center_x >= kCityExtentM || center_z >= kCityExtentM) return false;
            if (center_x < 60.f || center_z < 60.f) return false;

            attempts++;

            // Add random offset within the lot (-30 to +30m from center)
            const float offset_x = rand_float(-25.f, 25.f);
            const float offset_z = rand_float(-25.f, 25.f);
            const float pos_x = center_x + offset_x;
            const float pos_z = center_z + offset_z;

            // Check if on road
            if (on_road(pos_x, pos_z)) return false;

            // Check overlap with existing buildings
            if (overlaps_existing(pos_x, pos_z)) return false;

            // Check spacing with other new buildings
            if (too_close_new(pos_x, pos_z)) return false;

            // Select variant
            u32 variant = (block_i * 73u + block_j * 131u + placed * 37u) % 3u;
            if (glb_array[variant].nprims == 0) return false;
            if (count >= kTreeInstanceCap) return false;

            // Place building
            const float yaw = rand_float(0.f, 6.2831853f);
            const float target_h = (strcmp(type_name, "shop") == 0) ? 4.0f :
                                   (strcmp(type_name, "apartment") == 0) ? 18.0f : 10.0f;
            const float sc = glb_fit_scale(&glb_array[variant], target_h);
            const float y0 = glb_array[variant].z_up ? glb_array[variant].zmin : glb_array[variant].ymin;
            const float y = kCityPlateauY + 0.05f - y0 * sc;

            tree_yaw_mat(&mats_array[count * 16], pos_x, y, pos_z, yaw, sc, glb_array[variant].z_up);
            ++count;
            new_bld_xz[n_new_bld_xz * 2u] = pos_x;
            new_bld_xz[n_new_bld_xz * 2u + 1] = pos_z;
            ++n_new_bld_xz;
            ++placed;

            return true;
        };

        // Iterate through all blocks and place buildings
        u32 shop_attempts = 0, apt_attempts = 0, whse_attempts = 0;
        u32 shop_skipped = 0, apt_skipped = 0, whse_skipped = 0;

        // Place shops in commercial/retail/downtown blocks
        for (u32 bi = 0; bi < 20 && shops_placed < target_shops; ++bi) {
            for (u32 bj = 0; bj < 20 && shops_placed < target_shops; ++bj) {
                const float cx = 60.f + bi * kCityBlockPitch;
                const float cz = 60.f + bj * kCityBlockPitch;
                if (cx >= kCityExtentM || cz >= kCityExtentM) continue;
                if (cx < 60.f || cz < 60.f) continue;

                // Determine district
                const float dist_from_center = std::sqrt((cx - kCityCenterM) * (cx - kCityCenterM) +
                                                         (cz - kCityCenterM) * (cz - kCityCenterM));
                u32 district = kDistrictResidential;
                if (dist_from_center < 200.f) district = kDistrictDowntown;
                else if (dist_from_center < 400.f) district = kDistrictCommercial;

                // Shops go in commercial/retail/downtown
                if (district == kDistrictCommercial || district == kDistrictRetail || district == kDistrictDowntown) {
                    if (try_place(bi, bj, shops_placed, target_shops, shop_attempts,
                                  shop_glb, shop_count, "shop", shop_mats, district)) {
                        std::printf("[buildings] Placed shop #%u at (%.1f, %.1f), variant=%u\n",
                                    shops_placed, new_bld_xz[(n_new_bld_xz-1)*2u], new_bld_xz[(n_new_bld_xz-1)*2u+1],
                                    (bi * 73u + bj * 131u + shops_placed * 37u) % 3u);
                    } else {
                        ++shop_skipped;
                    }
                }
            }
        }

        // Place apartments in residential/suburban/downtown blocks
        for (u32 bi = 0; bi < 20 && apartment_count < target_apts; ++bi) {
            for (u32 bj = 0; bj < 20 && apartment_count < target_apts; ++bj) {
                const float cx = 60.f + bi * kCityBlockPitch;
                const float cz = 60.f + bj * kCityBlockPitch;
                if (cx >= kCityExtentM || cz >= kCityExtentM) continue;
                if (cx < 60.f || cz < 60.f) continue;

                const float dist_from_center = std::sqrt((cx - kCityCenterM) * (cx - kCityCenterM) +
                                                         (cz - kCityCenterM) * (cz - kCityCenterM));
                u32 district = kDistrictResidential;
                if (dist_from_center < 200.f) district = kDistrictDowntown;
                else if (dist_from_center > 600.f) district = kDistrictSuburban;

                if (district == kDistrictResidential || district == kDistrictSuburban || district == kDistrictDowntown) {
                    if (try_place(bi, bj, apartment_count, target_apts, apt_attempts,
                                  apartment_glb, apartment_count, "apartment", apartment_mats, district)) {
                        std::printf("[buildings] Placed apartment #%u at (%.1f, %.1f), variant=%u\n",
                                    apartment_count, new_bld_xz[(n_new_bld_xz-1)*2u], new_bld_xz[(n_new_bld_xz-1)*2u+1],
                                    (bi * 73u + bj * 131u + apartment_count * 37u) % 3u);
                    } else {
                        ++apt_skipped;
                    }
                }
            }
        }

        // Place warehouses in industrial blocks or at edges
        for (u32 bi = 0; bi < 20 && warehouse_count < target_whses; ++bi) {
            for (u32 bj = 0; bj < 20 && warehouse_count < target_whses; ++bj) {
                const float cx = 60.f + bi * kCityBlockPitch;
                const float cz = 60.f + bj * kCityBlockPitch;
                if (cx >= kCityExtentM || cz >= kCityExtentM) continue;
                if (cx < 60.f || cz < 60.f) continue;

                // Warehouses go at edges or industrial zones
                const bool at_edge = (bi < 2 || bj < 2 || bi > 17 || bj > 17);
                const float dist_from_center = std::sqrt((cx - kCityCenterM) * (cx - kCityCenterM) +
                                                         (cz - kCityCenterM) * (cz - kCityCenterM));
                const bool industrial = dist_from_center > 500.f;

                if (at_edge || industrial) {
                    if (try_place(bi, bj, warehouse_count, target_whses, whse_attempts,
                                  warehouse_glb, warehouse_count, "warehouse", warehouse_mats, kDistrictIndustrial)) {
                        std::printf("[buildings] Placed warehouse #%u at (%.1f, %.1f), variant=%u\n",
                                    warehouse_count, new_bld_xz[(n_new_bld_xz-1)*2u], new_bld_xz[(n_new_bld_xz-1)*2u+1],
                                    (bi * 73u + bj * 131u + warehouse_count * 37u) % 3u);
                    } else {
                        ++whse_skipped;
                    }
                }
            }
        }

        std::printf("\n[buildings] === PLACEMENT SUMMARY ===\n");
        std::printf("[buildings] Shops: placed %u (attempted %u, skipped %u)\n", shops_placed, shop_attempts, shop_skipped);
        std::printf("[buildings] Apartments: placed %u (attempted %u, skipped %u)\n", apartment_count, apt_attempts, apt_skipped);
        std::printf("[buildings] Warehouses: placed %u (attempted %u, skipped %u)\n", warehouse_count, whse_attempts, whse_skipped);
        std::printf("[buildings] Total new buildings: %u\n", shop_count + apartment_count + warehouse_count);
        std::printf("[buildings] ==========================\n\n");
        std::fflush(stdout);
}

} // namespace engine
