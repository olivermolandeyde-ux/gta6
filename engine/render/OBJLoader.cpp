#include "render/OBJLoader.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <mach-o/dyld.h>
#else
#include <SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#endif

namespace engine {
namespace {

constexpr u32 kPosCap  = 262144;
constexpr u32 kUvCap   = 262144;
constexpr u32 kNrmCap  = 262144;
constexpr u32 kVertCap = 524288;
constexpr u32 kIdxCap  = 786432;
constexpr u32 kMatCap  = 32;
constexpr u32 kTexCap  = 32;

struct ObjVert {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float cr, cg, cb, ca;
};

struct Vec3 {
    float x, y, z;
};
struct Vec2 {
    float u, v;
};

struct Mtl {
    char  name[64];
    char  map_kd[128];
    float kd[3];
    int   is_leaf;
};

struct TexSlot {
    char     file[128];
    unsigned id;
    u32      w, h;
    int      alpha;
};

int contains_ci(const char* s, const char* needle) {
    if (!s || !needle || !needle[0]) {
        return 0;
    }
    const u32 nlen = static_cast<u32>(std::strlen(needle));
    for (u32 i = 0; s[i]; ++i) {
        u32 k = 0;
        while (k < nlen) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + k])));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[k])));
            if (!s[i + k] || a != b) {
                break;
            }
            ++k;
        }
        if (k == nlen) {
            return 1;
        }
    }
    return 0;
}

int is_leaf_name(const char* s) {
    return contains_ci(s, "leaf") || contains_ci(s, "leave") || contains_ci(s, "foliage") ||
           contains_ci(s, "canopy") || contains_ci(s, "frond");
}

void dir_of(const char* path, char* dst, u32 cap) {
    dst[0] = 0;
    if (!path || cap == 0) {
        return;
    }
    u32 last = 0;
    u32 n = 0;
    while (path[n] && n + 1 < cap) {
        if (path[n] == '/' || path[n] == '\\') {
            last = n;
        }
        ++n;
    }
    const u32 len = last > 0 ? last : 0;
    if (len == 0) {
        dst[0] = '.';
        dst[1] = 0;
        return;
    }
    std::memcpy(dst, path, len);
    dst[len] = 0;
}

void join_path(char* dst, u32 cap, const char* dir, const char* file) {
    if (!file || !file[0]) {
        dst[0] = 0;
        return;
    }
    if (file[0] == '/' || (file[0] && file[1] == ':')) {
        std::snprintf(dst, cap, "%s", file);
        return;
    }
    std::snprintf(dst, cap, "%s/%s", dir && dir[0] ? dir : ".", file);
    for (u32 i = 0; dst[i]; ++i) {
        if (dst[i] == '\\') {
            dst[i] = '/';
        }
    }
}

void copy_label(char* dst, const char* path) {
    const char* base = path;
    for (const char* p = path; p && *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    const char* tag = "tree";
    if (contains_ci(base, "oak")) {
        tag = "oak";
    } else if (contains_ci(base, "pine")) {
        tag = "pine";
    } else if (contains_ci(base, "palm")) {
        tag = "palm";
    }
    u32 i = 0;
    while (tag[i] && i + 1u < 32u) {
        dst[i] = tag[i];
        ++i;
    }
    dst[i] = 0;
}

int fix_idx(int i, int n) {
    if (n <= 0) {
        return 0;
    }
    if (i > 0) {
        return i - 1;
    }
    if (i < 0) {
        return n + i;
    }
    return 0;
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

#if defined(__APPLE__)
bool imageio_file_rgba(const char* path, u8** out_rgba, u32* out_w, u32* out_h) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 8) {
        std::fclose(f);
        return false;
    }
    u8* file = static_cast<u8*>(std::malloc(static_cast<size_t>(sz)));
    if (!file) {
        std::fclose(f);
        return false;
    }
    if (std::fread(file, 1, static_cast<size_t>(sz), f) != static_cast<size_t>(sz)) {
        std::free(file);
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, file, static_cast<CFIndex>(sz));
    std::free(file);
    if (!data) {
        return false;
    }
    CGImageSourceRef isrc = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (!isrc) {
        return false;
    }
    CGImageRef img = CGImageSourceCreateImageAtIndex(isrc, 0, nullptr);
    CFRelease(isrc);
    if (!img) {
        return false;
    }
    const u32 w = static_cast<u32>(CGImageGetWidth(img));
    const u32 h = static_cast<u32>(CGImageGetHeight(img));
    if (!w || !h) {
        CGImageRelease(img);
        return false;
    }
    u8* rgba = static_cast<u8*>(std::malloc(w * h * 4u));
    if (!rgba) {
        CGImageRelease(img);
        return false;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGBitmapInfo infos[3] = {
        static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Little,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Little,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
    };
    CGContextRef ctx = nullptr;
    for (u32 i = 0; i < 3 && !ctx; ++i) {
        ctx = CGBitmapContextCreate(rgba, w, h, 8, static_cast<size_t>(w) * 4u, cs, infos[i]);
    }
    CGColorSpaceRelease(cs);
    if (!ctx) {
        std::free(rgba);
        CGImageRelease(img);
        return false;
    }
    std::memset(rgba, 0, w * h * 4u);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return true;
}
#endif

bool load_png_rgba(const char* path, u8** out_rgba, u32* out_w, u32* out_h) {
    *out_rgba = nullptr;
    *out_w = *out_h = 0;
#if defined(__APPLE__)
    if (imageio_file_rgba(path, out_rgba, out_w, out_h)) {
        return true;
    }
#endif
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        return false;
    }
    std::fclose(f);
    std::printf("[obj] PNG decode failed (need ImageIO/stb): %s\n", path);
    std::fflush(stdout);
    return false;
}

