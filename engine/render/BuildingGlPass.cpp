#include "render/BuildingGlPass.h"

#include "ecs/World.h"
#include "objects/StreetLight.h"
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
    "void main(){ FragPos=vec3(model*vec4(aPos,1.0)); Normal=mat3(transpose(inverse(model)))*aNormal; UV=aUV;\n"
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

void model_trs(float* m, float3 pos, float sx, float sy, float sz) {
    mat_ident(m);
    m[0]  = sx;
    m[5]  = sy;
    m[10] = sz;
    m[12] = pos.x - sx * 0.5f;
    m[13] = pos.y;
    m[14] = pos.z - sz * 0.5f;
}

void set_building_uniforms(unsigned prog, const float* view, const float* proj, float3 sun,
                           float time_of_day) {
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "view"), 1, GL_FALSE, view);
    glUniformMatrix4fv(glGetUniformLocation(prog, "projection"), 1, GL_FALSE, proj);
    glUniform3f(glGetUniformLocation(prog, "lightDir"), sun.x, sun.y, sun.z);
    glUniform3f(glGetUniformLocation(prog, "lightColor"), 1.f, 0.96f, 0.88f);
    glUniform1f(glGetUniformLocation(prog, "time_of_day"), time_of_day);
}

} // namespace

bool BuildingGlPass::init() {
    ok = false;
    building_prog = street_prog = 0;
    cube_vao = cube_vbo = cube_ibo = 0;
    street_vao = street_vbo = 0;
    street_count = 0;
    num_buildings = 0;

    building_prog =
        make_program("shaders/building.vert", "shaders/building.frag", kBldVs, kBldFs, "building");
    street_prog = make_program("shaders/street.vert", "shaders/street.frag", kStVs, kStFs, "street");
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

    glGenVertexArrays(1, &street_vao);
    glBindVertexArray(street_vao);
    glGenBuffers(1, &street_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, street_vbo);
    glBufferData(GL_ARRAY_BUFFER, 64 * 6 * 5 * static_cast<GLsizeiptr>(sizeof(float)), nullptr,
                 GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);

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

    float verts[64 * 6 * 5];
    u32 n = 0;
    street_count = 0;
    for (Entity e : world.query<StreetComponent>()) {
        StreetComponent* s = world.get<StreetComponent>(e);
        if (!s || n + 6 >= 64 * 6) {
            continue;
        }
        float3 d = float3_sub(s->end, s->start);
        const float len = float3_length(d);
        if (len < 1.f) {
            continue;
        }
        d = float3_scale(d, 1.f / len);
        float3 side = float3_normalize_or(float3_cross(d, float3{0.f, 1.f, 0.f}), float3{1.f, 0.f, 0.f});
        side = float3_scale(side, s->width * 0.5f + 2.4f);
        const float y0 = s->start.y + 0.35f;
        const float y1 = s->end.y + 0.35f;
        const float3 a = float3_sub(s->start, side);
        const float3 b = float3_add(s->start, side);
        const float3 c = float3_add(s->end, side);
        const float3 e2 = float3_sub(s->end, side);
        const float uvlen = len / 12.f;
        const float tri[6][5] = {
            {a.x, y0, a.z, 0.f, 0.f},     {b.x, y0, b.z, 1.f, 0.f}, {c.x, y1, c.z, 1.f, uvlen},
            {a.x, y0, a.z, 0.f, 0.f},     {c.x, y1, c.z, 1.f, uvlen}, {e2.x, y1, e2.z, 0.f, uvlen},
        };
        for (u32 t = 0; t < 6; ++t) {
            for (u32 k = 0; k < 5; ++k) {
                verts[n * 5 + k] = tri[t][k];
            }
            ++n;
        }
        ++street_count;
    }
    glBindBuffer(GL_ARRAY_BUFFER, street_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(n * 5 * sizeof(float)), verts);
    street_count = n; // vertex count for draw
    std::printf("[city] gpu mesh buildings=%u street_verts=%u\n", num_buildings, n);
    std::fflush(stdout);
}

void BuildingGlPass::draw(World& world, float3 camera_pos, float3 camera_target, int width, int height,
                          float time_of_day, float3 sun_dir) {
    if (!ok) {
        return;
    }
    float view[16], proj[16];
    mat_look(view, camera_pos, camera_target, float3{0.f, 1.f, 0.f});
    mat_persp(proj, 1.04719755f, static_cast<float>(width) / max_of(1, height), 0.5f, 4000.f);
    const float3 sun = float3_normalize_or(sun_dir, float3{0.5f, 0.8f, 0.3f});

    glEnable(GL_DEPTH_TEST);
    glUseProgram(street_prog);
    glUniformMatrix4fv(glGetUniformLocation(street_prog, "view"), 1, GL_FALSE, view);
    glUniformMatrix4fv(glGetUniformLocation(street_prog, "projection"), 1, GL_FALSE, proj);
    glBindVertexArray(street_vao);
    if (street_count > 0) {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(street_count));
    }

    set_building_uniforms(building_prog, view, proj, sun, time_of_day);
    glBindVertexArray(cube_vao);
    float model[16];
    u32 drawn = 0;
    for (Entity e : world.query<BuildingComponent>()) {
        BuildingComponent* b = world.get<BuildingComponent>(e);
        if (!b) {
            continue;
        }
        model_trs(model, b->position, b->width, b->height, b->depth);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
        glUniform3f(glGetUniformLocation(building_prog, "albedo"), b->albedo_color.x, b->albedo_color.y,
                    b->albedo_color.z);
        glUniform1f(glGetUniformLocation(building_prog, "roughness"), b->roughness);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 0.f);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        ++drawn;
    }
    for (Entity e : world.query<StreetLightComponent, TransformComponent>()) {
        TransformComponent* xf = world.get<TransformComponent>(e);
        if (!xf) {
            continue;
        }
        const float3 p{xf->position[0], xf->position[1], xf->position[2]};
        model_trs(model, p, xf->scale[0], xf->scale[1], xf->scale[2]);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
        glUniform3f(glGetUniformLocation(building_prog, "albedo"), 0.15f, 0.15f, 0.16f);
        glUniform1f(glGetUniformLocation(building_prog, "roughness"), 0.4f);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 0.f);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        float3 bulb{p.x, p.y + xf->scale[1], p.z};
        model_trs(model, bulb, 0.7f, 0.35f, 0.7f);
        glUniformMatrix4fv(glGetUniformLocation(building_prog, "model"), 1, GL_FALSE, model);
        glUniform3f(glGetUniformLocation(building_prog, "albedo"), 1.f, 0.85f, 0.4f);
        glUniform1f(glGetUniformLocation(building_prog, "emissionBoost"), 1.4f);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
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
    if (street_prog) {
        glDeleteProgram(street_prog);
    }
    building_prog = street_prog = 0;
}

} // namespace engine
