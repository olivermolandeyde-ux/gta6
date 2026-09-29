#include "render/TreeGenerator.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#endif

namespace engine {
namespace {

constexpr u32 kVertMax = 16384;
constexpr u32 kIdxMax  = 49152;

struct PVert {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float cr, cg, cb, ca;
};

struct Acc {
    PVert* v;
    u32*   i;
    u32    nv, ni;
    float  xmin, ymin, zmin, xmax, ymax, zmax;
};

void acc_init(Acc* a) {
    a->v = static_cast<PVert*>(std::malloc(sizeof(PVert) * kVertMax));
    a->i = static_cast<u32*>(std::malloc(sizeof(u32) * kIdxMax));
    a->nv = a->ni = 0;
    a->xmin = a->ymin = a->zmin = 1.0e9f;
    a->xmax = a->ymax = a->zmax = -1.0e9f;
}

void acc_free(Acc* a) {
    std::free(a->v);
    std::free(a->i);
    a->v = nullptr;
    a->i = nullptr;
}

void note(Acc* a, float x, float y, float z) {
    if (x < a->xmin) {
        a->xmin = x;
    }
    if (x > a->xmax) {
        a->xmax = x;
    }
    if (y < a->ymin) {
        a->ymin = y;
    }
    if (y > a->ymax) {
        a->ymax = y;
    }
    if (z < a->zmin) {
        a->zmin = z;
    }
    if (z > a->zmax) {
        a->zmax = z;
    }
}

u32 push_v(Acc* a, float x, float y, float z, float nx, float ny, float nz, float u, float v) {
    if (a->nv >= kVertMax) {
        return a->nv ? a->nv - 1 : 0;
    }
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (nl > 1.0e-6f) {
        nx /= nl;
        ny /= nl;
        nz /= nl;
    }
    PVert& p = a->v[a->nv];
    p.px = x;
    p.py = y;
    p.pz = z;
    p.nx = nx;
    p.ny = ny;
    p.nz = nz;
    p.u = u;
    p.v = v;
    p.cr = p.cg = p.cb = p.ca = 1.f;
    note(a, x, y, z);
    return a->nv++;
}

void tri(Acc* a, u32 a0, u32 a1, u32 a2) {
    if (a->ni + 3 > kIdxMax) {
        return;
    }
    a->i[a->ni++] = a0;
    a->i[a->ni++] = a1;
    a->i[a->ni++] = a2;
}

void axis_frame(float dx, float dy, float dz, float* bx, float* by, float* bz, float* tx, float* ty, float* tz) {
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.0e-6f) {
        dx = 0;
        dy = 1;
        dz = 0;
        len = 1;
    }
    dx /= len;
    dy /= len;
    dz /= len;
    float ux = 0.f, uy = 1.f, uz = 0.f;
    if (std::fabs(dy) > 0.92f) {
        ux = 1.f;
        uy = 0.f;
    }
    *bx = dy * uz - dz * uy;
    *by = dz * ux - dx * uz;
    *bz = dx * uy - dy * ux;
    float bl = std::sqrt(*bx * *bx + *by * *by + *bz * *bz);
    if (bl < 1.0e-6f) {
        *bx = 1;
        *by = 0;
        *bz = 0;
        bl = 1;
    }
    *bx /= bl;
    *by /= bl;
    *bz /= bl;
    *tx = *by * dz - *bz * dy;
    *ty = *bz * dx - *bx * dz;
    *tz = *bx * dy - *by * dx;
}

void add_cyl(Acc* a, float ax, float ay, float az, float bx, float by, float bz, float ra, float rb, int slices) {
    float fx, fy, fz, sx, sy, sz;
    axis_frame(bx - ax, by - ay, bz - az, &fx, &fy, &fz, &sx, &sy, &sz);
    const int n = slices < 6 ? 6 : slices;
    const u32 base = a->nv;
    for (int r = 0; r < 2; ++r) {
        const float t = static_cast<float>(r);
        const float px = ax + (bx - ax) * t;
        const float py = ay + (by - ay) * t;
        const float pz = az + (bz - az) * t;
        const float rad = ra + (rb - ra) * t;
        for (int i = 0; i < n; ++i) {
            const float ang = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
            const float c = std::cos(ang);
            const float s = std::sin(ang);
            const float ox = (fx * c + sx * s) * rad;
            const float oy = (fy * c + sy * s) * rad;
            const float oz = (fz * c + sz * s) * rad;
            push_v(a, px + ox, py + oy, pz + oz, ox, oy, oz, static_cast<float>(i) / static_cast<float>(n), t);
        }
    }
    for (int i = 0; i < n; ++i) {
        const u32 i0 = base + static_cast<u32>(i);
        const u32 i1 = base + static_cast<u32>((i + 1) % n);
        const u32 i2 = base + static_cast<u32>(n + i);
        const u32 i3 = base + static_cast<u32>(n + ((i + 1) % n));
        tri(a, i0, i2, i1);
        tri(a, i1, i2, i3);
    }
}

void add_cone(Acc* a, float cx, float cy, float cz, float r, float h, int slices) {
    add_cyl(a, cx, cy, cz, cx, cy + h, cz, r, 0.02f, slices);
}

void add_card(Acc* a, float cx, float cy, float cz, float rx, float ry, float rz, float ux, float uy, float uz,
              float w, float h) {
    float rxl = std::sqrt(rx * rx + ry * ry + rz * rz);
    float uxl = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (rxl < 1.0e-5f) {
        rxl = 1;
    }
    if (uxl < 1.0e-5f) {
        uxl = 1;
    }
    rx /= rxl;
    ry /= rxl;
    rz /= rxl;
    ux /= uxl;
    uy /= uxl;
    uz /= uxl;
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const float nx = ry * uz - rz * uy;
    const float ny = rz * ux - rx * uz;
    const float nz = rx * uy - ry * ux;
    const u32 v0 = push_v(a, cx - rx * hw - ux * hh, cy - ry * hw - uy * hh, cz - rz * hw - uz * hh, nx, ny, nz, 0, 0);
    const u32 v1 = push_v(a, cx + rx * hw - ux * hh, cy + ry * hw - uy * hh, cz + rz * hw - uz * hh, nx, ny, nz, 1, 0);
    const u32 v2 = push_v(a, cx + rx * hw + ux * hh, cy + ry * hw + uy * hh, cz + rz * hw + uz * hh, nx, ny, nz, 1, 1);
    const u32 v3 = push_v(a, cx - rx * hw + ux * hh, cy - ry * hw + uy * hh, cz - rz * hw + uz * hh, nx, ny, nz, 0, 1);
    tri(a, v0, v1, v2);
    tri(a, v0, v2, v3);
    tri(a, v0, v2, v1);
    tri(a, v0, v3, v2);
}

unsigned solid_tex(u8 r, u8 g, u8 b, u8 a) {
    unsigned tex = 0;
    const u8 px[4] = {r, g, b, a};
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

unsigned leaf_card_tex(u8 r, u8 g, u8 b) {
    constexpr int N = 32;
    u8 px[N * N * 4];
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(N) * 2.f - 1.f;
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(N) * 2.f - 1.f;
            const float d = std::sqrt(u * u + v * v);
            float a = 1.f - (d - 0.15f) / 0.70f;
            if (a < 0.f) {
                a = 0.f;
            }
            if (a > 1.f) {
                a = 1.f;
            }
            const u32 i = static_cast<u32>(y * N + x) * 4u;
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = static_cast<u8>(a * 255.f);
        }
    }
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    return tex;
}

