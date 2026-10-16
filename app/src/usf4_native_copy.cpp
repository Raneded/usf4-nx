// Ultra Street Fighter IV: the game's two memcpy routines run as the host's memcpy (CPU enhancement
// usf4_cpu_native_memcpy, 0.6.6).
//
// WHAT THE GAME DOES
//   Two copy routines of the Xbox 360 C library: sub_8220FFE8 (376 call sites; VMX path through sub_822140C0, word
//   path through sub_82514BD8) and sub_82514750 (2,119 call sites, a leaf). Both take r3 = destination, r4 = source,
//   r5 = bytes and return the destination in r3. They are written for the Xenon: dcbt/dcbzl cache hints, lvsl/vperm
//   realignment, byte loops for the edges. Recompiled, each guest instruction becomes host code with byte swaps and
//   per-instruction register spills. Switch 0.6.4, fights (2026-10-16): the main game thread 71 % of a core, of which
//   ~13 % in these routines and their helpers (sub_8220FFE8 7.1 %, sub_82514750 3.6 %, sub_822140C0 2.3 %); the job
//   workers spend 3-4 % in sub_82514750 as well.
//
// WHAT THIS DOES
//   Copies with the host's memcpy, byte for byte the same result: a memory copy moves bytes, and the guest's bytes are
//   stored as they are (big-endian) at base + address. Only when the two ranges do not overlap (what memcpy promises;
//   an overlapping call runs the original, so whatever order the original copies in is kept) and neither wraps past
//   4 GB. The return value is the destination, like the original. The cache hints have no visible effect.
//   A watched page (write or read fault) is handled as for the recompiled code: the host memcpy only uses
//   LDP/STP/LDR/STR/LDUR/STUR forms, which the Switch fault handler decodes (sdk/src/core/rex_decode.h).
//
// CHECKS
//   Each function's code is compared with the code this was written from (FNV-1a of its bytes) at its first call.
//   usf4_cpu_native_memcpy_audit (test): runs the original and then compares destination and source; a difference
//   (the routine is not a plain copy for some input) is counted and logged.
#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "cpu/sa2_enhancements.h"

SA2_ENHANCEMENT(g_native_memcpy, usf4_cpu_native_memcpy, sa2::enh::kCpu, true,
                "The game's memcpy routines (sub_8220FFE8, sub_82514750) run as the host's memcpy when source and "
                "destination do not overlap");
REXCVAR_DEFINE_BOOL(usf4_cpu_native_memcpy_audit, false, "SA2 enhancements",
                    "Test: the game's memcpy routines run as recompiled, and afterwards the destination is compared "
                    "with the source; differences are counted and logged")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

struct Routine {
  uint32_t address;
  uint32_t bytes;
  uint64_t fnv;
  const char* name;
  std::atomic<int> state{0};  // 0 unknown, 1 native, 2 audit, -1 original (code check failed or key off)
  std::atomic<uint64_t> calls{0}, audited{0}, differences{0}, overlapping{0};
};

Routine g_8220FFE8{0x8220FFE8, 760, 0x2F0222ADE966DC93ull, "usf4_cpu_native_memcpy (sub_8220FFE8)"};
Routine g_82514750{0x82514750, 1160, 0xEE0EAEB1E0584297ull, "usf4_cpu_native_memcpy (sub_82514750)"};

int StateOf(Routine& r, const uint8_t* base) {
  int state = r.state.load(std::memory_order_relaxed);
  if (state != 0) return state;
  if (!g_native_memcpy.active || !sa2::enh::GuestCodeMatches(base, r.address, r.bytes, r.fnv, r.name)) {
    state = -1;
  } else {
    state = REXCVAR_GET(usf4_cpu_native_memcpy_audit) ? 2 : 1;
    REXLOG_INFO("[enh] {}: {}", r.name, state == 2 ? "audit (original, then compared)" : "native");
  }
  r.state.store(state, std::memory_order_relaxed);
  return state;
}

bool Overlap(uint32_t dst, uint32_t src, uint32_t n) {
  return uint64_t(dst) < uint64_t(src) + n && uint64_t(src) < uint64_t(dst) + n;
}

bool Wraps(uint32_t dst, uint32_t src, uint32_t n) {
  return uint64_t(dst) + n > 0x100000000ull || uint64_t(src) + n > 0x100000000ull;
}

void Audit(Routine& r, uint8_t* base, uint32_t dst, uint32_t src, uint32_t n) {
  if (n == 0 || Wraps(dst, src, n)) return;
  if (Overlap(dst, src, n)) {
    r.overlapping.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const uint64_t audited = r.audited.fetch_add(1, std::memory_order_relaxed) + 1;
  if (std::memcmp(base + dst, base + src, n) != 0) {
    const uint64_t d = r.differences.fetch_add(1, std::memory_order_relaxed) + 1;
    if (d <= 32) {
      REXLOG_WARN("[enh] {} audit: destination {:08X} differs from source {:08X} after copying {} bytes", r.name,
                  dst, src, n);
    }
  }
  if ((audited & ((1u << 16) - 1)) == 0) {
    REXLOG_INFO("[enh] {} audit: {} copies compared, {} differences, {} overlapping (left to the original)", r.name,
                audited, r.differences.load(std::memory_order_relaxed),
                r.overlapping.load(std::memory_order_relaxed));
  }
}

// true = copied here; false = the caller runs the original.
inline bool Native(Routine& r, PPCContext& ctx, uint8_t* base) {
  const int state = StateOf(r, base);
  if (state != 1) return false;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32;
  if (Overlap(dst, src, n) || Wraps(dst, src, n)) return false;
  if (n) std::memcpy(base + dst, base + src, n);
  ctx.r3.u64 = dst;
  g_native_memcpy.UseSampled();
  return true;
}

}  // namespace

REX_EXTERN(__imp__sub_8220FFE8);
REX_HOOK_RAW(sub_8220FFE8) {
  if (Native(g_8220FFE8, ctx, base)) return;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32;
  __imp__sub_8220FFE8(ctx, base);
  if (g_8220FFE8.state.load(std::memory_order_relaxed) == 2) Audit(g_8220FFE8, base, dst, src, n);
}

REX_EXTERN(__imp__sub_82514750);
REX_HOOK_RAW(sub_82514750) {
  if (Native(g_82514750, ctx, base)) return;
  const uint32_t dst = ctx.r3.u32, src = ctx.r4.u32, n = ctx.r5.u32;
  __imp__sub_82514750(ctx, base);
  if (g_82514750.state.load(std::memory_order_relaxed) == 2) Audit(g_82514750, base, dst, src, n);
}
