#pragma once

#include "core/Types.h"

#include <cmath>

namespace engine {

struct SolidVert {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

inline void solid_push(SolidVert* v, u32* n, float x, float y, float z, float nx, float ny, float nz,
                       float u, float vv) {
    SolidVert& o = v[*n];
    o.px = x;
    o.py = y;
    o.pz = z;
    o.nx = nx;
    o.ny = ny;
    o.nz = nz;
    o.u = u;
    o.v = vv;
    ++(*n);
}

// Unit tapered cylinder along +Y, base at y=0, r_base=1, r_top=taper, h=1.
inline void mesh_taper_cyl(SolidVert* v, u32* vn, u32* idx, u32* in, u32 segs, float taper) {
    *vn = 0;
    *in = 0;
    for (u32 i = 0; i <= segs; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segs);
        const float a = t * 6.2831853f;
        const float c = std::cos(a);
        const float s = std::sin(a);
        const float nrm = std::sqrt(1.f + (1.f - taper) * (1.f - taper));
        solid_push(v, vn, c, 0.f, s, c / nrm, (1.f - taper) / nrm, s / nrm, t, 0.f);
        solid_push(v, vn, c * taper, 1.f, s * taper, c / nrm, (1.f - taper) / nrm, s / nrm, t, 1.f);
    }
    for (u32 i = 0; i < segs; ++i) {
        const u32 a = i * 2;
        idx[(*in)++] = a;
        idx[(*in)++] = a + 1;
        idx[(*in)++] = a + 2;
        idx[(*in)++] = a + 1;
        idx[(*in)++] = a + 3;
        idx[(*in)++] = a + 2;
    }
}

inline void mesh_uv_sphere(SolidVert* v, u32* vn, u32* idx, u32* in, u32 stacks, u32 slices) {
    *vn = 0;
    *in = 0;
    for (u32 y = 0; y <= stacks; ++y) {
        const float vj = static_cast<float>(y) / static_cast<float>(stacks);
        const float phi = vj * 3.14159265f;
        const float sy = std::cos(phi);
        const float r = std::sin(phi);
        for (u32 x = 0; x <= slices; ++x) {
            const float ui = static_cast<float>(x) / static_cast<float>(slices);
            const float th = ui * 6.2831853f;
            const float cx = std::cos(th) * r;
            const float cz = std::sin(th) * r;
            solid_push(v, vn, cx, sy, cz, cx, sy, cz, ui, vj);
        }
    }
    const u32 stride = slices + 1;
    for (u32 y = 0; y < stacks; ++y) {
        for (u32 x = 0; x < slices; ++x) {
            const u32 i = y * stride + x;
            idx[(*in)++] = i;
            idx[(*in)++] = i + stride;
            idx[(*in)++] = i + 1;
            idx[(*in)++] = i + 1;
            idx[(*in)++] = i + stride;
            idx[(*in)++] = i + stride + 1;
        }
    }
}

inline void mesh_cone(SolidVert* v, u32* vn, u32* idx, u32* in, u32 segs) {
    *vn = 0;
    *in = 0;
    solid_push(v, vn, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 1.f);
    for (u32 i = 0; i <= segs; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segs);
        const float a = t * 6.2831853f;
        const float c = std::cos(a);
        const float s = std::sin(a);
        solid_push(v, vn, c, 0.f, s, c, 0.35f, s, t, 0.f);
    }
    for (u32 i = 0; i < segs; ++i) {
        idx[(*in)++] = 0;
        idx[(*in)++] = i + 1;
        idx[(*in)++] = i + 2;
    }
}

} // namespace engine