void bind_instances(TreeGlb* out) {
    glGenBuffers(1, &out->instance_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, out->instance_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kTreeInstanceCap * 16 * sizeof(float)), nullptr,
                 GL_DYNAMIC_DRAW);
    for (u32 i = 0; i < out->nprims; ++i) {
        glBindVertexArray(out->prims[i].vao);
        glBindBuffer(GL_ARRAY_BUFFER, out->instance_vbo);
        const u32 stride = 16 * sizeof(float);
        for (u32 k = 0; k < 4; ++k) {
            glEnableVertexAttribArray(3 + k);
            glVertexAttribPointer(3 + k, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                                  reinterpret_cast<void*>(k * 4 * sizeof(float)));
            glVertexAttribDivisor(3 + k, 1);
        }
    }
    glBindVertexArray(0);
}

bool emit(TreeGlb* out, Acc* a, unsigned tex, u32 tw, u32 th, int mask) {
    if (!a->nv || a->ni < 3 || out->nprims >= kTreePrimCap) {
        return false;
    }
    TreePrim& pr = out->prims[out->nprims];
    std::memset(&pr, 0, sizeof(pr));
    glGenVertexArrays(1, &pr.vao);
    glGenBuffers(1, &pr.vbo);
    glGenBuffers(1, &pr.ibo);
    glBindVertexArray(pr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, pr.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(a->nv * sizeof(PVert)), a->v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pr.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(a->ni * sizeof(u32)), a->i, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PVert), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PVert), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PVert), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(PVert), reinterpret_cast<void*>(8 * sizeof(float)));
    glBindVertexArray(0);
    pr.nidx = a->ni;
    pr.tex = tex;
    pr.tex_emit = solid_tex(0, 0, 0, 255);
    pr.tex_w = tw;
    pr.tex_h = th;
    pr.alpha_mask = mask;
    pr.has_alpha = mask;
    pr.cutoff = 0.5f;
    pr.gl_mode = 4;
    out->nverts += a->nv;
    out->nprims++;
    if (a->xmin < out->xmin) {
        out->xmin = a->xmin;
    }
    if (a->xmax > out->xmax) {
        out->xmax = a->xmax;
    }
    if (a->ymin < out->ymin) {
        out->ymin = a->ymin;
    }
    if (a->ymax > out->ymax) {
        out->ymax = a->ymax;
    }
    if (a->zmin < out->zmin) {
        out->zmin = a->zmin;
    }
    if (a->zmax > out->zmax) {
        out->zmax = a->zmax;
    }
    return true;
}