unsigned upload_png(const u8* rgba, u32 w, u32 h, int alpha_tex) {
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (alpha_tex) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    if (!alpha_tex) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    return tex;
}

int png_has_alpha(const u8* rgba, u32 w, u32 h) {
    const u32 n = w * h;
    for (u32 i = 0; i < n; ++i) {
        if (rgba[i * 4u + 3u] < 250) {
            return 1;
        }
    }
    return 0;
}

int find_tex(TexSlot* slots, u32 n, const char* file) {
    for (u32 i = 0; i < n; ++i) {
        if (std::strcmp(slots[i].file, file) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int str_eq_ci(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(*a++)));
        const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(*b++)));
        if (ca != cb) {
            return 0;
        }
    }
    return *a == 0 && *b == 0;
}

int find_mtl(Mtl* mats, u32 n, const char* name) {
    for (u32 i = 0; i < n; ++i) {
        if (str_eq_ci(mats[i].name, name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int cmd_is(const char* p, const char* cmd) {
    const u32 n = static_cast<u32>(std::strlen(cmd));
    if (std::strncmp(p, cmd, n) != 0) {
        return 0;
    }
    return p[n] == 0 || std::isspace(static_cast<unsigned char>(p[n]));
}

int file_exists(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        return 0;
    }
    std::fclose(f);
    return 1;
}

void trim_nl(char* s) {
    u32 n = static_cast<u32>(std::strlen(s));
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = 0;
    }
}

const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return p;
}

bool parse_mtl(const char* path, Mtl* mats, u32* nmat) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("[obj] WARNING: mtl missing %s\n", path);
        std::fflush(stdout);
        return false;
    }
    char line[512];
    int cur = -1;
    while (std::fgets(line, sizeof(line), f)) {
        trim_nl(line);
        const char* p = skip_ws(line);
        if (p[0] == 0 || p[0] == '#') {
            continue;
        }
        if (std::strncmp(p, "newmtl", 6) == 0 && std::isspace(static_cast<unsigned char>(p[6]))) {
            if (*nmat >= kMatCap) {
                cur = -1;
                continue;
            }
            cur = static_cast<int>(*nmat);
            Mtl& m = mats[*nmat];
            std::memset(&m, 0, sizeof(m));
            m.kd[0] = m.kd[1] = m.kd[2] = 1.f;
            std::sscanf(p + 6, " %63s", m.name);
            m.is_leaf = is_leaf_name(m.name);
            ++(*nmat);
            continue;
        }
        if (cur < 0) {
            continue;
        }
        Mtl& m = mats[static_cast<u32>(cur)];
        std::printf("[obj] mtl: %s\n", p);
        if (std::strncmp(p, "map_Kd", 6) == 0 && std::isspace(static_cast<unsigned char>(p[6]))) {
            const char* t = skip_ws(p + 6);
            const char* last = t;
            const char* q = t;
            while (*q) {
                q = skip_ws(q);
                if (*q == 0) {
                    break;
                }
                last = q;
                while (*q && !std::isspace(static_cast<unsigned char>(*q))) {
                    ++q;
                }
            }
            u32 i = 0;
            while (last[i] && !std::isspace(static_cast<unsigned char>(last[i])) && i + 1u < sizeof(m.map_kd)) {
                m.map_kd[i] = last[i];
                ++i;
            }
            m.map_kd[i] = 0;
            if (is_leaf_name(m.map_kd) || is_leaf_name(m.name)) {
                m.is_leaf = 1;
            }
            std::printf("[obj] map_Kd -> '%s' leaf=%d\n", m.map_kd, m.is_leaf);
        } else if (cmd_is(p, "Kd")) {
            std::sscanf(p + 2, " %f %f %f", &m.kd[0], &m.kd[1], &m.kd[2]);
        }
    }
    std::fclose(f);
    return true;
}

