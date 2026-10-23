// Ultra Street Fighter IV: one tile instead of predicated tiling (usf4_single_tile). See docs/usf4-notes.md,
// "Predicated tiling".
//
// At 720p the game draws its scene with 2x MSAA, which does not fit the Xbox 360's 10 MB of EDRAM, so Direct3D's
// predicated tiling replays every scene draw once per strip (0-544, 544-1088, 1088-1280): 45 % of all draws on the
// console were replays, and each strip adds EDRAM conversions. The native renderer draws those 2x targets with one
// sample anyway. sub_82971F68 picks the preset from the display height (720: preset 6 = 3 strips, MSAA index 1 =
// 2x); the game's own table also has preset 1, one 1280x720 tile, and MSAA index 0 is none.
#include <rex/cvar.h>
#include <rex/logging.h>

#include <bit>
#include <cstring>

#include <rex/ppc.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#if SA2_RENUT_RENDERER
#include "renut_engine/gpu/banjo_renut_select.h"
#endif

REXCVAR_DEFINE_BOOL(usf4_single_tile, false, "USF4",
                    "Draw the scene as one 1280x720 tile without MSAA instead of three predicated-tiling strips with "
                    "2x MSAA (the native renderer draws those with one sample anyway). Read when the game creates its "
                    "render targets")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Mid-function hook (app/overrides.toml, [[midasm_hook]] at 0x82972060; the generated code declares it): r14 = the tile
// preset (index into the table at 0x820DE8F0), r24 = the MSAA index (table 0x820DE654).
namespace {
bool g_single_tile = false;  // usf4_scene_tiling switched this run to one tile

// One tile with Renut's renderer whatever usf4_single_tile says: it does not take USF4's predicated tiling (its tiling
// hooks are N&B's, at N&B's device offsets), so only the first strip would reach the screen (docs/renut-census.md).
bool SingleTile() {
#if SA2_RENUT_RENDERER
  if (::banjo::renut_gpu::Active()) return true;
#endif
  return REXCVAR_GET(usf4_single_tile);
}
}  // namespace

void usf4_scene_tiling(PPCRegister& preset, PPCRegister& msaa) {
  static bool logged = false;
  const uint32_t before_preset = preset.u32, before_msaa = msaa.u32;
  if (SingleTile() && preset.u32 == 6) {
    preset.u64 = 1;  // 0x820DE8F8: {1, 0x820DE670} = 0,0-1280,720
    msaa.u64 = 0;    // D3DMULTISAMPLE_NONE
    g_single_tile = true;
  }
  if (!logged) {
    logged = true;
    REXLOG_INFO("[usf4] scene tiling: preset {} msaa {} (game's choice: preset {} msaa {}; usf4_single_tile = {})",
                preset.u32, msaa.u32, before_preset, before_msaa, SingleTile());
  }
}

// Mid-function hook at 0x829728B4 (same function, a second switch on the display mode): the size of the tile-local
// textures ("Main Tile Depth" and the others the scene resolves per strip). 720p: r26 = 544 (the strip width), r25 =
// 720, r29 = 1280 (the full width). With one tile they span the whole width.
void usf4_scene_tile_size(PPCRegister& tile_width, PPCRegister& full_width) {
  if (!g_single_tile || tile_width.u32 == full_width.u32) return;
  REXLOG_INFO("[usf4] scene tile textures: width {} -> {} (usf4_single_tile)", tile_width.u32, full_width.u32);
  tile_width.u64 = full_width.u64;
}

// Mid-function hook after 0x8296E85C (sub_8296E828's display-mode switch, the post-process object at r31): the screen
// strips the post-process quads cover, as floats copied from 0x821295A4 (720p: 3 rects 0-544, 544-1088, 1088-1632, all
// 720 high): count at +684, two floats at +688/+692, rects at +696, +712, +728. With one tile: one rect, the screen.
void usf4_post_tiles(PPCRegister& object) {
  if (!g_single_tile) return;
  auto* memory = rex::runtime::ThreadState::Get()->memory();
  uint8_t* o = memory->TranslateVirtual<uint8_t*>(object.u32);
  const auto load = [&](uint32_t at) { uint32_t v; std::memcpy(&v, o + at, 4); return __builtin_bswap32(v); };
  const auto store = [&](uint32_t at, uint32_t v) { v = __builtin_bswap32(v); std::memcpy(o + at, &v, 4); };
  const auto f = [&](uint32_t at) { return std::bit_cast<float>(load(at)); };
  REXLOG_INFO("[usf4] post-process strips: count {} ({:g}, {:g}) rect0 {:g},{:g}-{:g},{:g} -> 1 rect 0,0-1280,720 "
              "(usf4_single_tile)", load(684), f(688), f(692), f(696), f(700), f(704), f(708));
  if (load(684) != 3 || f(704) != 544.0f) return;  // only the 720p 3-strip layout
  store(684, 1);
  store(696, std::bit_cast<uint32_t>(0.0f));
  store(700, std::bit_cast<uint32_t>(0.0f));
  store(704, std::bit_cast<uint32_t>(1280.0f));
  store(708, std::bit_cast<uint32_t>(720.0f));
}
