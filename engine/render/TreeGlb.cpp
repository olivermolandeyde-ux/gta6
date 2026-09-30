#include "render/TreeGlb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
#include <unistd.h>

namespace engine {
namespace {

struct BitReader {
    const u8* p;
    const u8* end;
    u32       acc;
    int       bits;
};

bool br_need(BitReader* b, int n) {
    while (b->bits < n) {
        if (b->p >= b->end) {
            return false;
        }
        b->acc |= static_cast<u32>(*b->p++) << b->bits;
        b->bits += 8;
    }
    return true;
}

u32 br_get(BitReader* b, int n) {
    if (!br_need(b, n)) {
        return 0;
    }
    const u32 r = b->acc & ((1u << n) - 1u);
    b->acc >>= n;
    b->bits -= n;
    return r;
}

void br_align(BitReader* b) {
    b->acc = 0;
    b->bits = 0;
}

struct Huff {
    u16 counts[16];
    u16 first[16];
    u16 symbols[320];
    u8  lengths[320];
    u32 nsym;
};

void huff_build(Huff* h, const u8* lengths, u32 nsym) {
    std::memset(h, 0, sizeof(*h));
    h->nsym = nsym;
    for (u32 i = 0; i < nsym; ++i) {
        h->lengths[i] = lengths[i];
        if (lengths[i] <= 15) {
            h->counts[lengths[i]]++;
        }
    }
    h->counts[0] = 0;
    u16 code = 0;
    for (int len = 1; len <= 15; ++len) {
        code = static_cast<u16>((code + h->counts[len - 1]) << 1);
        h->first[len] = code;
    }
    u16 next[16];
    for (int len = 0; len <= 15; ++len) {
        next[len] = 0;
    }
    u16 offs[16];
    offs[0] = 0;
    u16 sum = 0;
    for (int len = 1; len <= 15; ++len) {
        offs[len] = sum;
        sum = static_cast<u16>(sum + h->counts[len]);
    }
    for (u32 i = 0; i < nsym; ++i) {
        const u8 l = lengths[i];
        if (l == 0) {
            continue;
        }
        h->symbols[offs[l] + next[l]] = static_cast<u16>(i);
        next[l]++;
    }
}

int huff_decode(BitReader* b, const Huff* h) {
    u32 code = 0;
    u16 base = 0;
    for (int len = 1; len <= 15; ++len) {
        code = (code << 1) | br_get(b, 1);
        const u16 cnt = h->counts[len];
        const u16 first = h->first[len];
        if (cnt != 0 && code >= first && code < static_cast<u32>(first) + cnt) {
            return h->symbols[base + static_cast<u16>(code - first)];
        }
        base = static_cast<u16>(base + cnt);
    }
    return -1;
}

constexpr u16 kLenBase[29] = {3,  4,  5,  6,  7,  8,   9,   10,  11, 13, 15, 17, 19, 23, 27,
                              31, 35, 43, 51, 59, 67,  83,  99,  115, 131, 163, 195, 227, 258};
constexpr u8  kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr u16 kDistBase[32] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,   33,
                               49,   65,   97,   129,  193,  257,   385,   513,   769,  1025, 1537,
                               2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0,    0};
constexpr u8 kDistExtra[32] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5, 6, 6,
                               7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0};
constexpr u8 kClcOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

bool inflate_block(BitReader* b, u8* dst, u32 cap, u32* out_n, const Huff* lit, const Huff* dist) {
    for (;;) {
        const int s = huff_decode(b, lit);
        if (s < 0) {
            return false;
        }
        if (s < 256) {
            if (*out_n >= cap) {
                return false;
            }
            dst[(*out_n)++] = static_cast<u8>(s);
            continue;
        }
        if (s == 256) {
            return true;
        }
        const int len_i = s - 257;
        if (len_i < 0 || len_i > 28) {
            return false;
        }
        u32 length = kLenBase[len_i] + br_get(b, kLenExtra[len_i]);
        const int dsym = huff_decode(b, dist);
        if (dsym < 0 || dsym > 31) {
            return false;
        }
        u32 distv = kDistBase[dsym] + br_get(b, kDistExtra[dsym]);
        if (distv == 0 || distv > *out_n) {
            return false;
        }
        for (u32 k = 0; k < length; ++k) {
            if (*out_n >= cap) {
                return false;
            }
            dst[*out_n] = dst[*out_n - distv];
            ++(*out_n);
        }
    }
}