void finish(TreeGlb* out, const char* label) {
    u32 i = 0;
    while (label[i] && i + 1u < 32u) {
        out->label[i] = label[i];
        ++i;
    }
    out->label[i] = 0;
    out->z_up = 0;
    if (out->ymax < out->ymin) {
        out->ymin = 0;
        out->ymax = 1;
    }
    bind_instances(out);
    std::printf("[tree] procedural %s prims=%u verts=%u height=%.2f\n", label, out->nprims, out->nverts,
                out->ymax - out->ymin);
    std::fflush(stdout);
}

void reset_glb(TreeGlb* out) {
    std::memset(out, 0, sizeof(*out));
    out->xmin = out->ymin = out->zmin = 1.0e9f;
    out->xmax = out->ymax = out->zmax = -1.0e9f;
}

} // namespace

bool TreeGenerator::oak(TreeGlb* out) {
    reset_glb(out);
    Acc bark{}, leaf{};
    acc_init(&bark);
    acc_init(&leaf);
    add_cyl(&bark, 0.f, 0.f, 0.f, 0.f, 9.0f, 0.f, 0.30f, 0.15f, 10);
    for (int b = 0; b < 5; ++b) {
        const float yaw = 6.2831853f * static_cast<float>(b) / 5.f + 0.31f;
        const float pit = -0.55f - 0.12f * static_cast<float>(b % 3);
        const float y0 = 5.4f + 0.45f * static_cast<float>(b);
        const float len = 2.2f + 0.15f * static_cast<float>(b);
        const float c = std::cos(yaw) * std::cos(pit);
        const float s = std::sin(yaw) * std::cos(pit);
        const float up = std::sin(pit);
        add_cyl(&bark, 0.f, y0, 0.f, c * len, y0 + up * len + 0.4f, s * len, 0.08f, 0.03f, 7);
    }
    for (int c = 0; c < 10; ++c) {
        const float yaw = 2.399963f * static_cast<float>(c);
        const float pit = -0.15f + 0.22f * static_cast<float>(c % 4);
        const float rr = 1.35f + 0.25f * static_cast<float>(c % 3);
        const float cx = std::cos(yaw) * std::cos(pit) * rr;
        const float cz = std::sin(yaw) * std::cos(pit) * rr;
        const float cy = 7.4f + std::sin(pit) * rr + 0.35f * static_cast<float>(c % 3);
        const float w = 1.55f + 0.12f * static_cast<float>(c % 3);
        add_card(&leaf, cx, cy, cz, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, w, w);
        add_card(&leaf, cx, cy, cz, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, w, w);
        add_card(&leaf, cx, cy, cz, 0.70f, 0.f, 0.70f, -0.2f, 1.f, 0.1f, w * 0.85f, w * 0.85f);
    }
    emit(out, &bark, solid_tex(0x8B, 0x45, 0x13, 255), 1, 1, 0);
    emit(out, &leaf, leaf_card_tex(0x22, 0x8B, 0x22), 32, 32, 1);
    acc_free(&bark);
    acc_free(&leaf);
    finish(out, "oak");
    return out->nprims > 0;
}

