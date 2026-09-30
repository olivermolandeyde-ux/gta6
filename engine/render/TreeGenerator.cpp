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
}

float fr01(u32* s) {
    *s = *s * 1664525u + 1013904223u;
    return static_cast<float>((*s >> 8) & 0x00ffffffu) / 16777215.f;
}

void tint_last(Acc* a, u32 n, float r, float g, float b) {
    const u32 start = a->nv > n ? a->nv - n : 0;
    for (u32 i = start; i < a->nv; ++i) {
        a->v[i].cr = r;
        a->v[i].cg = g;
        a->v[i].cb = b;
    }
}

void add_jagged_cone(Acc* a, float cx, float cy, float cz, float r, float h, int slices, u32 seed) {
    const int n = slices < 8 ? 8 : slices;
    const u32 base = a->nv;
    for (int ring = 0; ring < 2; ++ring) {
        for (int i = 0; i < n; ++i) {
            u32 st = seed + static_cast<u32>(i * 17 + ring * 9);
            const float ang = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
            const float jig = 0.82f + 0.28f * fr01(&st);
            const float rad = (ring == 0) ? r * jig : 0.04f + 0.03f * fr01(&st);
            const float ox = std::cos(ang) * rad;
            const float oz = std::sin(ang) * rad;
            const float y = cy + (ring == 0 ? 0.f : h);
            push_v(a, cx + ox, y, cz + oz, ox, 0.35f, oz, 0.5f, 0.5f);
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

void leaf_cluster(Acc* a, float x, float y, float z, float w, u32 seed) {
    const int n = 3 + static_cast<int>(fr01(&seed) * 2.99f);
    for (int i = 0; i < n; ++i) {
        const float yaw = fr01(&seed) * 6.2831853f;
        const float pit = (fr01(&seed) - 0.5f) * 1.1f;
        const float roll = (fr01(&seed) - 0.5f) * 0.8f;
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const float cp = std::cos(pit);
        const float rx = c * cp;
        const float ry = std::sin(pit) * 0.4f;
        const float rz = s * cp;
        const float ux = -s * std::cos(roll);
        const float uy = std::cos(pit) * 0.85f + 0.2f;
        const float uz = c * std::cos(roll);
        const float jx = (fr01(&seed) - 0.5f) * 0.55f;
        const float jy = (fr01(&seed) - 0.5f) * 0.45f;
        const float jz = (fr01(&seed) - 0.5f) * 0.55f;
        const float ws = w * (0.82f + 0.28f * fr01(&seed));
        add_card(a, x + jx, y + jy, z + jz, rx, ry, rz, ux, uy, uz, ws, ws * 0.92f);
        const float g = 0.78f + 0.35f * fr01(&seed);
        tint_last(a, 4, 0.72f + 0.2f * fr01(&seed), g, 0.55f + 0.25f * fr01(&seed));
    }
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
    const float trunk_h = 3.6f;
    add_cyl(&bark, 0.f, 0.f, 0.f, 0.f, trunk_h, 0.f, 0.40f, 0.20f, 11);
    u32 seed = 0xA41C17u;
    struct Tip {
        float x, y, z;
    };
    Tip tips[8];
    const int nbr = 6;
    for (int b = 0; b < nbr; ++b) {
        const float yaw = 6.2831853f * static_cast<float>(b) / static_cast<float>(nbr) + 0.18f +
                          (fr01(&seed) - 0.5f) * 0.35f;
        const float elev = (30.f + 30.f * fr01(&seed) + (fr01(&seed) - 0.5f) * 15.f) * 0.0174533f;
        const float y0 = 2.15f + 1.15f * fr01(&seed);
        const float len = 1.55f + 0.95f * fr01(&seed);
        const float c = std::cos(yaw) * std::cos(elev);
        const float s = std::sin(yaw) * std::cos(elev);
        const float up = std::sin(elev);
        const float x1 = c * len;
        const float y1 = y0 + up * len;
        const float z1 = s * len;
        add_cyl(&bark, 0.04f * c, y0, 0.04f * s, x1, y1, z1, 0.15f, 0.08f, 8);
        tips[b].x = x1;
        tips[b].y = y1;
        tips[b].z = z1;
    }
    tips[6].x = 0.f;
    tips[6].y = trunk_h + 0.15f;
    tips[6].z = 0.f;
    tips[7].x = 0.12f;
    tips[7].y = trunk_h - 0.2f;
    tips[7].z = -0.08f;
    int ncl = 0;
    for (int t = 0; t < 8 && ncl < 20; ++t) {
        const int extra = (t < nbr) ? 2 : 1;
        for (int k = 0; k < extra && ncl < 20; ++k) {
            const float jx = (fr01(&seed) - 0.5f) * 0.60f;
            const float jy = (fr01(&seed) - 0.5f) * 0.50f;
            const float jz = (fr01(&seed) - 0.5f) * 0.60f;
            const float w = 0.80f + 0.40f * fr01(&seed);
            leaf_cluster(&leaf, tips[t].x + jx, tips[t].y + jy, tips[t].z + jz, w, seed + static_cast<u32>(ncl * 97));
            ++ncl;
        }
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
    add_cyl(&bark, 0.f, 0.f, 0.f, 0.f, 2.05f, 0.f, 0.25f, 0.18f, 9);
    const float width[6] = {3.0f, 2.6f, 2.2f, 1.8f, 1.4f, 1.0f};
    const float ht[6]    = {1.5f, 1.2f, 1.2f, 1.2f, 1.2f, 1.0f};
    float y = 1.85f;
    u32 seed = 0x51C0u;
    for (int k = 0; k < 6; ++k) {
        const float ox = (fr01(&seed) - 0.5f) * 0.18f;
        const float oz = (fr01(&seed) - 0.5f) * 0.18f;
        add_jagged_cone(&leaf, ox, y, oz, width[k] * 0.5f, ht[k], 12, seed);
        const int ncard = 5 + (k & 1);
        for (int i = 0; i < ncard; ++i) {
            const float yaw = 6.2831853f * static_cast<float>(i) / static_cast<float>(ncard) + 0.15f * k;
            const float cr = width[k] * 0.38f;
            add_card(&leaf, ox + std::cos(yaw) * cr, y + ht[k] * 0.40f, oz + std::sin(yaw) * cr, -std::sin(yaw),
                     0.35f, std::cos(yaw), 0.1f, 1.f, 0.05f, 0.85f, 1.05f);
        }
        y += 1.05f;
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
    const float seg_y[5] = {0.f, 1.45f, 2.95f, 4.35f, 5.55f};
    const float seg_x[5] = {0.f, 0.12f, 0.28f, 0.38f, 0.42f};
    for (int s = 0; s < 4; ++s) {
        const float t0 = static_cast<float>(s) / 4.f;
        const float t1 = static_cast<float>(s + 1) / 4.f;
        add_cyl(&bark, seg_x[s], seg_y[s], 0.f, seg_x[s + 1], seg_y[s + 1], 0.f, 0.30f - 0.10f * t0,
                0.30f - 0.10f * t1, 9);
    }
    const float tx = seg_x[4];
    const float ty = seg_y[4];
    u32 seed = 0x91A2u;
    const int nfr = 8;
    for (int f = 0; f < nfr; ++f) {
        const float yaw = 6.2831853f * static_cast<float>(f) / static_cast<float>(nfr) + (fr01(&seed) - 0.5f) * 0.2f;
        const float len = 2.55f + 0.40f * fr01(&seed);
        const float tip_el = -(45.f + 15.f * fr01(&seed)) * 0.0174533f;
        float x = tx;
        float y = ty + 0.12f;
        float z = 0.f;
        const int nq = 5;
        for (int q = 0; q < nq; ++q) {
            const float t0 = static_cast<float>(q) / static_cast<float>(nq);
            const float t1 = static_cast<float>(q + 1) / static_cast<float>(nq);
            const float el0 = 0.f * (1.f - t0) + tip_el * t0;
            const float el1 = 0.f * (1.f - t1) + tip_el * t1;
            const float step = len / static_cast<float>(nq);
            const float x1 = x + std::cos(yaw) * std::cos(el1) * step;
            const float y1 = y + std::sin(el1) * step;
            const float z1 = z + std::sin(yaw) * std::cos(el1) * step;
            const float mx = (x + x1) * 0.5f;
            const float my = (y + y1) * 0.5f;
            const float mz = (z + z1) * 0.5f;
            add_card(&leaf, mx, my, mz, x1 - x, y1 - y, z1 - z, -std::sin(yaw), 0.12f, std::cos(yaw), 0.40f,
                     step * 1.15f);
            const float young = t1;
            tint_last(&leaf, 4, 0.55f + 0.35f * young, 0.85f + 0.15f * young, 0.35f + 0.40f * young);
            x = x1;
            y = y1;
            z = z1;
            (void)el0;
        }
    }
    emit(out, &bark, solid_tex(0xD2, 0xB4, 0x8C, 255), 1, 1, 0);
    emit(out, &leaf, leaf_card_tex(0x22, 0x8B, 0x22), 32, 32, 1);
    acc_free(&bark);
    acc_free(&leaf);
    finish(out, "palm");
    return out->nprims > 0;
}

} // namespace engine
