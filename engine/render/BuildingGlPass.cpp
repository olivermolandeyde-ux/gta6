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

bool load_text_file(const char* rel, char* dst, u32 cap) {
    char abs0[512];
    std::snprintf(abs0, sizeof(abs0), "%s/%s", LEONIDA_SOURCE_DIR, rel);
    const char* paths[3] = {rel, abs0, nullptr};
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

void tree_yaw_mat(float* m, float x, float y, float z, float yaw, float sc) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    std::memset(m, 0, 16 * sizeof(float));
    m[0]  = c * sc;
    m[2]  = -s * sc;
    m[5]  = sc;
    m[8]  = s * sc;
    m[10] = c * sc;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1.f;
}

void draw_tree_glbs(TreeGlb* trees, unsigned prog) {
    if (!prog) {
        return;
    }
    glUseProgram(prog);
    glDisable(GL_CULL_FACE);
    glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 0);
    glUniform1i(glGetUniformLocation(prog, "uShadow"), 2);
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        if (trees[k].instance_count == 0) {
            continue;
        }
        for (u32 p = 0; p < trees[k].nprims; ++p) {
            TreePrim& pr = trees[k].prims[p];
            glBindVertexArray(pr.vao);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, pr.tex);
            glUniform1i(glGetUniformLocation(prog, "uAlphaMask"), pr.alpha_mask);
            glUniform1f(glGetUniformLocation(prog, "uAlphaCut"), pr.cutoff);
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(pr.nidx), GL_UNSIGNED_INT, nullptr,
                                    static_cast<GLsizei>(trees[k].instance_count));
        }
    }
    glBindVertexArray(0);
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
        "#version 330 core\nlayout(location=0) in vec3 aPos; layout(location=3) in vec4 iM0;\n"
        "layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;\n"
        "uniform mat4 view,projection,uLightVP; out vec3 FragPos; out vec3 Normal; out vec2 UV; out vec4 LightPos;\n"
        "void main(){ mat4 model=mat4(iM0,iM1,iM2,iM3); vec4 wp=model*vec4(aPos,1.0); FragPos=wp.xyz; Normal=vec3(0,1,0); UV=vec2(0.0); LightPos=uLightVP*wp; gl_Position=projection*view*wp; }\n";
    constexpr const char* kTreeFbFs =
        "#version 330 core\nin vec3 FragPos; in vec3 Normal; in vec2 UV; in vec4 LightPos; out vec4 FragColor;\n"
        "void main(){ FragColor=vec4(0.2,0.5,0.2,1.0); }\n";
    tree_prog = make_program("shaders/tree.vert", "shaders/tree.frag", kTreeFbVs, kTreeFbFs, "tree");
    tree_shadow_prog = 0;
    load_city_tree("oak_tree_realistic.glb", &tree_glb[0]);
    load_city_tree("pine_tree_realistic.glb", &tree_glb[1]);
    load_city_tree("palm_tree_realistic.glb", &tree_glb[2]);
    std::printf("[gl] Loaded 3 tree models: oak (%u verts), pine (%u verts), palm (%u verts)\n",
                tree_glb[0].nverts, tree_glb[1].nverts, tree_glb[2].nverts);
    std::printf("[gl] tree shadows disabled (perf) — 3 instanced draws, spawn cap %u\n", kTreeSpawnCap);
    std::fflush(stdout);

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

    static float tree_mats[kTreeKindCount][kTreeInstanceCap * 16];
    u32 tn[kTreeKindCount] = {0, 0, 0};
    auto skip_cross = [](float t) {
        const float g = t / kCityBlockPitch;
        const float f = g - std::floor(g);
        return f < 0.16f || f > 0.84f;
    };
    auto push_tree = [&](float x, float z) {
        const u32 total = tn[0] + tn[1] + tn[2];
        if (total >= kTreeSpawnCap) {
            return;
        }
        const u32 kind = (static_cast<u32>(x) / 24u + static_cast<u32>(z) / 24u) % 3u;
        if (tn[kind] >= kTreeInstanceCap) {
            return;
        }
        const float yaw = std::fmod(x * 0.173f + z * 0.091f, 6.2831853f);
        const float sc  = 0.85f + std::fmod(x * 0.031f + z * 0.017f, 0.30f);
        tree_yaw_mat(&tree_mats[kind][tn[kind] * 16], x, kCityPlateauY + 0.05f, z, yaw, sc);
        ++tn[kind];
    };
    const float sw = kCityStreetWidth * 0.5f + 1.6f;
    // Every 4th street, every 4th sidewalk tile (~96 m) — ~200–280 trees total.
    for (u32 j = 0; j <= kCityBlocks; j += 4) {
        const float z = static_cast<float>(j) * kCityBlockPitch;
        for (float x = 48.f; x < kCityExtentM - 48.f; x += 96.f) {
            if (!skip_cross(x)) {
                const float side = (static_cast<u32>(x) % 192u < 96u) ? sw : -sw;
                push_tree(x, z + side);
            }
        }
    }
    for (u32 i = 0; i <= kCityBlocks; i += 4) {
        const float x = static_cast<float>(i) * kCityBlockPitch;
        for (float z = 48.f; z < kCityExtentM - 48.f; z += 96.f) {
            if (!skip_cross(z)) {
                const float side = (static_cast<u32>(z) % 192u < 96u) ? sw : -sw;
                push_tree(x + side, z);
            }
        }
    }
    for (float t = 80.f; t < 400.f; t += 40.f) {
        push_tree(80.f + t * 0.15f, 80.f + std::fmod(t * 1.7f, 90.f));
    }
    u32 total_trees = 0;
    for (u32 k = 0; k < kTreeKindCount; ++k) {
        tree_glb_set_instances(&tree_glb[k], tree_mats[k], tn[k]);
        total_trees += tn[k];
    }
    std::printf("[city] Spawning %u trees with custom models\n", total_trees);
    std::fflush(stdout);
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
            if (!b || !near_xz(b->position, camera_pos, 140.f)) {
                continue;
            }
            model_trs(sm, b->position, b->width, b->height, b->depth);
            glUniformMatrix4fv(glGetUniformLocation(shadow_prog, "model"), 1, GL_FALSE, sm);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
            if (++sc > 220) {
                break;
            }
        }
        for (Entity e : world.query<StreetLightComponent, TransformComponent>()) {
            TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf) {
                continue;
            }
            float3 p{xf->position[0], xf->position[1], xf->position[2]};
            if (!near_xz(p, camera_pos, 90.f)) {
                continue;
            }
            model_axis(sm, p, 0.14f, 7.2f, 0.14f);
            glBindVertexArray(cyl_vao);
            glUniformMatrix4fv(glGetUniformLocation(shadow_prog, "model"), 1, GL_FALSE, sm);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cyl_count), GL_UNSIGNED_INT, nullptr);
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
        if (!b) {
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
    const float night = (time_of_day >= 20.f || time_of_day < 6.f) ? 1.f : 0.f;
    const float3 metal{0.18f, 0.18f, 0.20f};
    const float3 lamp_col{1.f, 0.86f, 0.35f};
    u32 lamps = 0;
    for (Entity e : world.query<StreetLightComponent, TransformComponent>()) {
        TransformComponent* xf = world.get<TransformComponent>(e);
        if (!xf) {
            continue;
        }
        const float3 p{xf->position[0], xf->position[1], xf->position[2]};
        if (!near_xz(p, camera_pos, 280.f)) {
            continue;
        }
        const float nx = std::round(p.x / kCityBlockPitch) * kCityBlockPitch;
        const float nz = std::round(p.z / kCityBlockPitch) * kCityBlockPitch;
        const bool ew = std::fabs(p.z - nz) > std::fabs(p.x - nx);
        const float dir = ew ? ((p.z > nz) ? -1.f : 1.f) : ((p.x > nx) ? -1.f : 1.f);
        glBindVertexArray(cube_vao);
        draw_box(building_prog, p, 0.42f, 0.32f, 0.42f, float3{0.58f, 0.57f, 0.54f}, 0.f);
        draw_axis_mesh(building_prog, cyl_vao, cyl_count, p, 0.11f, 7.0f, 0.11f, metal, 0.f, 0, 0);
        glBindVertexArray(cube_vao);
        for (u32 s = 0; s < 4; ++s) {
            const float t = (static_cast<float>(s) + 0.5f) / 4.f;
            const float along = t * 2.6f;
            const float drop = t * t * 0.55f;
            float3 sp = ew ? float3{p.x, p.y + 6.95f - drop, p.z + dir * along}
                           : float3{p.x + dir * along, p.y + 6.95f - drop, p.z};
            if (ew) {
                draw_box(building_prog, sp, 0.10f, 0.10f, 0.72f, metal, 0.f);
            } else {
                draw_box(building_prog, sp, 0.72f, 0.10f, 0.10f, metal, 0.f);
            }
        }
        float3 head = ew ? float3{p.x, p.y + 6.45f, p.z + dir * 2.55f}
                         : float3{p.x + dir * 2.55f, p.y + 6.45f, p.z};
        draw_box(building_prog, head, ew ? 0.70f : 0.36f, 0.26f, ew ? 0.36f : 0.70f, float3{0.12f, 0.12f, 0.13f},
                 0.f);
        draw_box(building_prog, float3{head.x, head.y - 0.16f, head.z}, 0.46f, 0.05f, 0.46f, lamp_col,
                 0.9f + night * 2.6f);
        if (night > 0.5f) {
            draw_box(building_prog, float3{head.x, kCityPlateauY + 0.03f, head.z}, 5.5f, 0.04f, 5.5f,
                     float3{1.f, 0.85f, 0.35f}, 1.8f);
        }
        ++lamps;
    }
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
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, shadow_tex);
        draw_tree_glbs(tree_glb, tree_prog);
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
        std::printf("[city] street kit lamps_near=%u trees_instanced=%u+%u+%u\n", lamps,
                    tree_glb[0].instance_count, tree_glb[1].instance_count, tree_glb[2].instance_count);
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
    building_prog = street_prog = 0;
}

} // namespace engine