int parse_obj_int(const char** pp) {
    const char* p = *pp;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        ++p;
    }
    if (*p < '0' || *p > '9') {
        *pp = p;
        return 0;
    }
    int v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        ++p;
    }
    *pp = p;
    return sign * v;
}

void parse_face_vert(const char* tok, int* v, int* vt, int* vn) {
    *v = *vt = *vn = 0;
    const char* p = tok;
    *v = parse_obj_int(&p);
    if (*p == '/') {
        ++p;
        if (*p != '/') {
            *vt = parse_obj_int(&p);
        }
        if (*p == '/') {
            ++p;
            *vn = parse_obj_int(&p);
        }
    }
}

void bind_instance_attrs(TreeGlb* out) {
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

bool emit_bucket(TreeGlb* out, const ObjVert* verts, u32 nvert, const u32* idx, u32 nidx, unsigned tex,
                 u32 tw, u32 th, int alpha_mask, float cutoff) {
    if (out->nprims >= kTreePrimCap || nidx < 3 || nvert == 0) {
        return false;
    }
    TreePrim& pr = out->prims[out->nprims];
    std::memset(&pr, 0, sizeof(pr));
    glGenVertexArrays(1, &pr.vao);
    glGenBuffers(1, &pr.vbo);
    glGenBuffers(1, &pr.ibo);
    glBindVertexArray(pr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, pr.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(nvert * sizeof(ObjVert)), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pr.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(nidx * sizeof(u32)), idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVert), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVert),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVert),
                          reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(ObjVert),
                          reinterpret_cast<void*>(8 * sizeof(float)));
    glBindVertexArray(0);
    pr.nidx = nidx;
    pr.tex = tex;
    pr.tex_emit = solid_tex(0, 0, 0, 255);
    pr.tex_w = tw;
    pr.tex_h = th;
    pr.alpha_mask = alpha_mask;
    pr.has_alpha = alpha_mask;
    pr.cutoff = cutoff;
    pr.gl_mode = 4;
    pr.has_color0 = 0;
    out->nverts += nvert;
    out->nprims++;
    return true;
}

} // namespace