bool zlib_inflate(const u8* src, u32 slen, u8* dst, u32 cap, u32* out_n) {
    if (slen < 6) {
        return false;
    }
    const u8 cmf = src[0];
    const u8 flg = src[1];
    if ((cmf & 0x0f) != 8) {
        return false;
    }
    if (((static_cast<u32>(cmf) << 8) + flg) % 31u != 0) {
        return false;
    }
    if (flg & 0x20) {
        return false;
    }
    BitReader b{src + 2, src + slen - 4, 0, 0};
    *out_n = 0;
    Huff lit{};
    Huff dist{};
    int bfinal = 0;
    while (!bfinal) {
        bfinal = static_cast<int>(br_get(&b, 1));
        const int btype = static_cast<int>(br_get(&b, 2));
        if (btype == 0) {
            br_align(&b);
            if (b.p + 4 > b.end) {
                return false;
            }
            const u32 len = static_cast<u32>(b.p[0] | (b.p[1] << 8));
            const u32 nlen = static_cast<u32>(b.p[2] | (b.p[3] << 8));
            b.p += 4;
            if ((len ^ 0xffffu) != nlen || b.p + len > b.end || *out_n + len > cap) {
                return false;
            }
            std::memcpy(dst + *out_n, b.p, len);
            *out_n += len;
            b.p += len;
        } else if (btype == 1) {
            u8 ll[288];
            u8 dd[32];
            for (int i = 0; i <= 143; ++i) {
                ll[i] = 8;
            }
            for (int i = 144; i <= 255; ++i) {
                ll[i] = 9;
            }
            for (int i = 256; i <= 279; ++i) {
                ll[i] = 7;
            }
            for (int i = 280; i <= 287; ++i) {
                ll[i] = 8;
            }
            for (int i = 0; i < 32; ++i) {
                dd[i] = 5;
            }
            huff_build(&lit, ll, 288);
            huff_build(&dist, dd, 32);
            if (!inflate_block(&b, dst, cap, out_n, &lit, &dist)) {
                return false;
            }
        } else if (btype == 2) {
            const u32 hlit = br_get(&b, 5) + 257;
            const u32 hdist = br_get(&b, 5) + 1;
            const u32 hclen = br_get(&b, 4) + 4;
            u8 clen[19];
            std::memset(clen, 0, sizeof(clen));
            for (u32 i = 0; i < hclen; ++i) {
                clen[kClcOrder[i]] = static_cast<u8>(br_get(&b, 3));
            }
            Huff clc{};
            huff_build(&clc, clen, 19);
            u8 lengths[320];
            std::memset(lengths, 0, sizeof(lengths));
            u32 n = 0;
            const u32 total = hlit + hdist;
            while (n < total) {
                const int s = huff_decode(&b, &clc);
                if (s < 0) {
                    return false;
                }
                if (s < 16) {
                    lengths[n++] = static_cast<u8>(s);
                } else if (s == 16) {
                    const u32 r = br_get(&b, 2) + 3;
                    if (n == 0) {
                        return false;
                    }
                    const u8 prev = lengths[n - 1];
                    for (u32 k = 0; k < r; ++k) {
                        lengths[n++] = prev;
                    }
                } else if (s == 17) {
                    const u32 r = br_get(&b, 3) + 3;
                    n += r;
                } else {
                    const u32 r = br_get(&b, 7) + 11;
                    n += r;
                }
            }
            huff_build(&lit, lengths, hlit);
            huff_build(&dist, lengths + hlit, hdist);
            if (!inflate_block(&b, dst, cap, out_n, &lit, &dist)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

bool png_decode_rgba(const u8* src, u32 slen, u8** out_rgba, u32* out_w, u32* out_h) {
    static const u8 kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (slen < 33 || std::memcmp(src, kSig, 8) != 0) {
        return false;
    }
    u32 w = 0, h = 0, depth = 0, color = 0;
    const u8* idat = nullptr;
    u32 idat_cap = 0;
    u8* idat_buf = nullptr;
    u32 idat_n = 0;
    u32 off = 8;
    while (off + 12 <= slen) {
        const u32 clen = (src[off] << 24) | (src[off + 1] << 16) | (src[off + 2] << 8) | src[off + 3];
        const char* tag = reinterpret_cast<const char*>(src + off + 4);
        const u8* data = src + off + 8;
        if (off + 12 + clen > slen) {
            std::free(idat_buf);
            return false;
        }
        if (std::memcmp(tag, "IHDR", 4) == 0 && clen >= 13) {
            w = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
            h = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
            depth = data[8];
            color = data[9];
        } else if (std::memcmp(tag, "IDAT", 4) == 0) {
            if (idat_n + clen > idat_cap) {
                idat_cap = idat_n + clen + 4096;
                u8* nb = static_cast<u8*>(std::realloc(idat_buf, idat_cap));
                if (!nb) {
                    std::free(idat_buf);
                    return false;
                }
                idat_buf = nb;
            }
            std::memcpy(idat_buf + idat_n, data, clen);
            idat_n += clen;
            idat = idat_buf;
        } else if (std::memcmp(tag, "IEND", 4) == 0) {
            break;
        }
        off += 12 + clen;
    }
    if (!w || !h || depth != 8 || (color > 6) || color == 1 || color == 3 || color == 5 || !idat) {
        std::free(idat_buf);
        return false;
    }
    const u32 bpp = (color == 6) ? 4u : (color == 4) ? 2u : (color == 2) ? 3u : 1u;
    const u32 raw_cap = (w * bpp + 1) * h;
    u8* raw = static_cast<u8*>(std::malloc(raw_cap + 64));
    if (!raw) {
        std::free(idat_buf);
        return false;
    }
    u32 raw_n = 0;
    if (!zlib_inflate(idat, idat_n, raw, raw_cap, &raw_n) || raw_n < raw_cap) {
        std::free(raw);
        std::free(idat_buf);
        return false;
    }
    std::free(idat_buf);
    u8* rgba = static_cast<u8*>(std::malloc(w * h * 4));
    if (!rgba) {
        std::free(raw);
        return false;
    }
    const u32 stride = w * bpp;
    u8* prev = static_cast<u8*>(std::calloc(stride, 1));
    if (!prev) {
        std::free(raw);
        std::free(rgba);
        return false;
    }
    u8* cur = static_cast<u8*>(std::malloc(stride));
    if (!cur) {
        std::free(prev);
        std::free(raw);
        std::free(rgba);
        return false;
    }
    u32 rp = 0;
    for (u32 y = 0; y < h; ++y) {
        const u8 filter = raw[rp++];
        for (u32 x = 0; x < stride; ++x) {
            const u8 f = raw[rp++];
            const u8 a = x >= bpp ? cur[x - bpp] : 0;
            const u8 b = prev[x];
            const u8 c = x >= bpp ? prev[x - bpp] : 0;
            u8 v = f;
            if (filter == 1) {
                v = static_cast<u8>(f + a);
            } else if (filter == 2) {
                v = static_cast<u8>(f + b);
            } else if (filter == 3) {
                v = static_cast<u8>(f + ((a + b) / 2));
            } else if (filter == 4) {
                v = static_cast<u8>(f + paeth(a, b, c));
            }
            cur[x] = v;
        }
        for (u32 x = 0; x < w; ++x) {
            const u8* px = cur + x * bpp;
            u8* o = rgba + (y * w + x) * 4;
            if (color == 0) {
                o[0] = o[1] = o[2] = px[0];
                o[3] = 255;
            } else if (color == 4) {
                o[0] = o[1] = o[2] = px[0];
                o[3] = px[1];
            } else {
                o[0] = px[0];
                o[1] = px[1];
                o[2] = px[2];
                o[3] = bpp == 4 ? px[3] : 255;
            }
        }
        std::memcpy(prev, cur, stride);
    }
    std::free(cur);
    std::free(prev);
    std::free(raw);
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return true;
}

#if defined(__APPLE__)
bool imageio_decode_rgba(const u8* src, u32 slen, u8** out_rgba, u32* out_w, u32* out_h, int tree_rgba) {
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, src, static_cast<CFIndex>(slen));
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
    u8* rgba = static_cast<u8*>(std::malloc(w * h * 4));
    if (!rgba) {
        CGImageRelease(img);
        return false;
    }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGBitmapInfo kTree[4] = {
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big,
        static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Big,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
        static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Little,
    };
    const CGBitmapInfo kNorm[3] = {
        static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Little,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Little,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast),
    };
    const CGBitmapInfo* kInfos = tree_rgba ? kTree : kNorm;
    const u32 ninfo            = tree_rgba ? 4u : 3u;
    CGContextRef ctx           = nullptr;
    u32 used                   = 0;
    for (u32 i = 0; i < ninfo && !ctx; ++i) {
        ctx  = CGBitmapContextCreate(rgba, w, h, 8, static_cast<size_t>(w) * 4, cs, kInfos[i]);
        used = i;
    }
    CGColorSpaceRelease(cs);
    if (!ctx) {
        std::printf("[glb] ImageIO bitmap context failed %ux%u\n", w, h);
        std::fflush(stdout);
        std::free(rgba);
        CGImageRelease(img);
        return false;
    }
    std::memset(rgba, 0, w * h * 4);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);
    {
        const u32 stride = w * 4;
        u8* row = static_cast<u8*>(std::malloc(stride));
        if (row) {
            for (u32 y = 0; y < h / 2; ++y) {
                u8* a = rgba + y * stride;
                u8* b = rgba + (h - 1 - y) * stride;
                std::memcpy(row, a, stride);
                std::memcpy(a, b, stride);
                std::memcpy(b, row, stride);
            }
            std::free(row);
        }
    }
    if (tree_rgba && used == 3u) {
        const u32 n = w * h;
        for (u32 i = 0; i < n; ++i) {
            const u8 t = rgba[i * 4 + 0];
            rgba[i * 4 + 0] = rgba[i * 4 + 2];
            rgba[i * 4 + 2] = t;
        }
    }
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return true;
}
#endif

bool decode_image_rgba(const u8* src, u32 slen, u8** out_rgba, u32* out_w, u32* out_h) {
#if defined(__APPLE__)
    if (imageio_decode_rgba(src, slen, out_rgba, out_w, out_h, 0)) {
        return true;
    }
#endif
    return png_decode_rgba(src, slen, out_rgba, out_w, out_h);
}

bool decode_image_rgba_tree(const u8* src, u32 slen, u8** out_rgba, u32* out_w, u32* out_h) {
#if defined(__APPLE__)
    if (imageio_decode_rgba(src, slen, out_rgba, out_w, out_h, 1)) {
        return true;
    }
#endif
    return png_decode_rgba(src, slen, out_rgba, out_w, out_h);
}

// --- tiny JSON ---
enum JKind : u8 { JK_NULL, JK_BOOL, JK_NUM, JK_STR, JK_ARR, JK_OBJ };

struct JNode {
    JKind       kind;
    double      num;
    const char* s;
    u32         slen;
    u32         child;
    u32         next;
    const char* key;
    u32         klen;
};

struct JDoc {
    JNode* nodes;
    u32    cap;
    u32    n;
    u32    root;
};

const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        ++p;
    }
    return p;
}

bool j_parse_value(JDoc* d, const char** pp, u32* out) {
    const char* p = skip_ws(*pp);
    if (d->n >= d->cap) {
        return false;
    }
    const u32 id = d->n++;
    JNode& n = d->nodes[id];
    std::memset(&n, 0, sizeof(n));
    if (*p == '{') {
        n.kind = JK_OBJ;
        ++p;
        u32* tail = &n.child;
        p = skip_ws(p);
        if (*p != '}') {
            for (;;) {
                p = skip_ws(p);
                if (*p != '"') {
                    return false;
                }
                ++p;
                const char* ks = p;
                while (*p && *p != '"') {
                    if (*p == '\\') {
                        p += 2;
                    } else {
                        ++p;
                    }
                }
                const u32 klen = static_cast<u32>(p - ks);
                if (*p != '"') {
                    return false;
                }
                ++p;
                p = skip_ws(p);
                if (*p != ':') {
                    return false;
                }
                ++p;
                u32 val = 0;
                if (!j_parse_value(d, &p, &val)) {
                    return false;
                }
                d->nodes[val].key = ks;
                d->nodes[val].klen = klen;
                *tail = val;
                tail = &d->nodes[val].next;
                p = skip_ws(p);
                if (*p == ',') {
                    ++p;
                    continue;
                }
                break;
            }
        }
        if (*p != '}') {
            return false;
        }
        ++p;
        *out = id;
        *pp = p;
        return true;
    }
    if (*p == '[') {
        n.kind = JK_ARR;
        ++p;
        u32* tail = &n.child;
        p = skip_ws(p);
        if (*p != ']') {
            for (;;) {
                u32 val = 0;
                if (!j_parse_value(d, &p, &val)) {
                    return false;
                }
                *tail = val;
                tail = &d->nodes[val].next;
                p = skip_ws(p);
                if (*p == ',') {
                    ++p;
                    continue;
                }
                break;
            }
        }
        if (*p != ']') {
            return false;
        }
        ++p;
        *out = id;
        *pp = p;
        return true;
    }
    if (*p == '"') {
        n.kind = JK_STR;
        ++p;
        n.s = p;
        while (*p && *p != '"') {
            if (*p == '\\') {
                p += 2;
            } else {
                ++p;
            }
        }
        n.slen = static_cast<u32>(p - n.s);
        if (*p != '"') {
            return false;
        }
        ++p;
        *out = id;
        *pp = p;
        return true;
    }
    if (std::strncmp(p, "true", 4) == 0) {
        n.kind = JK_BOOL;
        n.num = 1;
        *out = id;
        *pp = p + 4;
        return true;
    }
    if (std::strncmp(p, "false", 5) == 0) {
        n.kind = JK_BOOL;
        n.num = 0;
        *out = id;
        *pp = p + 5;
        return true;
    }
    if (std::strncmp(p, "null", 4) == 0) {
        n.kind = JK_NULL;
        *out = id;
        *pp = p + 4;
        return true;
    }
    char* end = nullptr;
    n.kind = JK_NUM;
    n.num = std::strtod(p, &end);
    if (end == p) {
        return false;
    }
    *out = id;
    *pp = end;
    return true;
}