bool TreeGenerator::pine(TreeGlb* out) {
    reset_glb(out);
    Acc bark{}, leaf{};
    acc_init(&bark);
    acc_init(&leaf);
    add_cyl(&bark, 0.f, 0.f, 0.f, 0.f, 11.2f, 0.f, 0.20f, 0.12f, 9);
    const float ys[5] = {2.6f, 4.6f, 6.4f, 8.0f, 9.4f};
    const float rs[5] = {1.55f, 1.22f, 0.92f, 0.62f, 0.38f};
    const float hs[5] = {2.55f, 2.20f, 1.85f, 1.50f, 1.25f};
    for (int k = 0; k < 5; ++k) {
        add_cone(&leaf, 0.f, ys[k], 0.f, rs[k], hs[k], 10);
        const int ncard = 6 + k;
        for (int i = 0; i < ncard; ++i) {
            const float yaw = 6.2831853f * static_cast<float>(i) / static_cast<float>(ncard) + 0.2f * k;
            const float cr = rs[k] * 0.72f;
            add_card(&leaf, std::cos(yaw) * cr, ys[k] + hs[k] * 0.35f, std::sin(yaw) * cr, -std::sin(yaw), 0.25f,
                     std::cos(yaw), 0.f, 1.f, 0.f, 1.1f, 1.35f);
        }
    }
    emit(out, &bark, solid_tex(0x65, 0x43, 0x21, 255), 1, 1, 0);
    emit(out, &leaf, leaf_card_tex(0x00, 0x64, 0x00), 32, 32, 1);
    acc_free(&bark);
    acc_free(&leaf);
    finish(out, "pine");
    return out->nprims > 0;
}

bool TreeGenerator::palm(TreeGlb* out) {
    reset_glb(out);
    Acc bark{}, leaf{};
    acc_init(&bark);
    acc_init(&leaf);
    float px = 0.f, py = 0.f, pz = 0.f;
    const int segs = 10;
    for (int s = 0; s < segs; ++s) {
        const float t0 = static_cast<float>(s) / static_cast<float>(segs);
        const float t1 = static_cast<float>(s + 1) / static_cast<float>(segs);
        const float x0 = 0.38f * std::sin(t0 * 1.4f);
        const float x1 = 0.38f * std::sin(t1 * 1.4f);
        const float y0 = t0 * 11.0f;
        const float y1 = t1 * 11.0f;
        add_cyl(&bark, x0, y0, 0.f, x1, y1, 0.f, 0.26f - 0.08f * t0, 0.26f - 0.08f * t1, 8);
        px = x1;
        py = y1;
        pz = 0.f;
    }
    for (int f = 0; f < 7; ++f) {
        const float yaw = 6.2831853f * static_cast<float>(f) / 7.f;
        const float droop = 0.62f;
        float x = px;
        float y = py + 0.15f;
        float z = pz;
        const float len = 3.05f;
        const int nq = 6;
        for (int q = 0; q < nq; ++q) {
            const float t0 = static_cast<float>(q) / static_cast<float>(nq);
            const float t1 = static_cast<float>(q + 1) / static_cast<float>(nq);
            const float x1 = px + std::cos(yaw) * len * t1;
            const float z1 = pz + std::sin(yaw) * len * t1;
            const float y1 = py - droop * t1 * t1 * 2.4f;
            const float mx = (x + x1) * 0.5f;
            const float my = (y + y1) * 0.5f;
            const float mz = (z + z1) * 0.5f;
            const float dx = x1 - x;
            const float dy = y1 - y;
            const float dz = z1 - z;
            add_card(&leaf, mx, my, mz, dx, dy, dz, -std::sin(yaw), 0.15f, std::cos(yaw), 0.55f, 0.95f);
            x = x1;
            y = y1;
            z = z1;
        }
    }
    emit(out, &bark, solid_tex(0x8B, 0x73, 0x55, 255), 1, 1, 0);
    emit(out, &leaf, leaf_card_tex(0x32, 0xCD, 0x32), 32, 32, 1);
    acc_free(&bark);
    acc_free(&leaf);
    finish(out, "palm");
    return out->nprims > 0;
}

} // namespace engine