bool load_tree_obj(const char* path, TreeGlb* out) {
    std::memset(out, 0, sizeof(*out));
    out->ymin = out->xmin = out->zmin = 1.0e9f;
    out->ymax = out->xmax = out->zmax = -1.0e9f;
    copy_label(out->label, path);

    FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("[obj] missing %s\n", path);
        std::fflush(stdout);
        return false;
    }

    char dir[512];
    dir_of(path, dir, sizeof(dir));

    Vec3* pos = static_cast<Vec3*>(std::malloc(sizeof(Vec3) * kPosCap));
    Vec2* uvs = static_cast<Vec2*>(std::malloc(sizeof(Vec2) * kUvCap));
    Vec3* nrm = static_cast<Vec3*>(std::malloc(sizeof(Vec3) * kNrmCap));
    if (!pos || !uvs || !nrm) {
        std::free(pos);
        std::free(uvs);
        std::free(nrm);
        std::fclose(f);
        return false;
    }
    u32 npos = 0, nuv = 0, nnrm = 0;

    Mtl mats[kMatCap];
    u32 nmat = 0;
    std::memset(mats, 0, sizeof(mats));

    struct Corner {
        int v, vt, vn;
        int mat;
    };
    Corner* corners = static_cast<Corner*>(std::malloc(sizeof(Corner) * kIdxCap));
    if (!corners) {
        std::free(pos);
        std::free(uvs);
        std::free(nrm);
        std::fclose(f);
        return false;
    }
    u32 ncorn = 0;
    int cur_mat = 0;
    u32 nface = 0;
    u32 nobj = 0;
    char line[1024];
    char mtl_path[768];
    mtl_path[0] = 0;

    auto ensure_default_mat = [&]() {
        if (nmat > 0) {
            return;
        }
        std::memset(&mats[0], 0, sizeof(mats[0]));
        std::snprintf(mats[0].name, sizeof(mats[0].name), "default");
        mats[0].kd[0] = mats[0].kd[1] = mats[0].kd[2] = 1.f;
        nmat = 1;
        cur_mat = 0;
    };

    u32 nline = 0;
    u32 logged_face = 0;
    while (std::fgets(line, sizeof(line), f)) {
        trim_nl(line);
        char* raw = line;
        if (nline == 0 && static_cast<unsigned char>(raw[0]) == 0xef) {
            raw += 3;
        }
        ++nline;
        const char* p = skip_ws(raw);
        if (nline <= 80u) {
            std::printf("[obj] line %u: %s\n", nline, p);
        }
        if (p[0] == 0 || p[0] == '#') {
            continue;
        }
        if (cmd_is(p, "v")) {
            if (npos >= kPosCap) {
                continue;
            }
            Vec3 t{};
            if (std::sscanf(p + 1, " %f %f %f", &t.x, &t.y, &t.z) == 3) {
                pos[npos++] = t;
            }
            continue;
        }
        if (cmd_is(p, "vt")) {
            if (nuv >= kUvCap) {
                continue;
            }
            Vec2 t{};
            if (std::sscanf(p + 2, " %f %f", &t.u, &t.v) >= 1) {
                uvs[nuv++] = t;
            }
            continue;
        }
        if (cmd_is(p, "vn")) {
            if (nnrm >= kNrmCap) {
                continue;
            }
            Vec3 t{};
            if (std::sscanf(p + 2, " %f %f %f", &t.x, &t.y, &t.z) == 3) {
                nrm[nnrm++] = t;
            }
            continue;
        }
        if (cmd_is(p, "o") || cmd_is(p, "g")) {
            ++nobj;
            std::printf("[obj] object/group: %s\n", skip_ws(p + 1));
            continue;
        }
        if (cmd_is(p, "mtllib")) {
            char name[128];
            name[0] = 0;
            std::sscanf(p + 6, " %127s", name);
            join_path(mtl_path, sizeof(mtl_path), dir, name);
            std::printf("[obj] mtllib '%s' -> %s EXISTS=%s\n", name, mtl_path,
                        file_exists(mtl_path) ? "YES" : "NO");
            parse_mtl(mtl_path, mats, &nmat);
            continue;
        }
        if (cmd_is(p, "usemtl")) {
            char name[64];
            name[0] = 0;
            std::sscanf(p + 6, " %63s", name);
            ensure_default_mat();
            const int found = find_mtl(mats, nmat, name);
            if (found >= 0) {
                cur_mat = found;
            } else if (nmat < kMatCap) {
                Mtl& m = mats[nmat];
                std::memset(&m, 0, sizeof(m));
                std::snprintf(m.name, sizeof(m.name), "%s", name);
                m.kd[0] = m.kd[1] = m.kd[2] = 1.f;
                m.is_leaf = is_leaf_name(name);
                cur_mat = static_cast<int>(nmat);
                ++nmat;
            }
            std::printf("[obj] usemtl '%s' -> mat %d leaf=%d\n", name, cur_mat,
                        (cur_mat >= 0 && static_cast<u32>(cur_mat) < nmat) ? mats[cur_mat].is_leaf : 0);
            continue;
        }
        if (cmd_is(p, "f")) {
            ensure_default_mat();
            int fv[64], ft[64], fn[64];
            u32 nv = 0;
            const char* q = p + 1;
            while (*q && nv < 64) {
                q = skip_ws(q);
                if (*q == 0) {
                    break;
                }
                char tok[64];
                u32 ti = 0;
                while (*q && *q != ' ' && *q != '\t' && ti + 1 < sizeof(tok)) {
                    tok[ti++] = *q++;
                }
                tok[ti] = 0;
                parse_face_vert(tok, &fv[nv], &ft[nv], &fn[nv]);
                ++nv;
            }
            if (nv < 3) {
                continue;
            }
            for (u32 t = 1; t + 1 < nv; ++t) {
                const int ids[3] = {0, static_cast<int>(t), static_cast<int>(t + 1)};
                if (ncorn + 3 > kIdxCap) {
                    break;
                }
                for (u32 k = 0; k < 3; ++k) {
                    Corner c{};
                    c.v = fv[ids[k]];
                    c.vt = ft[ids[k]];
                    c.vn = fn[ids[k]];
                    c.mat = cur_mat;
                    corners[ncorn++] = c;
                }
                ++nface;
                if (logged_face < 5u) {
                    const int i0 = fix_idx(fv[0], static_cast<int>(npos));
                    const int i1 = fix_idx(fv[ids[1]], static_cast<int>(npos));
                    const int i2 = fix_idx(fv[ids[2]], static_cast<int>(npos));
                    std::printf("[obj] face[%u]=(%d,%d,%d) raw=(%d,%d,%d) uv=(%d,%d,%d) mat=%d\n", logged_face, i0,
                                i1, i2, fv[0], fv[ids[1]], fv[ids[2]], ft[0], ft[ids[1]], ft[ids[2]], cur_mat);
                    ++logged_face;
                }
            }
        }
    }
    std::fclose(f);
    std::printf("[obj] parse done lines=%u v=%u vt=%u vn=%u faces=%u mats=%u corners=%u\n", nline, npos, nuv,
                nnrm, nface, nmat, ncorn);
    {
        const u32 show = nuv < 5 ? nuv : 5;
        for (u32 i = 0; i < show; ++i) {
            std::printf("[obj] UV[%u]=(%.4f,%.4f)\n", i, uvs[i].u, uvs[i].v);
        }
        if (nuv == 0) {
            std::printf("[obj] WARNING: no vt lines — UVs will be 0,0\n");
        }
        const u32 showv = npos < 3 ? npos : 3;
        for (u32 i = 0; i < showv; ++i) {
            std::printf("[obj] v[%u]=(%.4f,%.4f,%.4f)\n", i, pos[i].x, pos[i].y, pos[i].z);
        }
        std::fflush(stdout);
    }

    TexSlot texs[kTexCap];
    u32 ntex = 0;
    std::memset(texs, 0, sizeof(texs));
    for (u32 mi = 0; mi < nmat; ++mi) {
        Mtl& m = mats[mi];
        int loaded = 0;
        if (m.map_kd[0]) {
            char texpath[768];
            join_path(texpath, sizeof(texpath), dir, m.map_kd);
            int slot = find_tex(texs, ntex, m.map_kd);
            if (slot < 0 && ntex < kTexCap) {
                u8* rgba = nullptr;
                u32 w = 0, h = 0;
                std::printf("[obj] Checking file: %s - EXISTS: %s\n", texpath,
                            file_exists(texpath) ? "YES" : "NO");
                if (load_png_rgba(texpath, &rgba, &w, &h) && rgba) {
                    const int ha = png_has_alpha(rgba, w, h) || m.is_leaf;
                    texs[ntex].id = upload_png(rgba, w, h, ha || m.is_leaf);
                    texs[ntex].w = w;
                    texs[ntex].h = h;
                    texs[ntex].alpha = ha;
                    std::snprintf(texs[ntex].file, sizeof(texs[ntex].file), "%s", m.map_kd);
                    u8 amin = 255, amax = 0;
                    u32 cut = 0;
                    const u32 npx = w * h;
                    for (u32 pi = 0; pi < npx; ++pi) {
                        const u8 a = rgba[pi * 4u + 3u];
                        if (a < amin) {
                            amin = a;
                        }
                        if (a > amax) {
                            amax = a;
                        }
                        if (a < 128) {
                            ++cut;
                        }
                    }
                    std::printf("[obj] Loading texture: %s - size: %ux%u, channels: 4 id=%u alpha=%s "
                                "a=[%u..%u] cutout=%u/%u\n",
                                m.map_kd, w, h, texs[ntex].id, ha ? "YES" : "NO", amin, amax, cut, npx);
                    std::free(rgba);
                    loaded = 1;
                    slot = static_cast<int>(ntex);
                    ++ntex;
                } else {
                    std::printf("[obj] WARNING: Failed to load texture %s\n", texpath);
                    std::fflush(stdout);
                }
            } else if (slot >= 0) {
                loaded = 1;
            }
            std::printf("[obj] Material '%s': texture=%s, loaded=%s%s\n", m.name, m.map_kd,
                        loaded ? "YES" : "NO", m.is_leaf ? ", alpha=YES" : "");
        } else {
            std::printf("[obj] Material '%s': texture=(none Kd %.2f %.2f %.2f)%s\n", m.name, m.kd[0], m.kd[1],
                        m.kd[2], m.is_leaf ? ", alpha=YES" : "");
        }
        std::fflush(stdout);
    }

    for (u32 mi = 0; mi < nmat; ++mi) {
        ObjVert* verts = static_cast<ObjVert*>(std::malloc(sizeof(ObjVert) * kVertCap));
        u32* idx = static_cast<u32*>(std::malloc(sizeof(u32) * kIdxCap));
        if (!verts || !idx) {
            std::free(verts);
            std::free(idx);
            break;
        }
        u32 nv = 0, ni = 0;
        for (u32 c = 0; c + 2 < ncorn && ni + 3 <= kIdxCap && nv + 3 <= kVertCap; c += 3) {
            if (corners[c].mat != static_cast<int>(mi)) {
                continue;
            }
            for (u32 k = 0; k < 3; ++k) {
                const Corner& cr = corners[c + k];
                const int vi = fix_idx(cr.v, static_cast<int>(npos));
                const int ti = cr.vt ? fix_idx(cr.vt, static_cast<int>(nuv)) : -1;
                const int ni_ = cr.vn ? fix_idx(cr.vn, static_cast<int>(nnrm)) : -1;
                ObjVert ov{};
                if (vi >= 0 && static_cast<u32>(vi) < npos) {
                    ov.px = pos[vi].x;
                    ov.py = pos[vi].y;
                    ov.pz = pos[vi].z;
                }
                if (ni_ >= 0 && static_cast<u32>(ni_) < nnrm) {
                    ov.nx = nrm[ni_].x;
                    ov.ny = nrm[ni_].y;
                    ov.nz = nrm[ni_].z;
                } else {
                    ov.ny = 1.f;
                }
                if (ti >= 0 && static_cast<u32>(ti) < nuv) {
                    ov.u = uvs[ti].u;
                    ov.v = uvs[ti].v;
                }
                ov.cr = ov.cg = ov.cb = ov.ca = 1.f;
                if (ov.px < out->xmin) {
                    out->xmin = ov.px;
                }
                if (ov.px > out->xmax) {
                    out->xmax = ov.px;
                }
                if (ov.py < out->ymin) {
                    out->ymin = ov.py;
                }
                if (ov.py > out->ymax) {
                    out->ymax = ov.py;
                }
                if (ov.pz < out->zmin) {
                    out->zmin = ov.pz;
                }
                if (ov.pz > out->zmax) {
                    out->zmax = ov.pz;
                }
                verts[nv] = ov;
                idx[ni++] = nv;
                ++nv;
            }
        }
        if (ni >= 3) {
            const Mtl& m = mats[mi];
            unsigned tex = 0;
            u32 tw = 1, th = 1;
            int amask = m.is_leaf ? 1 : 0;
            if (m.map_kd[0]) {
                const int slot = find_tex(texs, ntex, m.map_kd);
                if (slot >= 0) {
                    tex = texs[slot].id;
                    tw = texs[slot].w;
                    th = texs[slot].h;
                    if (texs[slot].alpha) {
                        amask = 1;
                    }
                }
            }
            if (!tex) {
                tex = m.is_leaf ? solid_tex(0x22, 0x8B, 0x22, 255) : solid_tex(0x8B, 0x45, 0x13, 255);
                tw = th = 1;
            }
            std::printf("[obj] Applying material '%s' with texture ID: %u tris=%u leaf=%d mask=%d %ux%u\n",
                        m.name, tex, ni / 3u, m.is_leaf, amask, tw, th);
            emit_bucket(out, verts, nv, idx, ni, tex, tw, th, amask, 0.5f);
        }
        std::free(verts);
        std::free(idx);
    }

    std::free(corners);
    std::free(pos);
    std::free(uvs);
    std::free(nrm);

    if (out->ymax < out->ymin) {
        out->ymin = 0.f;
        out->ymax = 1.f;
    }
    out->z_up = 0;
    if (out->nprims > 0) {
        bind_instance_attrs(out);
    }
    std::printf("[obj] Loaded %s: %u vertices, %u faces, %u materials\n", path, npos, nface, nmat);
    std::printf("[obj] %s AABB min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) Y-up (height=%.3f) prims=%u gpu_verts=%u\n",
                out->label, out->xmin, out->ymin, out->zmin, out->xmax, out->ymax, out->zmax,
                out->ymax - out->ymin, out->nprims, out->nverts);
    std::printf("[obj] textures on, leaf discard a<0.5, bark opaque\n");
    std::fflush(stdout);
    return out->nprims > 0;
}

