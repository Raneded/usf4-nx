// usf4-nx (2026-10-23): Switch button icons in place of the Xbox ones USF4 draws (usf4_button_icons). After the
// Mortal Kombat: Shaolin Monks port's (port/vk/button_icons.cpp). USF4's menus take each prompt's icon from a 32x32
// texture of its own (Scaleform images: the A, B and X balls, the RB key, the BACK button, as 8:8:8:8 or BC3 on the
// host), several copies of each. Both renderers call it: Renut (render/textures.cpp) and the native Vulkan one
// (me/native/sa2_native_draws.cpp). They are known by a hash of their host bytes and redrawn at upload, before the copy to
// the GPU. With input_xbox_layout (the default) buttons are mapped by position, so each icon becomes the Switch
// button in that place: Xbox A (bottom) is the Switch's B, B (right) its A, X (left) its Y; RB is R and BACK is -.
#pragma once

#include <cstdint>

namespace usf4::button_icons {

// The two layouts the icons come in on the host: 8:8:8:8 texels (alpha_byte says which byte of the four is alpha; the
// other three get the same grey) and BC3 blocks.
enum class Format { Rgba8, Bc3 };

// Whether a texture of this size and layout can be one of the icons: only those are hashed at upload.
bool Candidate(uint32_t width, uint32_t height, Format format);

// The texture's first level as uploaded ('row_pitch' bytes a row of texels or of 4x4 blocks): redrawn in place when it
// is one of the Xbox icons and usf4_button_icons is "switch". Returns whether it was.
// bc3_swap16: BC3 blocks with their 16-bit units byte-swapped (the native renderer swaps them after this point).
bool Paint(uint8_t *data, uint32_t width, uint32_t height, uint32_t row_pitch, Format format, uint32_t alpha_byte = 3,
           bool bc3_swap16 = false);

// Diagnostic (usf4_icon_dump_dir): a 32x32 candidate's level written once per content as <hash>_<tag>.bin, packed rows.
void Dump(const uint8_t *data, uint32_t width, uint32_t height, uint32_t row_pitch, Format format, const char *tag);

// FNV-1a 64 of the level's bytes without row padding (the icons' table key; for diagnostics).
uint64_t Hash(const uint8_t *data, uint32_t width, uint32_t height, uint32_t row_pitch, Format format);

}  // namespace usf4::button_icons
