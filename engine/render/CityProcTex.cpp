#include "render/CityProcTex.h"

#include <cmath>
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

[[nodiscard]] float hash2(float x, float y) {
    float n = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return n - std::floor(n);
}

[[nodiscard]] u8 u8c(float x) {
    x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
    return static_cast<u8>(x * 255.f + 0.5f);
}

void pack_rgba(u8* p, u32 i, float r, float g, float b, float a) {
    p[i * 4] = u8c(r);
    p[i * 4 + 1] = u8c(g);
    p[i * 4 + 2] = u8c(b);
    p[i * 4 + 3] = u8c(a);
}

void pack_rgb(u8* p, u32 i, float r, float g, float b) {
    p[i * 3] = u8c(r);
    p[i * 3 + 1] = u8c(g);
    p[i * 3 + 2] = u8c(b);
}

void height_to_normal(const float* h, u8* nrm, u32 w) {
    for (u32 y = 0; y < w; ++y) {
        for (u32 x = 0; x < w; ++x) {
            const float hl = h[y * w + (x == 0 ? w - 1 : x - 1)];
            const float hr = h[y * w + (x + 1) % w];
            const float hd = h[(y == 0 ? w - 1 : y - 1) * w + x];
            const float hu = h[((y + 1) % w) * w + x];
            float nx = (hl - hr) * 6.f;
            float ny = (hd - hu) * 6.f;
            float nz = 1.f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;
            pack_rgb(nrm, y * w + x, nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f);
        }
    }
}

} // namespace

void proc_tex_brick(u8* rgba, u8* normal_rgb) {
    constexpr u32 N = kProcTexSize;
    static float h[kProcTexSize * kProcTexSize];
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            const u32 row = y / 28;
            const u32 shift = (row & 1u) ? 32u : 0u;
            const u32 bx = (x + shift) % 64;
            const u32 by = y % 28;
            const bool mortar = bx < 5 || by < 4;
            const float n = hash2(static_cast<float>((x + shift) / 64), static_cast<float>(row));
            float r = 0.42f + n * 0.22f;
            float g = 0.18f + n * 0.08f;
            float b = 0.12f + n * 0.04f;
            if (mortar) {
                r = 0.55f;
                g = 0.50f;
                b = 0.45f;
            }
            r *= 0.92f + 0.08f * hash2(static_cast<float>(x), static_cast<float>(y));
            pack_rgba(rgba, y * N + x, r, g, b, 1.f);
            h[y * N + x] = mortar ? 0.15f : 0.85f + n * 0.1f;
        }
    }
    height_to_normal(h, normal_rgb, N);
}

void proc_tex_concrete(u8* rgba, u8* normal_rgb) {
    constexpr u32 N = kProcTexSize;
    static float h[kProcTexSize * kProcTexSize];
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            const float n = hash2(static_cast<float>(x) * 0.17f, static_cast<float>(y) * 0.13f);
            const float n2 = hash2(static_cast<float>(x) * 0.03f, static_cast<float>(y) * 0.04f);
            float g = 0.58f + n * 0.10f - n2 * 0.06f;
            if (hash2(static_cast<float>(x) * 0.4f, static_cast<float>(y) * 0.9f) > 0.985f) {
                g *= 0.7f;
            }
            pack_rgba(rgba, y * N + x, g, g * 0.99f, g * 0.96f, 1.f);
            h[y * N + x] = n * 0.4f + n2 * 0.3f;
        }
    }
    height_to_normal(h, normal_rgb, N);
}

void proc_tex_asphalt(u8* rgba, u8* normal_rgb) {
    constexpr u32 N = kProcTexSize;
    static float h[kProcTexSize * kProcTexSize];
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            const float n = hash2(static_cast<float>(x) * 0.9f, static_cast<float>(y) * 1.1f);
            const float n2 = hash2(static_cast<float>(x) * 0.07f, static_cast<float>(y) * 0.09f);
            float g = 0.16f + n * 0.10f;
            if (n2 > 0.82f) {
                g *= 0.55f;
            }
            pack_rgba(rgba, y * N + x, g, g, g * 0.98f, 1.f);
            h[y * N + x] = n;
        }
    }
    height_to_normal(h, normal_rgb, N);
}

void proc_tex_bark(u8* rgba) {
    constexpr u32 N = kProcTexSize;
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            const float v = hash2(static_cast<float>(x) * 0.35f, static_cast<float>(y) * 0.02f);
            const float r = 0.28f + v * 0.18f;
            pack_rgba(rgba, y * N + x, r, r * 0.62f, r * 0.38f, 1.f);
        }
    }
}

void proc_tex_leaf(u8* rgba) {
    constexpr u32 N = kProcTexSize;
    const float cx = 0.5f;
    const float cy = 0.5f;
    for (u32 y = 0; y < N; ++y) {
        for (u32 x = 0; x < N; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(N);
            const float v = static_cast<float>(y) / static_cast<float>(N);
            const float dx = (u - cx) * 2.f;
            const float dy = (v - cy) * 2.f;
            float d = std::sqrt(dx * dx + dy * dy);
            d += 0.15f * hash2(u * 8.f, v * 8.f);
            float a = d < 0.92f ? 1.f : 0.f;
            if (hash2(u * 18.f, v * 17.f) > 0.78f && d > 0.35f) {
                a = 0.f;
            }
            const float g = 0.18f + 0.35f * hash2(u * 6.f, v * 5.f);
            pack_rgba(rgba, y * N + x, 0.10f, g, 0.08f, a);
        }
    }
}

unsigned gl_upload_rgba(const u8* rgba, u32 w, u32 h, bool mip) {
    unsigned id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (mip) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    return id;
}

unsigned gl_upload_rgb(const u8* rgb, u32 w, u32 h) {
    unsigned id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGB,
                 GL_UNSIGNED_BYTE, rgb);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    return id;
}

} // namespace engine