void log_tree_obj_files() {
    const char* names[] = {"oak_tree.obj",
                           "oak_tree.mtl",
                           "oak_tree_bark.png",
                           "oak_tree_leaves.png",
                           "pine_tree.obj",
                           "pine_tree.mtl",
                           "pine_tree_bark.png",
                           "pine_tree_leaves.png",
                           "palm_tree.obj",
                           "palm_tree.mtl",
                           "palm_tree_bark.png",
                           "palm_tree_leaves.png"};
    char cwd[512];
    if (!getcwd(cwd, sizeof(cwd))) {
        cwd[0] = '.';
        cwd[1] = 0;
    }
    for (u32 i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        char p0[768], p1[768], p2[768];
        std::snprintf(p0, sizeof(p0), "%s/assets/models/%s", LEONIDA_SOURCE_DIR, names[i]);
        std::snprintf(p1, sizeof(p1), "%s/assets/models/%s", cwd, names[i]);
        std::snprintf(p2, sizeof(p2), "%s/../assets/models/%s", cwd, names[i]);
        const int ok = file_exists(p0) || file_exists(p1) || file_exists(p2);
        const char* hit = file_exists(p0) ? p0 : (file_exists(p1) ? p1 : (file_exists(p2) ? p2 : p0));
        std::printf("[obj] Checking file: assets/models/%s - EXISTS: %s (%s)\n", names[i], ok ? "YES" : "NO",
                    hit);
    }
    std::fflush(stdout);
}

