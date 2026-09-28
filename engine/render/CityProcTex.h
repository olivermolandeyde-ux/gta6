#pragma once

#include "core/Types.h"

namespace engine {

inline constexpr u32 kProcTexSize = 512;

void proc_tex_brick(u8* rgba, u8* normal_rgb);
void proc_tex_concrete(u8* rgba, u8* normal_rgb);
void proc_tex_asphalt(u8* rgba, u8* normal_rgb);
void proc_tex_bark(u8* rgba);
void proc_tex_leaf(u8* rgba); // RGBA with alpha

unsigned gl_upload_rgba(const u8* rgba, u32 w, u32 h, bool mip);
unsigned gl_upload_rgb(const u8* rgb, u32 w, u32 h);

} // namespace engine