bool key_eq(const JNode& n, const char* k) {
    const u32 l = static_cast<u32>(std::strlen(k));
    return n.klen == l && std::memcmp(n.key, k, l) == 0;
}

const JNode* j_field(const JDoc* d, u32 obj, const char* k) {
    if (d->nodes[obj].kind != JK_OBJ) {
        return nullptr;
    }
    for (u32 c = d->nodes[obj].child; c != 0; c = d->nodes[c].next) {
        if (key_eq(d->nodes[c], k)) {
            return &d->nodes[c];
        }
    }
    return nullptr;
}

double j_num(const JDoc* d, u32 obj, const char* k, double def = 0) {
    const JNode* n = j_field(d, obj, k);
    return n && n->kind == JK_NUM ? n->num : def;
}

u32 j_idx(const JDoc* d, u32 obj, const char* k) {
    return static_cast<u32>(j_num(d, obj, k, 0));
}

bool str_eq(const JNode* n, const char* s) {
    if (!n || n->kind != JK_STR) {
        return false;
    }
    const u32 l = static_cast<u32>(std::strlen(s));
    return n->slen == l && std::memcmp(n->s, s, l) == 0;
}

u32 j_arr_at(const JDoc* d, const JNode* arr, u32 i) {
    if (!arr || arr->kind != JK_ARR) {
        return 0;
    }
    u32 c = arr->child;
    while (c != 0 && i) {
        c = d->nodes[c].next;
        --i;
    }
    return c;
}

u32 g_last_tex_w     = 1;
u32 g_last_tex_h     = 1;
int g_last_tex_alpha = 0;

float g_last_mean_r = 0.f;
float g_last_mean_g = 0.f;
float g_last_mean_b = 0.f;

unsigned upload_rgba(const u8* rgba, u32 w, u32 h) {
    int has_alpha = 0;
    const u32 n = w * h;
    u64 sr = 0, sg = 0, sb = 0, sc = 0;
    for (u32 i = 0; i < n; ++i) {
        const u8 a = rgba[i * 4 + 3];
        if (a < 250) {
            has_alpha = 1;
        }
        if (a > 80) {
            sr += rgba[i * 4 + 0];
            sg += rgba[i * 4 + 1];
            sb += rgba[i * 4 + 2];
            ++sc;
        }
    }
    g_last_mean_r    = sc ? static_cast<float>(sr / sc) : 0.f;
    g_last_mean_g    = sc ? static_cast<float>(sg / sc) : 0.f;
    g_last_mean_b    = sc ? static_cast<float>(sb / sc) : 0.f;
    g_last_tex_w     = w;
    g_last_tex_h     = h;
    g_last_tex_alpha = has_alpha;
    std::printf("[glb] texture mean RGB=(%.0f,%.0f,%.0f) first pixels:", g_last_mean_r, g_last_mean_g,
                g_last_mean_b);
    const u32 show = n < 10 ? n : 10;
    for (u32 i = 0; i < show; ++i) {
        std::printf(" (%u,%u,%u,%u)", rgba[i * 4 + 0], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]);
    }
    std::printf("\n");
    std::fflush(stdout);
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (has_alpha) {
        // Mipmaps average leaf alpha below the cutoff and the canopy vanishes.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(w), static_cast<int>(h), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    if (!has_alpha) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    std::printf("[glb] Tree texture size: %ux%u, has alpha channel: %s (id=%u)\n", w, h,
                has_alpha ? "yes" : "no", tex);
    std::fflush(stdout);
    return tex;
}

struct TreeVert {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float cr, cg, cb, ca;
};