bool find_and_load_tree_obj(const char* filename, TreeGlb* out) {
    char cwd[512];
    cwd[0] = 0;
    if (!getcwd(cwd, sizeof(cwd))) {
        cwd[0] = '.';
        cwd[1] = 0;
    }
    char exe_dir[512];
    exe_dir[0] = 0;
#if defined(__APPLE__)
    char exe[512];
    u32 esz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &esz) == 0) {
        dir_of(exe, exe_dir, sizeof(exe_dir));
    }
#endif
    char buf[12][768];
    u32 n = 0;
    auto add = [&](const char* fmt, const char* a) {
        if (n >= 12) {
            return;
        }
        std::snprintf(buf[n], sizeof(buf[n]), fmt, a, filename);
        ++n;
    };
    add("%s/assets/models/%s", LEONIDA_SOURCE_DIR);
    add("%s/assets/models/%s", cwd);
    add("%s/../assets/models/%s", cwd);
    add("%s/assets/models/%s", ".");
    add("%s/assets/models/%s", "..");
    if (exe_dir[0]) {
        add("%s/assets/models/%s", exe_dir);
        add("%s/../assets/models/%s", exe_dir);
        add("%s/../../assets/models/%s", exe_dir);
    }
    add("%s/build/assets/models/%s", LEONIDA_SOURCE_DIR);
    for (u32 i = 0; i < n; ++i) {
        FILE* f = std::fopen(buf[i], "rb");
        if (!f) {
            continue;
        }
        std::fclose(f);
        std::printf("[obj] found %s\n", buf[i]);
        std::fflush(stdout);
        return load_tree_obj(buf[i], out);
    }
    std::printf("[obj] not found: %s (drop oak_tree.obj + mtl + png in assets/models/)\n", filename);
    std::fflush(stdout);
    return false;
}

} // namespace engine
