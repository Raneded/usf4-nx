// Ultra Street Fighter IV hooks for the native renderer (src/me, sa2-nx's copy of masseffect-nx's renderer through
// banjo-nx), with USF4's addresses (docs/usf4-notes.md, "Direct3D": found by the PM4 packets each function writes and
// a diagnostic run over all Direct3D entry points; no reference game was available for tools/d3d_match.py).
//
// The renderer reads the game's PM4 ring by itself; these hooks only feed it what the ring cannot say:
//
//   - Shader objects. NoteShaderObject reads the container copy inside a D3D shader object and its microcode pointer.
//     USF4's draw-time shader load is sub_82200438 (r3 = device): it reads the bound VS from device+13072 and the PS
//     from device+13068 (the same layout as sa2-nx's 2012 XDK), so the objects are noted there, before the original
//     patches the VS for the vertex declaration.
//   - Draw records, a hint for pairing (NoteDrawCall: primitive type, then r5..r7 as D3D received them):
//       D3DDevice_DrawVertices          sub_821F4B88 (dev, type, start, count)
//       D3DDevice_DrawIndexedVertices   sub_821F4FA0 (dev, type, base, start, count)
//       D3DDevice_DrawVerticesUP        sub_821F40A0 (dev, type, count, data, stride)
//       D3DDevice_DrawIndexedVerticesUP sub_821F45A8 (dev, type, ...): SA2 has no such hook; noted the same way.
//   - Shader containers for the library (usf4_dump_shaders = true): the blob D3DDevice_CreateVertexShader
//     (sub_821E5D60, magic 0x102A1101) and D3DDevice_CreatePixelShader (sub_821E59B8, magic 0x102A1100) receive in r3.
//     It describes itself: [0] magic/flags, [1] header size (the microcode starts there), [2] microcode size. Written
//     whole to shaders/vs_<hash>.bin / ps_<hash>.bin next to the executable, once each; tools/build_shader_library.sh
//     turns the folder into the shader package.
//
// The direct renderer (sa2-nx's sa2_cpu_direct_state / direct_flush / api_* layers) is not ported: every hook here
// calls the original and changes no PPC register.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "me/me_native_system.h"
#include "renut_engine/gpu/sa2_renut_yield.h"  // sa2_renderer = "renut"

REXCVAR_DEFINE_BOOL(usf4_dump_shaders, false, "USF4",
                    "Write every shader container the game creates to shaders/ next to the executable "
                    "(vs_<hash>.bin / ps_<hash>.bin), for tools/build_shader_library.sh");

namespace {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

constexpr uint32_t kDeviceVertexShader = 13072;  // 0x3310 (sub_82200438: lwz r30,13072(r31))
constexpr uint32_t kDevicePixelShader = 13068;   // 0x330C (sub_82200438: lwz r29,13068(r31))

// The draw records (NoteDrawCall) read the bound shaders from the same device fields.
const bool g_device_layout_set = [] {
  me::native::SetDeviceShaderOffsets(kDeviceVertexShader, kDevicePixelShader);
  return true;
}();

std::mutex g_dump_mutex;
std::unordered_set<uint64_t> g_dumped;

void DumpShaderBlob(const uint8_t* base, uint32_t blob_address, bool vertex) {
  if (!REXCVAR_GET(usf4_dump_shaders) || !blob_address) {
    return;
  }
  const uint32_t header = Load32(base, blob_address + 4);
  const uint32_t ucode = Load32(base, blob_address + 8);
  if (header < 12 || header > (1u << 20) || !ucode || ucode > (1u << 20)) {
    REXLOG_WARN("[shaders] implausible {} blob at {:08X} (header {}, microcode {}), skipped", vertex ? "VS" : "PS",
                blob_address, header, ucode);
    return;
  }
  const uint8_t* blob = base + blob_address;
  const size_t size = size_t(header) + ucode;
  const uint64_t hash = XXH3_64bits(blob, size);
  {
    std::lock_guard lock(g_dump_mutex);
    if (!g_dumped.insert(hash).second) {
      return;
    }
  }
  const auto folder = rex::filesystem::GetExecutableFolder() / "shaders";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const auto path = folder / fmt::format("{}_{:016x}.bin", vertex ? "vs" : "ps", hash);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(blob), std::streamsize(size));
}

}  // namespace

// Draw-time shader load (the third call of every Draw*'s state flush).
REX_EXTERN(__imp__sub_82200438);
REX_HOOK_RAW(sub_82200438) {
  SA2_RENUT_YIELD(sub_82200438)
  const uint32_t device = ctx.r3.u32;
  if (device >= 0x40000000 && device < 0x7F000000) {
    const uint32_t vs = Load32(base, device + kDeviceVertexShader), ps = Load32(base, device + kDevicePixelShader);
    if (!me::native::ShaderObjectsFinished(vs, ps)) {
      me::native::NoteShaderObject(base, vs, true);
      me::native::NoteShaderObject(base, ps, false);
    }
  }
  __imp__sub_82200438(ctx, base);
}

// yield: what the hook does instead with sa2_renderer = "renut" (sa2_renut_yield.h).
#define USF4_DRAW_HOOK(addr, name, yield)                                                                  \
  REX_EXTERN(__imp__sub_##addr);                                                                          \
  REX_HOOK_RAW(sub_##addr) { /* name */                                                                   \
    yield(sub_##addr)                                                                                     \
    me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, nullptr); \
    __imp__sub_##addr(ctx, base);                                                                         \
  }

USF4_DRAW_HOOK(821F4B88, D3DDevice_DrawVertices, SA2_RENUT_YIELD_SHARED)
USF4_DRAW_HOOK(821F4FA0, D3DDevice_DrawIndexedVertices, SA2_RENUT_YIELD_SHARED)
// These two are BeginVertices and BeginIndexedVertices, the inline draws' first half (docs/renut-census.md; called
// from DrawVerticesUP sub_821F4560 and DrawIndexedVerticesUP sub_821F4AE8). Renut hooks BeginVertices, and replaces
// DrawIndexedVerticesUP whole.
USF4_DRAW_HOOK(821F40A0, D3DDevice_DrawVerticesUP, SA2_RENUT_YIELD_SHARED)
USF4_DRAW_HOOK(821F45A8, D3DDevice_DrawIndexedVerticesUP, SA2_RENUT_YIELD)

REX_EXTERN(__imp__sub_821E5D60);
REX_HOOK_RAW(sub_821E5D60) {  // D3DDevice_CreateVertexShader (r3 = the container blob)
  DumpShaderBlob(base, ctx.r3.u32, true);
  __imp__sub_821E5D60(ctx, base);
}

REX_EXTERN(__imp__sub_821E59B8);
REX_HOOK_RAW(sub_821E59B8) {  // D3DDevice_CreatePixelShader (r3 = the container blob)
  DumpShaderBlob(base, ctx.r3.u32, false);
  __imp__sub_821E59B8(ctx, base);
}