void mul_mat_vec3(const float* m, float x, float y, float z, float* ox, float* oy, float* oz) {
    *ox = m[0] * x + m[4] * y + m[8] * z + m[12];
    *oy = m[1] * x + m[5] * y + m[9] * z + m[13];
    *oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

void mul_mat_dir(const float* m, float x, float y, float z, float* ox, float* oy, float* oz) {
    *ox = m[0] * x + m[4] * y + m[8] * z;
    *oy = m[1] * x + m[5] * y + m[9] * z;
    *oz = m[2] * x + m[6] * y + m[10] * z;
}

void mat_ident(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.f;
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

void node_local(const JDoc* d, u32 node, float* m) {
    const JNode* mat = j_field(d, node, "matrix");
    if (mat && mat->kind == JK_ARR) {
        mat_ident(m);
        u32 i = 0;
        for (u32 c = mat->child; c != 0 && i < 16; c = d->nodes[c].next, ++i) {
            m[i] = static_cast<float>(d->nodes[c].num);
        }
        return;
    }
    // glTF TRS is T * R * S. Applying S then R on a translation matrix
    // scattered leaf cards ("exploded" canopy).
    float T[16], R[16], S[16], RS[16];
    mat_ident(T);
    mat_ident(R);
    mat_ident(S);
    const JNode* tr = j_field(d, node, "translation");
    if (tr && tr->kind == JK_ARR) {
        float x = 0, y = 0, z = 0;
        u32 c = tr->child;
        if (c) {
            x = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            y = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            z = static_cast<float>(d->nodes[c].num);
        }
        T[12] = x;
        T[13] = y;
        T[14] = z;
    }
    const JNode* sc = j_field(d, node, "scale");
    if (sc && sc->kind == JK_ARR) {
        float sx = 1, sy = 1, sz = 1;
        u32 c = sc->child;
        if (c) {
            sx = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            sy = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            sz = static_cast<float>(d->nodes[c].num);
        }
        S[0]  = sx;
        S[5]  = sy;
        S[10] = sz;
    }
    const JNode* rot = j_field(d, node, "rotation");
    if (rot && rot->kind == JK_ARR) {
        float x = 0, y = 0, z = 0, w = 1;
        u32 c = rot->child;
        if (c) {
            x = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            y = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            z = static_cast<float>(d->nodes[c].num);
            c = d->nodes[c].next;
        }
        if (c) {
            w = static_cast<float>(d->nodes[c].num);
        }
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;
        R[0]  = 1.f - 2.f * (yy + zz);
        R[1]  = 2.f * (xy + wz);
        R[2]  = 2.f * (xz - wy);
        R[4]  = 2.f * (xy - wz);
        R[5]  = 1.f - 2.f * (xx + zz);
        R[6]  = 2.f * (yz + wx);
        R[8]  = 2.f * (xz + wy);
        R[9]  = 2.f * (yz - wx);
        R[10] = 1.f - 2.f * (xx + yy);
    }
    mat_mul(RS, R, S);
    mat_mul(m, T, RS);
}

const u8* accessor_bytes(const JDoc* d, u32 acc_i, const u8* bin, u32 bin_len, u32* count, u32* stride,
                         u32* comp, u32* ncomp) {
    const JNode* accessors = j_field(d, d->root, "accessors");
    if (!accessors) {
        return nullptr;
    }
    u32 acc = j_arr_at(d, accessors, acc_i);
    if (!acc) {
        return nullptr;
    }
    *count = static_cast<u32>(j_num(d, acc, "count", 0));
    *comp = static_cast<u32>(j_num(d, acc, "componentType", 5126));
    const JNode* ty = j_field(d, acc, "type");
    *ncomp = 1;
    if (str_eq(ty, "VEC2")) {
        *ncomp = 2;
    } else if (str_eq(ty, "VEC3")) {
        *ncomp = 3;
    } else if (str_eq(ty, "VEC4")) {
        *ncomp = 4;
    }
    const u32 bv_i = j_idx(d, acc, "bufferView");
    const u32 acc_off = j_idx(d, acc, "byteOffset");
    const JNode* views = j_field(d, d->root, "bufferViews");
    u32 view = j_arr_at(d, views, bv_i);
    const u32 off = j_idx(d, view, "byteOffset") + acc_off;
    const u32 el = (*comp == 5126) ? 4u : (*comp == 5125) ? 4u : (*comp == 5123) ? 2u : 1u;
    *stride = static_cast<u32>(j_num(d, view, "byteStride", 0));
    if (*stride == 0) {
        *stride = el * (*ncomp);
    }
    if (off >= bin_len) {
        return nullptr;
    }
    return bin + off;
}

float read_f32(const u8* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

float read_acc_f(const u8* p, u32 comp, int normalized) {
    if (comp == 5126) {
        return read_f32(p);
    }
    if (comp == 5125) {
        u32 v;
        std::memcpy(&v, p, 4);
        return normalized ? static_cast<float>(v) / 4294967295.f : static_cast<float>(v);
    }
    if (comp == 5123) {
        u16 v;
        std::memcpy(&v, p, 2);
        return normalized ? static_cast<float>(v) / 65535.f : static_cast<float>(v);
    }
    if (comp == 5122) {
        i16 v;
        std::memcpy(&v, p, 2);
        return normalized ? std::fmax(static_cast<float>(v) / 32767.f, -1.f) : static_cast<float>(v);
    }
    if (comp == 5121) {
        return normalized ? static_cast<float>(p[0]) / 255.f : static_cast<float>(p[0]);
    }
    if (comp == 5120) {
        const i8 v = static_cast<i8>(p[0]);
        return normalized ? std::fmax(static_cast<float>(v) / 127.f, -1.f) : static_cast<float>(v);
    }
    return 0.f;
}

u32 read_index(const u8* p, u32 comp) {
    if (comp == 5125) {
        u32 v;
        std::memcpy(&v, p, 4);
        return v;
    }
    if (comp == 5123) {
        u16 v;
        std::memcpy(&v, p, 2);
        return v;
    }
    return p[0];
}

unsigned solid_tex(u8 r, u8 g, u8 b, u8 a) {
    const u8 px[4] = {r, g, b, a};
    return upload_rgba(px, 1, 1);
}

unsigned white_tex() {
    return solid_tex(220, 220, 220, 255);
}

unsigned gray_metal_tex() {
    return solid_tex(168, 170, 174, 255);
}

unsigned bark_brown_tex() {
    return solid_tex(0x8B, 0x45, 0x13, 255);
}

unsigned leaf_green_tex() {
    return solid_tex(0x22, 0x8B, 0x22, 255);
}

int is_tree_label(const char* s) {
    return s && (std::strcmp(s, "oak") == 0 || std::strcmp(s, "pine") == 0 || std::strcmp(s, "palm") == 0 ||
                 std::strcmp(s, "tree") == 0 || std::strcmp(s, "coconut") == 0);
}

int is_lamp_label(const char* s) {
    return s && (std::strcmp(s, "klassisk") == 0 || std::strcmp(s, "moderne") == 0);
}

unsigned klassisk_iron_tex() {
    return solid_tex(42, 40, 36, 255);
}

unsigned moderne_steel_tex() {
    return solid_tex(30, 34, 40, 255);
}

unsigned fail_red_tex() {
    return solid_tex(255, 32, 32, 255);
}

unsigned lamp_or_fail_tex(const char* label) {
    if (label && std::strcmp(label, "klassisk") == 0) {
        return klassisk_iron_tex();
    }
    if (label && std::strcmp(label, "moderne") == 0) {
        return moderne_steel_tex();
    }
    return fail_red_tex();
}

unsigned black_tex() {
    return solid_tex(0, 0, 0, 255);
}

u8 slen_sig(const u8* s, u32 bl, u32 i) {
    return i < bl ? s[i] : 0;
}

int is_hot_magenta(u8 r, u8 g, u8 b) {
    return (r > 230 && b > 220 && g < 50) ? 1 : 0;
}

void argb_to_rgba(u8* p, u32 tot) {
    for (u32 i = 0; i < tot; ++i) {
        const u8 a = p[i * 4 + 0];
        const u8 r = p[i * 4 + 1];
        const u8 g = p[i * 4 + 2];
        const u8 b = p[i * 4 + 3];
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = a;
    }
}

void mean_rgb(const u8* p, u32 tot, float* mr, float* mg, float* mb) {
    u64 sr = 0, sg = 0, sb = 0, n = 0;
    for (u32 i = 0; i < tot; ++i) {
        const u8 r = p[i * 4 + 0];
        const u8 g = p[i * 4 + 1];
        const u8 b = p[i * 4 + 2];
        if (static_cast<u32>(r) + g + b < 24u) {
            continue;
        }
        sr += r;
        sg += g;
        sb += b;
        ++n;
    }
    *mr = n ? static_cast<float>(sr / n) : 0.f;
    *mg = n ? static_cast<float>(sg / n) : 0.f;
    *mb = n ? static_cast<float>(sb / n) : 0.f;
}

int looks_magenta(float mr, float mg, float mb) {
    return (mr > mg * 1.40f && mb > mg * 1.15f) ? 1 : 0;
}

void kind_pine(u8* p, u32 tot) {
    float mr = 0.f, mg = 0.f, mb = 0.f;
    mean_rgb(p, tot, &mr, &mg, &mb);
    if (looks_magenta(mr, mg, mb)) {
        argb_to_rgba(p, tot);
        mean_rgb(p, tot, &mr, &mg, &mb);
    }
    if (mg < 70.f) {
        for (u32 i = 0; i < tot; ++i) {
            int r = p[i * 4 + 0];
            int g = p[i * 4 + 1];
            int b = p[i * 4 + 2];
            int a = p[i * 4 + 3];
            g = std::min(255, g * 4 + 48);
            r = std::min(255, r * 2 + 12);
            b = std::min(255, b + 10);
            if (a < 128 && g > 70) {
                a = 255;
            }
            p[i * 4 + 0] = static_cast<u8>(r);
            p[i * 4 + 1] = static_cast<u8>(g);
            p[i * 4 + 2] = static_cast<u8>(b);
            p[i * 4 + 3] = static_cast<u8>(a);
        }
        std::printf("[trees] pine: lifted dark needles (mean %.0f,%.0f,%.0f)\n", mr, mg, mb);
        std::fflush(stdout);
    }
}

void kind_palm(u8* p, u32 tot) {
    float mr = 0.f, mg = 0.f, mb = 0.f;
    mean_rgb(p, tot, &mr, &mg, &mb);
    if (looks_magenta(mr, mg, mb)) {
        argb_to_rgba(p, tot);
    }
    u32 frond = 0;
    u32 trunk = 0;
    for (u32 i = 0; i < tot; ++i) {
        const int r = p[i * 4 + 0];
        const int g = p[i * 4 + 1];
        const int b = p[i * 4 + 2];
        const int a = p[i * 4 + 3];
        if (a < 40) {
            continue;
        }
        const int luma = r + g + b;
        if (luma < 100) {
            p[i * 4 + 0] = 0x8B;
            p[i * 4 + 1] = 0x55;
            p[i * 4 + 2] = 0x22;
            p[i * 4 + 3] = 255;
            ++trunk;
            continue;
        }
        const int silver = (r > 160 && g > 160 && b > 160) ? 1 : 0;
        const int cyan   = (b > g + 6) ? 1 : 0;
        if (silver || cyan || g + 8 < ((r + b) / 2)) {
            const int y = (r * 30 + g * 59 + b * 11) / 100;
            p[i * 4 + 0] = static_cast<u8>(std::min(255, y * 45 / 100 + 18));
            p[i * 4 + 1] = static_cast<u8>(std::min(255, y + 36));
            p[i * 4 + 2] = static_cast<u8>(std::min(255, y * 28 / 100 + 8));
            ++frond;
        }
    }
    std::printf("[trees] palm: trunk %u brown, frond %u greened (mean %.0f,%.0f,%.0f)\n", trunk, frond, mr, mg,
                mb);
    std::fflush(stdout);
}

void kind_oak(u8* p, u32 tot) {
    float mr = 0.f, mg = 0.f, mb = 0.f;
    mean_rgb(p, tot, &mr, &mg, &mb);
    if (looks_magenta(mr, mg, mb)) {
        argb_to_rgba(p, tot);
    }
    u32 punched = 0;
    u32 bark    = 0;
    for (u32 i = 0; i < tot; ++i) {
        const u8 r = p[i * 4 + 0];
        const u8 g = p[i * 4 + 1];
        const u8 b = p[i * 4 + 2];
        if (is_hot_magenta(r, g, b) || (r < 32 && g < 32 && b < 32)) {
            p[i * 4 + 3] = 0;
            ++punched;
            continue;
        }
        const int leaf = (g > r + 8 && g > b + 8) ? 1 : 0;
        if (!leaf && p[i * 4 + 3] < 250) {
            p[i * 4 + 3] = 255;
            ++bark;
        }
    }
    std::printf("[trees] oak: punched %u black/magenta card texels, filled %u bark\n", punched, bark);
    std::fflush(stdout);
}

void kind_klassisk(u8* p, u32 tot) {
    float mr = 0.f, mg = 0.f, mb = 0.f;
    mean_rgb(p, tot, &mr, &mg, &mb);
    if (looks_magenta(mr, mg, mb)) {
        argb_to_rgba(p, tot);
        mean_rgb(p, tot, &mr, &mg, &mb);
    }
    if ((mr + mg + mb) * (1.f / 3.f) > 140.f) {
        for (u32 i = 0; i < tot; ++i) {
            p[i * 4 + 0] = static_cast<u8>(std::min(255, static_cast<int>(p[i * 4 + 0]) * 22 / 100 + 38));
            p[i * 4 + 1] = static_cast<u8>(std::min(255, static_cast<int>(p[i * 4 + 1]) * 22 / 100 + 36));
            p[i * 4 + 2] = static_cast<u8>(std::min(255, static_cast<int>(p[i * 4 + 2]) * 22 / 100 + 32));
        }
        std::printf("[glb] klassisk: pulled washed-white metal to dark iron (mean %.0f,%.0f,%.0f)\n", mr, mg, mb);
        std::fflush(stdout);
    }
}

void kind_moderne(u8* p, u32 tot) {
    float mr = 0.f, mg = 0.f, mb = 0.f;
    mean_rgb(p, tot, &mr, &mg, &mb);
    if (looks_magenta(mr, mg, mb)) {
        argb_to_rgba(p, tot);
        mean_rgb(p, tot, &mr, &mg, &mb);
    }
    if ((mr + mg + mb) * (1.f / 3.f) > 140.f) {
        for (u32 i = 0; i < tot; ++i) {
            p[i * 4 + 0] = static_cast<u8>(std::min(255, static_cast<int>(p[i * 4 + 0]) * 20 / 100 + 30));
            p[i * 4 + 1] = static_cast<u8>(std::min(255, static_cast<int>(p[i * 4 + 1]) * 20 / 100 + 34));
            p[i * 4 + 2] = static_cast<u8>(std::min(255, static_cast<int>(p[i * 4 + 2]) * 20 / 100 + 40));
        }
        std::printf("[glb] moderne: pulled washed-white metal to anthracite (mean %.0f,%.0f,%.0f)\n", mr, mg, mb);
        std::fflush(stdout);
    }
}

void repair_tree_tex(u8* p, u32 w, u32 h, const char* label) {
    if (!p || !w || !h) {
        return;
    }
    const u32 tot = w * h;
    if (label && std::strcmp(label, "pine") == 0) {
        kind_pine(p, tot);
        return;
    }
    if (label && std::strcmp(label, "palm") == 0) {
        kind_palm(p, tot);
        return;
    }
    if (label && std::strcmp(label, "oak") == 0) {
        kind_oak(p, tot);
        return;
    }
    if (label && std::strcmp(label, "klassisk") == 0) {
        kind_klassisk(p, tot);
        return;
    }
    if (label && std::strcmp(label, "moderne") == 0) {
        kind_moderne(p, tot);
        return;
    }
}

int read_f3_arr(const JDoc* d, const JNode* arr, float* o) {
    if (!arr || arr->kind != JK_ARR) {
        return 0;
    }
    u32 i = 0;
    for (u32 c = arr->child; c != 0 && i < 3; c = d->nodes[c].next, ++i) {
        o[i] = static_cast<float>(d->nodes[c].num);
    }
    return i == 3;
}

unsigned decode_view_image(const JDoc* d, u32 img, const u8* bin, u32 bin_len, u32* out_w, u32* out_h,
                          int* out_a, const char* tree_label) {
    *out_w = *out_h = 0;
    if (out_a) {
        *out_a = 0;
    }
    if (!img) {
        return 0;
    }
    const JNode* bv_n = j_field(d, img, "bufferView");
    if (!bv_n || bv_n->kind != JK_NUM) {
        std::printf("[glb] WARNING: image has no bufferView (uri-only?)\n");
        std::fflush(stdout);
        return 0;
    }
    const u32 bv = static_cast<u32>(bv_n->num);
    const JNode* views = j_field(d, d->root, "bufferViews");
    u32 view = j_arr_at(d, views, bv);
    if (!view) {
        std::printf("[glb] WARNING: image bufferView %u missing\n", bv);
        std::fflush(stdout);
        return 0;
    }
    const u32 off = j_idx(d, view, "byteOffset");
    const u32 bl = j_idx(d, view, "byteLength");
    if (bl == 0 || off + bl > bin_len) {
        std::printf("[glb] WARNING: image bytes out of range off=%u len=%u bin=%u\n", off, bl, bin_len);
        std::fflush(stdout);
        return 0;
    }
    u8* rgba = nullptr;
    u32 w = 0, h = 0;
    const int is_prop = is_tree_label(tree_label) || is_lamp_label(tree_label);
    const int ok_img  = is_prop ? decode_image_rgba_tree(bin + off, bl, &rgba, &w, &h)
                                : decode_image_rgba(bin + off, bl, &rgba, &w, &h);
    if (!ok_img || !rgba) {
        const u8* s = bin + off;
        std::printf("[glb] WARNING: Failed to decode image (%u bytes, sig %02x %02x %02x %02x)\n", bl,
                    slen_sig(s, bl, 0), slen_sig(s, bl, 1), slen_sig(s, bl, 2), slen_sig(s, bl, 3));
        std::fflush(stdout);
        return 0;
    }
    if (is_prop) {
        repair_tree_tex(rgba, w, h, tree_label);
    }
    unsigned tex = upload_rgba(rgba, w, h);
    std::printf("[glb] Created OpenGL texture ID %u (%ux%u pixels, %u src bytes)\n", tex, w, h, bl);
    std::fflush(stdout);
    std::free(rgba);
    *out_w = w;
    *out_h = h;
    if (out_a) {
        *out_a = g_last_tex_alpha;
    }
    return tex;
}

bool emit_prim(TreeGlb* out, const JDoc* d, u32 prim, const float* world, const u8* bin, u32 bin_len,
               unsigned* tex_cache, const u32* tex_w, const u32* tex_h, const int* tex_a, u32 ntex) {
    if (out->nprims >= kTreePrimCap) {
        std::printf("[glb] WARNING: primitive cap %u hit — extra leaf meshes dropped\n", kTreePrimCap);
        std::fflush(stdout);
        return true;
    }
    const JNode* attrs = j_field(d, prim, "attributes");
    if (!attrs) {
        return false;
    }
    u32 attr_id = static_cast<u32>(attrs - d->nodes);
    const JNode* pacc = j_field(d, attr_id, "POSITION");
    const JNode* nacc = j_field(d, attr_id, "NORMAL");
    const JNode* uacc = j_field(d, attr_id, "TEXCOORD_0");
    const JNode* cacc = j_field(d, attr_id, "COLOR_0");
    {
        char alist[160];
        u32 ap = 0;
        auto adda = [&](const char* s, const JNode* n) {
            if (!n || ap + 24 >= sizeof(alist)) {
                return;
            }
            if (ap) {
                alist[ap++] = ',';
                alist[ap++] = ' ';
            }
            while (*s && ap + 1 < sizeof(alist)) {
                alist[ap++] = *s++;
            }
            alist[ap] = 0;
        };
        adda("POSITION", pacc);
        adda("NORMAL", nacc);
        adda("TEXCOORD_0", uacc);
        adda("COLOR_0", cacc);
        const JNode* t1 = j_field(d, attr_id, "TEXCOORD_1");
        adda("TEXCOORD_1", t1);
        std::printf("[glb] %s primitive %u: attributes=[%s] COLOR_0: %s mode=%d\n", out->label, out->nprims,
                    alist, cacc ? "YES" : "NO", static_cast<int>(j_num(d, prim, "mode", 4)));
        std::fflush(stdout);
    }
    if (!pacc) {
        return false;
    }
    u32 pc = 0, ps = 0, pcomp = 0, pn = 0;
    const u8* pb = accessor_bytes(d, static_cast<u32>(pacc->num), bin, bin_len, &pc, &ps, &pcomp, &pn);
    u32 nc = 0, ns = 0, ncomp = 0, nn = 0;
    const u8* nb = nacc ? accessor_bytes(d, static_cast<u32>(nacc->num), bin, bin_len, &nc, &ns, &ncomp, &nn)
                        : nullptr;
    u32 uc = 0, us = 0, ucomp = 0, un = 0;
    const u8* ub = uacc ? accessor_bytes(d, static_cast<u32>(uacc->num), bin, bin_len, &uc, &us, &ucomp, &un)
                        : nullptr;
    u32 cc = 0, cs = 0, ccomp = 0, cn = 0;
    const u8* cb = cacc ? accessor_bytes(d, static_cast<u32>(cacc->num), bin, bin_len, &cc, &cs, &ccomp, &cn)
                        : nullptr;
    if (!pb || pc == 0) {
        return false;
    }
    const JNode* accessors = j_field(d, d->root, "accessors");
    u32 pacc_n = accessors ? j_arr_at(d, accessors, static_cast<u32>(pacc->num)) : 0;
    int pnorm = 0;
    if (pacc_n) {
        const JNode* pnrm = j_field(d, pacc_n, "normalized");
        pnorm = (pnrm && pnrm->kind == JK_BOOL && pnrm->num != 0) ? 1 : 0;
    }
    float pmin[3] = {0, 0, 0};
    float pmax[3] = {0, 0, 0};
    int has_pmm = pacc_n ? read_f3_arr(d, j_field(d, pacc_n, "min"), pmin) &&
                               read_f3_arr(d, j_field(d, pacc_n, "max"), pmax)
                         : 0;
    std::printf("[glb] %s prim %u POSITION count=%u comp=%u stride=%u normalized=%d minmax=%d\n", out->label,
                out->nprims, pc, pcomp, ps, pnorm, has_pmm);
    if (cacc && cc > 0 && cb) {
        const u32 cel = (ccomp == 5126 || ccomp == 5125) ? 4u : (ccomp == 5123 || ccomp == 5122) ? 2u : 1u;
        const int cnorm = (ccomp != 5126);
        const u32 show = cc < 5 ? cc : 5;
        for (u32 i = 0; i < show; ++i) {
            const u8* cp = cb + i * cs;
            std::printf("[glb] %s vertex %u color=(%.3f,%.3f,%.3f,%.3f) — IGNORED\n", out->label, i,
                        read_acc_f(cp, ccomp, cnorm), read_acc_f(cp + cel, ccomp, cnorm),
                        read_acc_f(cp + cel * 2u, ccomp, cnorm),
                        cn >= 4 ? read_acc_f(cp + cel * 3u, ccomp, cnorm) : 1.f);
        }
        std::fflush(stdout);
    }
    u32 uv_es = 4;
    int uv_norm = 0;
    if (uacc) {
        if (ucomp == 5123 || ucomp == 5122) {
            uv_es = 2;
        } else if (ucomp == 5121 || ucomp == 5120) {
            uv_es = 1;
        } else if (ucomp == 5125) {
            uv_es = 4;
        }
        const JNode* accessors = j_field(d, d->root, "accessors");
        u32 uacc_n = accessors ? j_arr_at(d, accessors, static_cast<u32>(uacc->num)) : 0;
        const JNode* nrmn = uacc_n ? j_field(d, uacc_n, "normalized") : nullptr;
        uv_norm = (ucomp != 5126) || (nrmn && nrmn->kind == JK_BOOL && nrmn->num != 0);
        if (ucomp != 5126) {
            uv_norm = 1;
        }
    }
    TreeVert* verts = static_cast<TreeVert*>(std::malloc(sizeof(TreeVert) * pc));
    if (!verts) {
        return false;
    }
    for (u32 i = 0; i < pc; ++i) {
        const u8* vp = pb + i * ps;
        const u32 pel = (pcomp == 5126 || pcomp == 5125) ? 4u : (pcomp == 5123 || pcomp == 5122) ? 2u : 1u;
        float x = read_acc_f(vp, pcomp, pnorm);
        float y = read_acc_f(vp + pel, pcomp, pnorm);
        float z = read_acc_f(vp + pel * 2u, pcomp, pnorm);
        if (i < 3u) {
            std::printf("[glb] %s prim %u vtx %u pos=(%.4f,%.4f,%.4f) acc_min=(%.4f,%.4f,%.4f) acc_max=(%.4f,%.4f,%.4f)\n",
                        out->label, out->nprims, i, x, y, z, pmin[0], pmin[1], pmin[2], pmax[0], pmax[1], pmax[2]);
        }
        float ox, oy, oz;
        mul_mat_vec3(world, x, y, z, &ox, &oy, &oz);
        if (ox < out->xmin) {
            out->xmin = ox;
        }
        if (ox > out->xmax) {
            out->xmax = ox;
        }
        if (oy < out->ymin) {
            out->ymin = oy;
        }
        if (oy > out->ymax) {
            out->ymax = oy;
        }
        if (oz < out->zmin) {
            out->zmin = oz;
        }
        if (oz > out->zmax) {
            out->zmax = oz;
        }
        float nx = 0, ny = 1, nz = 0;
        if (nb && i < nc) {
            const u8* np = nb + i * ns;
            const u32 nel = (ncomp == 5126 || ncomp == 5125) ? 4u : (ncomp == 5123 || ncomp == 5122) ? 2u : 1u;
            const int nrm = (ncomp != 5126);
            nx = read_acc_f(np, ncomp, nrm);
            ny = read_acc_f(np + nel, ncomp, nrm);
            nz = read_acc_f(np + nel * 2u, ncomp, nrm);
            mul_mat_dir(world, nx, ny, nz, &nx, &ny, &nz);
            const float ls = nx * nx + ny * ny + nz * nz;
            if (ls > 1e-12f) {
                const float inv = 1.f / std::sqrt(ls);
                nx *= inv;
                ny *= inv;
                nz *= inv;
            }
        }
        float u = 0, v = 0;
        if (ub && i < uc) {
            const u8* up = ub + i * us;
            u = read_acc_f(up, ucomp, uv_norm);
            v = read_acc_f(up + uv_es, ucomp, uv_norm);
        }
        float cr = 1.f, cg = 1.f, cbv = 1.f, ca = 1.f;
        if (cb && i < cc && !is_tree_label(out->label) && !is_lamp_label(out->label)) {
            const u8* cp = cb + i * cs;
            const u32 cel = (ccomp == 5126 || ccomp == 5125) ? 4u : (ccomp == 5123 || ccomp == 5122) ? 2u : 1u;
            const int cnorm = (ccomp != 5126);
            cr  = read_acc_f(cp, ccomp, cnorm);
            cg  = read_acc_f(cp + cel, ccomp, cnorm);
            cbv = read_acc_f(cp + cel * 2u, ccomp, cnorm);
            if (cn >= 4) {
                ca = read_acc_f(cp + cel * 3u, ccomp, cnorm);
            }
        }
        verts[i] = TreeVert{ox, oy, oz, nx, ny, nz, u, v, cr, cg, cbv, ca};
    }
    const JNode* idxn = j_field(d, prim, "indices");
    u32 nidx = pc;
    u32* idx = nullptr;
    if (idxn && idxn->kind == JK_NUM) {
        u32 ic = 0, is = 0, icomp = 0, in = 0;
        const u8* ib = accessor_bytes(d, static_cast<u32>(idxn->num), bin, bin_len, &ic, &is, &icomp, &in);
        nidx = ic;
        idx = static_cast<u32*>(std::malloc(sizeof(u32) * nidx));
        if (!idx) {
            std::free(verts);
            return false;
        }
        for (u32 i = 0; i < nidx; ++i) {
            idx[i] = read_index(ib + i * is, icomp);
        }
    } else {
        nidx = pc;
        idx = static_cast<u32*>(std::malloc(sizeof(u32) * nidx));
        if (!idx) {
            std::free(verts);
            return false;
        }
        for (u32 i = 0; i < nidx; ++i) {
            idx[i] = i;
        }
    }
    TreePrim& pr = out->prims[out->nprims];
    std::memset(&pr, 0, sizeof(pr));
    glGenVertexArrays(1, &pr.vao);
    glGenBuffers(1, &pr.vbo);
    glGenBuffers(1, &pr.ibo);
    glBindVertexArray(pr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, pr.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(pc * sizeof(TreeVert)), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pr.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(nidx * sizeof(u32)), idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TreeVert), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(TreeVert),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TreeVert),
                          reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(TreeVert),
                          reinterpret_cast<void*>(8 * sizeof(float)));
    glBindVertexArray(0);
    pr.nidx = nidx;
    pr.alpha_mask = 0;
    pr.cutoff = 0.5f;
    pr.tex = 0;
    pr.tex_emit = 0;
    pr.gl_mode = static_cast<int>(j_num(d, prim, "mode", 4));
    pr.has_color0 = cacc ? 1 : 0;
    const JNode* matn = j_field(d, prim, "material");
    u32 mat_i = 0;
    if (matn && matn->kind == JK_NUM) {
        mat_i = static_cast<u32>(matn->num);
        const JNode* materials = j_field(d, d->root, "materials");
        u32 mat = j_arr_at(d, materials, mat_i);
        if (mat) {
            const JNode* am = j_field(d, mat, "alphaMode");
            const JNode* mnm = j_field(d, mat, "name");
            if (str_eq(am, "MASK") || str_eq(am, "BLEND")) {
                pr.alpha_mask = 1;
            }
            pr.cutoff = static_cast<float>(j_num(d, mat, "alphaCutoff", 0.5));
            if (pr.cutoff < 0.01f) {
                pr.cutoff = 0.5f;
            }
            std::printf("[glb] %s material %u: name='%.*s' alphaMode=%s alphaCutoff=%.2f\n", out->label, mat_i,
                        mnm && mnm->kind == JK_STR ? static_cast<int>(mnm->slen) : 1,
                        mnm && mnm->kind == JK_STR ? mnm->s : "-",
                        pr.alpha_mask ? (str_eq(am, "BLEND") ? "BLEND" : "MASK") : "OPAQUE", pr.cutoff);
            const JNode* pbr = j_field(d, mat, "pbrMetallicRoughness");
            if (pbr) {
                u32 pbr_id = static_cast<u32>(pbr - d->nodes);
                const JNode* bct = j_field(d, pbr_id, "baseColorTexture");
                if (bct) {
                    const u32 tex_i = j_idx(d, static_cast<u32>(bct - d->nodes), "index");
                    if (tex_i < ntex && tex_cache[tex_i]) {
                        pr.tex       = tex_cache[tex_i];
                        pr.tex_w     = tex_w[tex_i];
                        pr.tex_h     = tex_h[tex_i];
                        pr.has_alpha = tex_a[tex_i];
                        if (pr.has_alpha) {
                            pr.alpha_mask = 1;
                            pr.cutoff     = 0.5f;
                        }
                        std::printf("[glb] %s material %u: baseColorTexture=%u, alphaMode=%s\n", out->label,
                                    mat_i, tex_i, pr.alpha_mask ? "MASK" : "OPAQUE");
                        std::printf("[glb] %s texture %u: %ux%u pixels, channels=4\n", out->label, tex_i,
                                    pr.tex_w, pr.tex_h);
                        std::fflush(stdout);
                    } else {
                        std::printf("[glb] WARNING: Failed to load texture for material %u%s\n", mat_i,
                                    is_tree_label(out->label) ? " — tree fallback later"
                                    : is_lamp_label(out->label) ? " — lamp metal fallback"
                                                                : " — debug RED");
                        std::fflush(stdout);
                        if (!is_tree_label(out->label)) {
                            pr.tex   = lamp_or_fail_tex(out->label);
                            pr.tex_w = pr.tex_h = 1;
                        }
                    }
                }
                if (!pr.tex) {
                    const JNode* bcf = j_field(d, pbr_id, "baseColorFactor");
                    if (bcf && bcf->kind == JK_ARR) {
                        float f[4] = {1.f, 1.f, 1.f, 1.f};
                        u32 ci = 0;
                        for (u32 c = bcf->child; c != 0 && ci < 4; c = d->nodes[c].next, ++ci) {
                            f[ci] = static_cast<float>(d->nodes[c].num);
                        }
                        const float lum = f[0] * 0.30f + f[1] * 0.59f + f[2] * 0.11f;
                        if (lum < 0.18f) {
                            f[0] = 0.62f;
                            f[1] = 0.63f;
                            f[2] = 0.66f;
                        }
                        pr.tex = solid_tex(static_cast<u8>(clampf(f[0], 0.f, 1.f) * 255.f),
                                           static_cast<u8>(clampf(f[1], 0.f, 1.f) * 255.f),
                                           static_cast<u8>(clampf(f[2], 0.f, 1.f) * 255.f),
                                           static_cast<u8>(clampf(f[3], 0.f, 1.f) * 255.f));
                        pr.tex_w = pr.tex_h = 1;
                    }
                }
            }
            const JNode* emt = j_field(d, mat, "emissiveTexture");
            if (emt) {
                const u32 tex_i = j_idx(d, static_cast<u32>(emt - d->nodes), "index");
                if (tex_i < ntex) {
                    pr.tex_emit = tex_cache[tex_i];
                }
            }
            if (!pr.tex_emit) {
                const JNode* emf = j_field(d, mat, "emissiveFactor");
                if (emf && emf->kind == JK_ARR) {
                    float f[3] = {0, 0, 0};
                    u32 ci = 0;
                    for (u32 c = emf->child; c != 0 && ci < 3; c = d->nodes[c].next, ++ci) {
                        f[ci] = static_cast<float>(d->nodes[c].num);
                    }
                    if (f[0] + f[1] + f[2] > 0.01f) {
                        pr.tex_emit = solid_tex(static_cast<u8>(clampf(f[0], 0.f, 1.f) * 255.f),
                                                static_cast<u8>(clampf(f[1], 0.f, 1.f) * 255.f),
                                                static_cast<u8>(clampf(f[2], 0.f, 1.f) * 255.f), 255);
                    }
                }
            }
        }
    }
    if (!pr.tex) {
        if (is_tree_label(out->label)) {
            pr.tex = pr.alpha_mask ? leaf_green_tex() : bark_brown_tex();
            std::printf("[glb] %s material %u has no albedo — fallback %s\n", out->label, mat_i,
                        pr.alpha_mask ? "leaf green #228B22" : "bark brown #8B4513");
        } else if (out->label[0] && std::strcmp(out->label, "klassisk") == 0) {
            pr.tex = klassisk_iron_tex();
            std::printf("[glb] klassisk material %u has no albedo — dark iron\n", mat_i);
        } else if (out->label[0] && std::strcmp(out->label, "moderne") == 0) {
            pr.tex = moderne_steel_tex();
            std::printf("[glb] moderne material %u has no albedo — anthracite\n", mat_i);
        } else {
            std::printf("[glb] WARNING: Failed to load texture for material %u — using gray metal\n", mat_i);
            pr.tex = gray_metal_tex();
        }
        std::fflush(stdout);
        pr.tex_w = pr.tex_h = 1;
    }
    if (!pr.tex_emit) {
        pr.tex_emit = black_tex();
    }
    {
        const char* solid = std::getenv("LEONIDA_TREE_SOLID");
        if (solid && solid[0] == '1' && is_tree_label(out->label)) {
            const int leaf = pr.alpha_mask;
            pr.tex = leaf ? leaf_green_tex() : bark_brown_tex();
            pr.alpha_mask = 0;
            pr.tex_w = pr.tex_h = 1;
            std::printf("[glb] %s SOLID DEBUG: %s # %s (alpha off)\n", out->label, leaf ? "leaf" : "bark",
                        leaf ? "228B22" : "8B4513");
        }
    }
    std::printf("[glb] %s prim %u draw mode=%d nidx=%u nvert=%u tex=%ux%u mask=%d cutoff=%.2f COLOR_0=%s\n",
                out->label, out->nprims, pr.gl_mode, nidx, pc, pr.tex_w, pr.tex_h, pr.alpha_mask, pr.cutoff,
                pr.has_color0 ? "YES-ignored" : "NO");
    std::fflush(stdout);
    out->nverts += pc;
    out->nprims++;
    std::free(verts);
    std::free(idx);
    return true;
}

void walk_node(TreeGlb* out, const JDoc* d, u32 node, const float* parent, const u8* bin, u32 bin_len,
               unsigned* tex_cache, const u32* tex_w, const u32* tex_h, const int* tex_a, u32 ntex) {
    float local[16], world[16];
    node_local(d, node, local);
    mat_mul(world, parent, local);
    {
        const JNode* nn = j_field(d, node, "name");
        const JNode* mesh_n0 = j_field(d, node, "mesh");
        std::printf("[glb] %s node '%.*s' mesh=%s T=(%.3f,%.3f,%.3f)\n", out->label,
                    nn && nn->kind == JK_STR ? static_cast<int>(nn->slen) : 1, nn && nn->kind == JK_STR ? nn->s : "-",
                    mesh_n0 && mesh_n0->kind == JK_NUM ? "yes" : "no", local[12], local[13], local[14]);
        std::fflush(stdout);
    }
    const JNode* mesh_n = j_field(d, node, "mesh");
    if (mesh_n && mesh_n->kind == JK_NUM) {
        const JNode* meshes = j_field(d, d->root, "meshes");
        u32 mesh = j_arr_at(d, meshes, static_cast<u32>(mesh_n->num));
        const JNode* prims = j_field(d, mesh, "primitives");
        const JNode* mname = j_field(d, mesh, "name");
        u32 npr = 0;
        if (prims && prims->kind == JK_ARR) {
            for (u32 c = prims->child; c != 0; c = d->nodes[c].next) {
                ++npr;
            }
        }
        std::printf("[glb] %s mesh '%.*s': %u primitives\n", out->label,
                    mname && mname->kind == JK_STR ? static_cast<int>(mname->slen) : 1,
                    mname && mname->kind == JK_STR ? mname->s : "-", npr);
        std::fflush(stdout);
        if (prims && prims->kind == JK_ARR) {
            for (u32 c = prims->child; c != 0; c = d->nodes[c].next) {
                emit_prim(out, d, c, world, bin, bin_len, tex_cache, tex_w, tex_h, tex_a, ntex);
            }
        }
    }
    const JNode* ch = j_field(d, node, "children");
    if (ch && ch->kind == JK_ARR) {
        for (u32 c = ch->child; c != 0; c = d->nodes[c].next) {
            if (d->nodes[c].kind == JK_NUM) {
                walk_node(out, d, j_arr_at(d, j_field(d, d->root, "nodes"), static_cast<u32>(d->nodes[c].num)),
                          world, bin, bin_len, tex_cache, tex_w, tex_h, tex_a, ntex);
            }
        }
    }
}

} // namespace

