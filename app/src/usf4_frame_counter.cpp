// Counts the frames the game presents and logs the rate every 5 s ("[fps] ..."), so test runs measure speed.
//
// sub_821FAA00 is the game's Direct3D swap: the only caller of the kernel's VdSwap (import thunk 0x82A6C924 in
// generated/default/usf4_init.cpp; found 2026-10-11), so it runs once per presented frame.

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(usf4_fps_log, true, "USF4", "Log the game's presented frames per second every 5 s");

std::atomic<uint64_t> g_usf4_game_frames{0};

namespace {

void Report() {
  using clock = std::chrono::steady_clock;
  static clock::time_point window_start = clock::now();
  static uint64_t window_frames = 0;
  static uint64_t worst_us = 0;
  static clock::time_point last = clock::now();
  const auto now = clock::now();
  const uint64_t dt = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - last).count());
  last = now;
  if (dt > worst_us) worst_us = dt;
  ++window_frames;
  const double elapsed = std::chrono::duration<double>(now - window_start).count();
  if (elapsed >= 5.0) {
    REXLOG_INFO("[fps] {:.1f} fps over {:.1f} s ({} frames), longest frame {:.1f} ms, total {}", window_frames / elapsed,
                elapsed, window_frames, worst_us / 1000.0, g_usf4_game_frames.load(std::memory_order_relaxed));
    window_start = now;
    window_frames = 0;
    worst_us = 0;
  }
}

}  // namespace

extern "C" REX_FUNC(__imp__sub_821FAA00);
REX_HOOK_RAW(sub_821FAA00) {
  __imp__sub_821FAA00(ctx, base);
  g_usf4_game_frames.fetch_add(1, std::memory_order_relaxed);
  if (REXCVAR_GET(usf4_fps_log)) Report();
}
