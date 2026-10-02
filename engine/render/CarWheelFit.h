#pragma once

#include "core/Types.h"

#include <cmath>
#include <cstring>

namespace engine {

// Wheel geometry for car GLBs. Header-only and free of OpenGL on purpose: the fitting
// maths is the part that cannot be eyeballed from a screenshot, so the sandbox exercises
// it headlessly with synthetic wheel clouds.
//
// A wheel is a disc: one AABB axis (the axle) is clearly thinner than the two that span
// the wheel plane, and those two are about equal. A primitive holding several wheels is
// an arrangement of such discs along one or two axes, which is found by splitting while
// the cloud stays elongated and re-testing each half. That handles a primitive per wheel,
// a primitive per axle and one primitive holding all four.

struct CarWheelSet {
    int   count;     // 1..4 wheels found in this primitive
    int   axle_axis; // model-space axis index of the rotation axis
    float center[4][3];
    float radius[4]; // model units
};

namespace car_wheel_detail {

struct Range {
    u32 start;
    u32 len;
};

inline void range_extents(const float* pts, u32 stride, const u32* idx, u32 start, u32 len,
                          float mn[3], float mx[3]) {
    mn[0] = mn[1] = mn[2] = 1.0e30f;
    mx[0] = mx[1] = mx[2] = -1.0e30f;
    for (u32 i = 0; i < len; ++i) {
        const float* p = pts + static_cast<usize>(idx[start + i]) * stride;
        for (int k = 0; k < 3; ++k) {
            if (p[k] < mn[k]) {
                mn[k] = p[k];
            }
            if (p[k] > mx[k]) {
                mx[k] = p[k];
            }
        }
    }
}

inline int argmax3(const float e[3]) {
    int a = 0;
    if (e[1] > e[a]) {
        a = 1;
    }
    if (e[2] > e[a]) {
        a = 2;
    }
    return a;
}

inline int argmax3_except(const float e[3], int skip) {
    int a = -1;
    for (int k = 0; k < 3; ++k) {
        if (k == skip) {
            continue;
        }
        if (a < 0 || e[k] > e[a]) {
            a = k;
        }
    }
    return a;
}

inline int argmin3(const float e[3]) {
    int a = 0;
    if (e[1] < e[a]) {
        a = 1;
    }
    if (e[2] < e[a]) {
        a = 2;
    }
    return a;
}

} // namespace car_wheel_detail

// Fits wheels into one primitive's vertex cloud (assembled model space, i.e. already
// through the glTF node transform — the same space the vertex shader sees). `stride` is
// the distance between vertices in floats, so an interleaved vertex buffer can be passed
// as-is. `scratch` must hold at least n u32 entries. Returns false when the cloud does not
// look like wheels at all, so a bumper or a spoiler can never be spun.
[[nodiscard]] inline bool car_wheel_fit(const float* pts, u32 stride, u32 n, u32* scratch,
                                        CarWheelSet* out) {
    if (!pts || !scratch || !out || n < 12u || stride < 3u) {
        return false;
    }
    *out = CarWheelSet{0, 0, {{0.f}}, {0.f}};
    for (u32 i = 0; i < n; ++i) {
        scratch[i] = i;
    }

    car_wheel_detail::Range stack[8];
    car_wheel_detail::Range clusters[4];
    u32                      sp = 0;
    u32                      nc = 0;
    stack[sp++] = car_wheel_detail::Range{0u, n};

    while (sp > 0u) {
        const car_wheel_detail::Range r = stack[--sp];
        float mn[3], mx[3];
        car_wheel_detail::range_extents(pts, stride, scratch, r.start, r.len, mn, mx);
        const float e[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
        const int   big  = car_wheel_detail::argmax3(e);
        const int   snd  = car_wheel_detail::argmax3_except(e, big);
        const bool  elongated = e[snd] > 1.0e-6f && e[big] > 1.8f * e[snd];

        if (!elongated || nc + sp + 1u >= 4u) {
            if (nc < 4u) {
                clusters[nc++] = r;
            }
            continue;
        }
        // Split the range in place along its elongation axis at the mid-plane.
        const float mid = 0.5f * (mn[big] + mx[big]);
        u32         i   = r.start;
        u32         j   = r.start + r.len;
        while (i < j) {
            const float* p = pts + static_cast<usize>(scratch[i]) * stride;
            if (p[big] < mid) {
                ++i;
            } else {
                --j;
                const u32 t = scratch[i];
                scratch[i] = scratch[j];
                scratch[j] = t;
            }
        }
        const u32 left  = i - r.start;
        const u32 right = r.len - left;
        if (left < 6u || right < 6u) {
            if (nc < 4u) {
                clusters[nc++] = r; // too small to be a ring of wheels: keep it whole
            }
            continue;
        }
        stack[sp++] = car_wheel_detail::Range{r.start, left};
        stack[sp++] = car_wheel_detail::Range{r.start + left, right};
    }

    if (nc == 0u || nc > 4u) {
        return false;
    }

    int axle_ref = -1;
    for (u32 c = 0; c < nc; ++c) {
        const car_wheel_detail::Range r = clusters[c];
        float mn[3], mx[3];
        car_wheel_detail::range_extents(pts, stride, scratch, r.start, r.len, mn, mx);
        const float e[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
        const int   axle = car_wheel_detail::argmin3(e);
        const int   ra0  = car_wheel_detail::argmax3(e);
        const int   ra1  = car_wheel_detail::argmax3_except(e, ra0);
        if (ra1 < 0 || e[ra0] < 1.0e-6f || e[ra1] < 1.0e-6f) {
            return false;
        }
        // A wheel is thin along its axle and round in its plane.
        if (e[axle] > 0.75f * e[ra1]) {
            return false;
        }
        if (e[ra0] > 1.6f * e[ra1]) {
            return false;
        }
        if (axle_ref < 0) {
            axle_ref = axle;
        } else if (axle_ref != axle) {
            return false; // wheels in one primitive must share an axle axis
        }
        float ctr[3] = {0.5f * (mn[0] + mx[0]), 0.5f * (mn[1] + mx[1]), 0.5f * (mn[2] + mx[2])};
        float rad    = 0.f;
        for (u32 i = 0; i < r.len; ++i) {
            const float* p = pts + static_cast<usize>(scratch[r.start + i]) * stride;
            float        d2 = 0.f;
            for (int k = 0; k < 3; ++k) {
                if (k == axle) {
                    continue;
                }
                const float d = p[k] - ctr[k];
                d2 += d * d;
            }
            const float d = std::sqrt(d2);
            if (d > rad) {
                rad = d;
            }
        }
        if (rad < 1.0e-6f) {
            return false;
        }
        out->center[c][0] = ctr[0];
        out->center[c][1] = ctr[1];
        out->center[c][2] = ctr[2];
        out->radius[c]    = rad;
    }
    out->count     = static_cast<int>(nc);
    out->axle_axis = axle_ref;
    return true;
}

// Does a fitted disc stand on the model floor? A wheel touches the road; headlights,
// mirrors, exhaust tips and a spare tyre in the boot all float above it. `up` is the
// model's height axis, so `center_up - radius` is the lowest point of the disc.
[[nodiscard]] inline bool car_wheel_on_ground(float center_up, float radius, float floor_up,
                                              float model_height, float tol_frac = 0.08f) {
    if (model_height <= 1.0e-6f) {
        return false;
    }
    const float bottom = center_up - radius;
    const float tol    = tol_frac * model_height;
    return std::fabs(bottom - floor_up) <= tol;
}

// Name tests for wheel detection, as plain C strings, so the sandbox can run real asset
// node names through exactly this code without a GLB or a JSON DOM. Both are
// case-insensitive substring tests, like the rest of the loader's naming rules, with one
// extra guard: a keyword that is glued to the end of another word only counts after
// "wheel" or "hjul". Without it "wheel_trim" would be read as a rim — a substring test for
// "rim" happily matches "tRIM" — and a wheel arch trim would start spinning.
namespace car_wheel_detail {

inline bool has_icase_exact(const char* at, const char* word) {
    for (unsigned k = 0; word[k]; ++k) {
        char a = at[k];
        char b = word[k];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a + 32);
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b + 32);
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

inline bool has_icase(const char* hay, const char* needle) {
    const unsigned nl = static_cast<unsigned>(std::strlen(hay));
    const unsigned sl = static_cast<unsigned>(std::strlen(needle));
    if (sl == 0u || sl > nl) {
        return false;
    }
    for (unsigned i = 0; i + sl <= nl; ++i) {
        bool ok = true;
        for (unsigned k = 0; k < sl; ++k) {
            char a = hay[i + k];
            char b = needle[k];
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<char>(a + 32);
            }
            if (b >= 'A' && b <= 'Z') {
                b = static_cast<char>(b + 32);
            }
            if (a != b) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        if (i == 0u || !((hay[i - 1u] >= 'a' && hay[i - 1u] <= 'z') ||
                         (hay[i - 1u] >= 'A' && hay[i - 1u] <= 'Z'))) {
            return true; // start of the name, or after a separator: a word of its own
        }
        // Glued to a letter: only a compound such as "wheelbrake" or "wheeltire" counts.
        const unsigned pre = i;
        if (pre >= 5u && has_icase_exact(hay + pre - 5u, "wheel")) {
            return true;
        }
        if (pre >= 4u && has_icase_exact(hay + pre - 4u, "hjul")) {
            return true;
        }
    }
    return false;
}


inline const char* const* wheel_words(unsigned* count) {
    static const char* const kW[] = {"wheel", "hjul",  "tire",  "tyre",    "rubber",  "tread",
                                     "rim",   "felg",  "alloy", "hubcap",  "dek",     "brake",
                                     "bremse", "rotor", "caliper", "kaliper"};
    *count = static_cast<unsigned>(sizeof(kW) / sizeof(kW[0]));
    return kW;
}

inline const char* const* bodywork_words(unsigned* count) {
    static const char* const kW[] = {"arch", "bue",   "well",   "trim",  "liner", "mud",
                                     "flap", "skirt", "fender", "skjerm", "sill", "rocker"};
    *count = static_cast<unsigned>(sizeof(kW) / sizeof(kW[0]));
    return kW;
}

inline const char* const* hardware_words(unsigned* count) {
    static const char* const kW[] = {"brake", "bremse", "rim",   "felg", "alloy", "disc",
                                     "disk",  "rotor",  "spoke", "eike", "hub",   "caliper",
                                     "kaliper", "bolt"};
    *count = static_cast<unsigned>(sizeof(kW) / sizeof(kW[0]));
    return kW;
}

} // namespace car_wheel_detail

// Does the name say the part is part of a wheel? Deliberately not "brake" alone: a brake
// disc rotates but a caliper does not, so the geometry test always has the final word.
// A wheel arch, a wheel well and a wheel trim are spelled with "wheel" but they are
// bodywork bolted to the shell, and they must never spin. A name that names bodywork is
// disqualified whatever else it says.
[[nodiscard]] inline bool car_wheel_name_is_bodywork(const char* name) {
    if (!name || !*name) {
        return false;
    }
    unsigned              count = 0u;
    const char* const* const words = car_wheel_detail::bodywork_words(&count);
    for (unsigned i = 0; i < count; ++i) {
        if (car_wheel_detail::has_icase(name, words[i])) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool car_wheel_name_looks_like_wheel(const char* name) {
    if (!name || !*name || car_wheel_name_is_bodywork(name)) {
        return false;
    }
    unsigned              count = 0u;
    const char* const* const words = car_wheel_detail::wheel_words(&count);
    for (unsigned i = 0; i < count; ++i) {
        if (car_wheel_detail::has_icase(name, words[i])) {
            return true;
        }
    }
    return false;
}

// Names that can only be wheel hardware — a brake, a disc, a rotor, a rim, spokes, the hub.
// A part with such a name may be a little larger and still be safe to spin with the tyre,
// because a caliper makes a brake assembly wider than the disc it grips.
[[nodiscard]] inline bool car_wheel_name_is_hardware(const char* name) {
    if (!name || !*name || car_wheel_name_is_bodywork(name)) {
        return false;
    }
    unsigned              count = 0u;
    const char* const* const words = car_wheel_detail::hardware_words(&count);
    for (unsigned i = 0; i < count; ++i) {
        if (car_wheel_detail::has_icase(name, words[i])) {
            return true;
        }
    }
    return false;
}

// Which fitted wheel does a primitive belong to, when the strict disc test refused it?
//
// A brake disc with its caliper, a rim with bolt heads, a hub cap and an upright are all
// part of the wheel assembly, but they are not clean discs — a caliper sticking out, hex
// bolts, a strut — and car_wheel_fit rejects exactly that on purpose, because it must
// never spin a bumper. Such a primitive can still be spun safely when it is small and
// concentric with a wheel whose disc test did pass: then it belongs to that wheel and has
// to turn with it, at the same angle as the tyre.
//
// Returns the index of the fitted wheel to spin it about, or -1 to leave it static.
// `centers` holds three floats per fitted wheel, `radii` one.
inline constexpr float kAttachOffsetHardware = 1.00f; // of the wheel radius
inline constexpr float kAttachExtentHardware = 5.00f;
inline constexpr float kAttachOffsetGeneric  = 0.25f; // a part named just "wheel" must be
inline constexpr float kAttachExtentGeneric  = 1.60f; // dead concentric and no larger than a disc

// `max_offset_frac` and `max_extent_frac` are fractions of the wheel radius: how far the
// part's centre may sit from the wheel centre, and how large the part may be. Wheel
// hardware (a brake with its caliper) gets the looser pair, anything else the tight one.
[[nodiscard]] inline int car_wheel_attach_to(const float prim_center[3], const float prim_extent[3],
                                             const float* centers, const float* radii, u32 n,
                                             float max_offset_frac = kAttachOffsetGeneric,
                                             float max_extent_frac = kAttachExtentGeneric) {
    if (!prim_center || !prim_extent || !centers || !radii || n == 0u) {
        return -1;
    }
    int   best   = -1;
    float best_d = 0.f;
    for (u32 i = 0; i < n; ++i) {
        if (radii[i] <= 1.0e-6f) {
            continue;
        }
        const float dx = prim_center[0] - centers[3u * i + 0u];
        const float dy = prim_center[1] - centers[3u * i + 1u];
        const float dz = prim_center[2] - centers[3u * i + 2u];
        const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
        // Concentric, or a caliper's worth of offset — never a body panel down the street.
        if (d <= max_offset_frac * radii[i] && (best < 0 || d < best_d)) {
            best   = static_cast<int>(i);
            best_d = d;
        }
    }
    if (best < 0) {
        return -1;
    }
    // And small enough to be one wheel's worth of parts. A whole underside, a fender, a
    // wheel arch or a sill spans metres and would be dragged around by the rotation.
    float ext = prim_extent[0];
    for (int k = 1; k < 3; ++k) {
        if (prim_extent[k] > ext) {
            ext = prim_extent[k];
        }
    }
    if (ext > max_extent_frac * radii[best]) {
        return -1;
    }
    return best;
}

// Sign that turns a forward-travelling angle into the model-space spin direction.
//
// While the car drives forward, the contact patch — the bottom of the tyre, the part that
// touches the road — has to move backwards relative to the car. That is rolling; if it
// moved forwards the wheels would visibly skid.
//
// Let A be the model→world map the renderer uses (the AABB basis, then the heading yaw).
// Because that map is orthogonal up to a positive scale, A preserves dot products, so the
// test "does the patch move backwards along the nose" can be answered in model space,
// where the nose is the model's forward axis f and the patch offset from the wheel centre
// is minus the up axis u. Spinning about the axle a by a positive angle moves the patch
// along a x (-u), so rolling needs
//
//     dot( a x (-u), f ) < 0   <=>   dot( a, f x u ) > 0,
//
// and the angle therefore has to grow in the negative direction for a positive spin.
// This is exact and needs no yaw, no body offset and no fitted radii: the yaw cancels.
[[nodiscard]] inline float car_wheel_roll_dir(int fwd_axis, int up_axis, int axle_axis) {
    const float e[3][3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
    const float* f = e[fwd_axis & 3];
    const float* u = e[up_axis & 3];
    const float  r[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2],
                         f[0] * u[1] - f[1] * u[0]};
    const float* a = e[axle_axis & 3];
    const float  d = a[0] * r[0] + a[1] * r[1] + a[2] * r[2];
    // d is +-1 for a sane car (the axle is the third axis). Anything else means the
    // fitted "wheel" shares an axis with the length or the height, which the geometry
    // test above already rejects, so the sign there is immaterial.
    return d > 0.f ? -1.f : 1.f;
}

} // namespace engine