const char* glb_short_name(const char* path) {
    const char* base = path;
    for (const char* p = path; p && *p; ++p) {
        if (*p == '/') {
            base = p + 1;
        }
    }
    if (std::strstr(base, "coconut")) {
        return "palm";
    }
    if (std::strstr(base, "pine")) {
        return "pine";
    }
    if (std::strstr(base, "palm") || std::strstr(base, "oak")) {
        return std::strstr(base, "palm") ? "palm" : "oak";
    }
    if (std::strcmp(base, "tree.glb") == 0) {
        return "oak";
    }
    if (std::strstr(base, "klassisk")) {
        return "klassisk";
    }
    if (std::strstr(base, "moderne")) {
        return "moderne";
    }
    if (std::strstr(base, "skyscraper")) {
        return "skyscraper";
    }
    return base;
}

u32 j_arr_len(const JDoc* d, const JNode* arr) {
    if (!arr || arr->kind != JK_ARR) {
        return 0;
    }
    u32 n = 0;
    for (u32 c = arr->child; c != 0; c = d->nodes[c].next) {
        ++n;
    }
    return n;
}

bool load_tree_glb(const char* path, TreeGlb* out) {
    std::memset(out, 0, sizeof(*out));
    out->ymin = out->xmin = out->zmin = 1.0e9f;
    out->ymax = out->xmax = out->zmax = -1.0e9f;
    {
        const char* sn = glb_short_name(path);
        u32 i = 0;
        while (sn[i] && i + 1u < 32u) {
            out->label[i] = sn[i];
            ++i;
        }
        out->label[i] = 0;
    }
    std::printf("[glb] Loading tree model: %s\n", path);
    std::fflush(stdout);
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("[gl] tree glb missing %s\n", path);
        std::fflush(stdout);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 20) {
        std::fclose(f);
        return false;
    }
    u8* file = static_cast<u8*>(std::malloc(static_cast<usize>(sz) + 1));
    if (!file) {
        std::fclose(f);
        return false;
    }
    const usize nread = std::fread(file, 1, static_cast<usize>(sz), f);
    std::fclose(f);
    file[nread] = 0;
    u32 magic = 0, ver = 0, length = 0;
    std::memcpy(&magic, file, 4);
    std::memcpy(&ver, file + 4, 4);
    std::memcpy(&length, file + 8, 4);
    if (magic != 0x46546C67u || ver != 2) {
        std::printf("[gl] not a GLB2: %s\n", path);
        std::free(file);
        return false;
    }
    u32 cursor = 12;
    const char* json = nullptr;
    u32 json_len = 0;
    const u8* bin = nullptr;
    u32 bin_len = 0;
    while (cursor + 8 <= nread) {
        u32 clen = 0, cty = 0;
        std::memcpy(&clen, file + cursor, 4);
        std::memcpy(&cty, file + cursor + 4, 4);
        cursor += 8;
        if (cursor + clen > nread) {
            break;
        }
        if (cty == 0x4E4F534Au) {
            json = reinterpret_cast<const char*>(file + cursor);
            json_len = clen;
        } else if (cty == 0x004E4942u) {
            bin = file + cursor;
            bin_len = clen;
        }
        cursor += clen;
    }
    if (!json || !bin) {
        std::free(file);
        return false;
    }
    char* json_z = static_cast<char*>(std::malloc(json_len + 1));
    if (!json_z) {
        std::free(file);
        return false;
    }
    std::memcpy(json_z, json, json_len);
    json_z[json_len] = 0;
    const u32 jcap = 65536;
    JNode* nodes = static_cast<JNode*>(std::malloc(sizeof(JNode) * jcap));
    if (!nodes) {
        std::free(json_z);
        std::free(file);
        return false;
    }
    JDoc doc{};
    doc.nodes = nodes;
    doc.cap = jcap;
    doc.n = 1; // 0 unused so child=0 means empty
    const char* jp = json_z;
    u32 root = 0;
    if (!j_parse_value(&doc, &jp, &root)) {
        std::printf("[gl] tree glb JSON parse failed %s\n", path);
        std::free(nodes);
        std::free(json_z);
        std::free(file);
        return false;
    }
    doc.root = root;

    unsigned tex_cache[64];
    u32 tex_w[64];
    u32 tex_h[64];
    int tex_a[64];
    std::memset(tex_cache, 0, sizeof(tex_cache));
    std::memset(tex_w, 0, sizeof(tex_w));
    std::memset(tex_h, 0, sizeof(tex_h));
    std::memset(tex_a, 0, sizeof(tex_a));
    u32 ntex = 0;
    const JNode* textures = j_field(&doc, root, "textures");
    const JNode* images = j_field(&doc, root, "images");
    if (textures && textures->kind == JK_ARR && textures->child != 0) {
        for (u32 c = textures->child; c != 0 && ntex < 64; c = doc.nodes[c].next) {
            const u32 src_i = j_idx(&doc, c, "source");
            unsigned tex = 0;
            u32 w = 0, h = 0;
            int ha = 0;
            if (images) {
                u32 img = j_arr_at(&doc, images, src_i);
                tex = decode_view_image(&doc, img, bin, bin_len, &w, &h, &ha, out->label);
            }
            if (!tex) {
                std::printf("[glb] WARNING: Failed to load texture %u (image source %u)%s\n", ntex, src_i,
                            is_tree_label(out->label) ? " — tree will use brown/green fallback"
                            : is_lamp_label(out->label) ? " — lamp metal fallback"
                                                        : " — debug RED");
                std::fflush(stdout);
                if (!is_tree_label(out->label)) {
                    tex = lamp_or_fail_tex(out->label);
                    w = h = 1;
                    ha    = 0;
                }
            }
            tex_w[ntex]     = w;
            tex_h[ntex]     = h;
            tex_a[ntex]     = ha;
            tex_cache[ntex] = tex;
            ++ntex;
        }
    } else if (images && images->kind == JK_ARR) {
        for (u32 c = images->child; c != 0 && ntex < 64; c = doc.nodes[c].next) {
            u32 w = 0, h = 0;
            int ha          = 0;
            unsigned tex = decode_view_image(&doc, c, bin, bin_len, &w, &h, &ha, out->label);
            if (!tex && !is_tree_label(out->label)) {
                tex = lamp_or_fail_tex(out->label);
                w = h = 1;
                ha    = 0;
            }
            tex_w[ntex]     = w;
            tex_h[ntex]     = h;
            tex_a[ntex]     = ha;
            tex_cache[ntex] = tex;
            ++ntex;
        }
    }
    std::printf("[glb] textures decoded=%u json_nodes=%u bin=%u\n", ntex, doc.n, bin_len);
    std::printf("[glb] Tree '%s' meshes: %u, materials: %u, textures: %u\n", out->label,
                j_arr_len(&doc, j_field(&doc, root, "meshes")),
                j_arr_len(&doc, j_field(&doc, root, "materials")), ntex);
    std::fflush(stdout);

    float ident[16];
    mat_ident(ident);
    const JNode* scenes = j_field(&doc, root, "scenes");
    const JNode* nodesj = j_field(&doc, root, "nodes");
    u32 scene_i = static_cast<u32>(j_num(&doc, root, "scene", 0));
    u32 scene = scenes ? j_arr_at(&doc, scenes, scene_i) : 0;
    const JNode* snodes = scene ? j_field(&doc, scene, "nodes") : nullptr;
    if (snodes && snodes->kind == JK_ARR && nodesj) {
        for (u32 c = snodes->child; c != 0; c = doc.nodes[c].next) {
            if (doc.nodes[c].kind == JK_NUM) {
                u32 ni = j_arr_at(&doc, nodesj, static_cast<u32>(doc.nodes[c].num));
                if (ni) {
                    walk_node(out, &doc, ni, ident, bin, bin_len, tex_cache, tex_w, tex_h, tex_a, ntex);
                }
            }
        }
    }

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
    std::free(nodes);
    std::free(json_z);
    std::free(file);
    if (out->ymax < out->ymin) {
        out->ymin = 0.f;
        out->ymax = 1.f;
    }
    if (out->xmax < out->xmin) {
        out->xmin = -0.5f;
        out->xmax = 0.5f;
    }
    if (out->zmax < out->zmin) {
        out->zmin = -0.5f;
        out->zmax = 0.5f;
    }
    {
        const float hy = out->ymax - out->ymin;
        const float hx = out->xmax - out->xmin;
        const float hz = out->zmax - out->zmin;
        const float y_plant = std::min(std::fabs(out->ymin), std::fabs(out->ymax)) / (hy + 1e-4f);
        const float z_plant = std::min(std::fabs(out->zmin), std::fabs(out->zmax)) / (hz + 1e-4f);
        const float x_plant = std::min(std::fabs(out->xmin), std::fabs(out->xmax)) / (hx + 1e-4f);
        // Unique long axis → up. A wide Y-up canopy has hx≈hz, so it stays Y-up.
        out->z_up = 0;
        if (hx > hy * 1.45f && hx > hz * 1.45f) {
            out->z_up = 2;
        } else if (hz > hy * 1.45f && hz > hx * 1.45f) {
            out->z_up = 1;
        } else if (z_plant < y_plant * 0.45f && hz >= hy * 0.8f && hz >= hx * 0.8f) {
            out->z_up = 1;
        }
        (void)x_plant;
    }
    std::printf("[glb] %s AABB x=[%.3f,%.3f] y=[%.3f,%.3f] z=[%.3f,%.3f] size=(%.3f,%.3f,%.3f) z_up=%d prims=%u verts=%u\n",
                out->label, out->xmin, out->xmax, out->ymin, out->ymax, out->zmin, out->zmax,
                out->xmax - out->xmin, out->ymax - out->ymin, out->zmax - out->zmin, out->z_up, out->nprims,
                out->nverts);
    std::printf("[gl] tree glb %s prims=%u verts=%u bytes=%ld height=%.2f (y=%.2f..%.2f) z_up=%d\n", path,
                out->nprims, out->nverts, static_cast<long>(sz), out->ymax - out->ymin, out->ymin, out->ymax,
                out->z_up);
    std::fflush(stdout);
    return out->nprims > 0;
}

bool try_load_path(const char* path, TreeGlb* out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        return false;
    }
    std::fclose(f);
    std::printf("[gl] found tree model %s\n", path);
    std::fflush(stdout);
    return load_tree_glb(path, out);
}

void dir_of(const char* path, char* dst, u32 cap) {
    dst[0] = 0;
    if (!path) {
        return;
    }
    u32 last = 0;
    u32 n = 0;
    while (path[n] && n + 1 < cap) {
        if (path[n] == '/') {
            last = n;
        }
        ++n;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    const u32 len = last > 0 ? last : n;
    std::memcpy(dst, path, len);
    dst[len] = 0;
}

bool find_and_load_tree_glb(const char* filename, TreeGlb* out) {
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
    const char* cands[12];
    char buf[12][768];
    u32 n = 0;
    auto add = [&](const char* fmt, const char* a) {
        if (n >= 12) {
            return;
        }
        std::snprintf(buf[n], sizeof(buf[n]), fmt, a, filename);
        cands[n] = buf[n];
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
        if (try_load_path(cands[i], out)) {
            return true;
        }
    }
    std::printf("[gl] tree glb not found: %s (drop it in assets/models/)\n", filename);
    std::fflush(stdout);
    return false;
}

void tree_glb_shutdown(TreeGlb* t) {
    if (!t) {
        return;
    }
    for (u32 i = 0; i < t->nprims; ++i) {
        if (t->prims[i].vao) {
            glDeleteVertexArrays(1, &t->prims[i].vao);
        }
        if (t->prims[i].vbo) {
            glDeleteBuffers(1, &t->prims[i].vbo);
        }
        if (t->prims[i].ibo) {
            glDeleteBuffers(1, &t->prims[i].ibo);
        }
        if (t->prims[i].tex) {
            glDeleteTextures(1, &t->prims[i].tex);
        }
        if (t->prims[i].tex_emit && t->prims[i].tex_emit != t->prims[i].tex) {
            glDeleteTextures(1, &t->prims[i].tex_emit);
        }
    }
    if (t->instance_vbo) {
        glDeleteBuffers(1, &t->instance_vbo);
    }
    std::memset(t, 0, sizeof(*t));
}

void tree_glb_set_instances(TreeGlb* t, const float* mats16, u32 count) {
    if (!t || !t->instance_vbo) {
        return;
    }
    if (count > kTreeInstanceCap) {
        count = kTreeInstanceCap;
    }
    t->instance_count = count;
    glBindBuffer(GL_ARRAY_BUFFER, t->instance_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(count * 16 * sizeof(float)), mats16);
}

} // namespace engine
