// Counts the frames the game presents and logs the rate every 5 s ("[fps] ..."), so test runs measure speed.
//
// sub_821FAA00 is the game's Direct3D swap: the only caller of the kernel's VdSwap (import thunk 0x82A6C924 in
// generated/default/usf4_init.cpp; found 2026-10-11), so it runs once per presented frame.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <cmath>
#include <cstdio>

#include <rex/ui/switch_sysclk.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/wait_watch.h>

#include "renut_engine/gpu/sa2_renut_yield.h"  // sa2_renderer = "renut": Renut's Swap
#include "sa2_capture.h"
#include "usf4_frame_probe.h"
#include "usf4_late_input.h"

REXCVAR_DEFINE_BOOL(usf4_fps_log, true, "USF4", "Log the game's presented frames per second every 5 s");
REXCVAR_DEFINE_INT32(usf4_hitch_log_ms, 25, "USF4",
                     "Frames longer than this (ms) get a \"[hitch]\" line: where the main thread's time went (GPU ring "
                     "waits, job waits, frame limiter, the rest = the game's own work and other waits). 0 = off");

REXCVAR_DEFINE_STRING(usf4_test_clock_override, "", "USF4",
                      "Tests only: \"CPU_MHZ,GPU_MHZ\" sets Horizon OC's CPU and GPU overrides at the first frame (the "
                      "memory clock is never touched); they stay after the game exits until set again. Empty: off");
REXCVAR_DEFINE_INT32(usf4_stall_report_s, 3, "USF4",
                     "When no frame has been presented for this many seconds, log what every guest thread is waiting "
                     "on (\"[waits]\", again every 10 s while it lasts). 0 = off");

std::atomic<uint64_t> g_usf4_game_frames{0};

extern "C" void RexSwitchPerfHitch(uint64_t start, uint64_t end);  // sdk switch_perf.cpp (PC: me_pc_stubs.cpp)

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
  static const uint64_t hitch_us = uint64_t(REXCVAR_GET(usf4_hitch_log_ms)) * 1000;
  uint64_t* w = usf4::probe::t_us;
  // usf4-nx (0.7.4): game frames over 30 ms are stack-sampling windows too (profiling builds; the renderer only
  // registers its own long Swap intervals, which a game-side hitch does not always make).
  if (dt > 30000) {
#if defined(__aarch64__)
    uint64_t end = 0;
    asm volatile("mrs %0, cntpct_el0" : "=r"(end));  // libnx's armGetSystemTick, 19.2 MHz
    const uint64_t length = dt * 192 / 10;
    RexSwitchPerfHitch(end > length ? end - length : 0, end);
#endif
  }
  if (hitch_us && dt >= hitch_us) {
    const uint64_t waits = w[usf4::probe::kGpu] + w[usf4::probe::kJobs] + w[usf4::probe::kLimiter];
    REXLOG_INFO("[hitch] frame {} took {:.1f} ms: waiting for the GPU ring {:.1f}, for jobs {:.1f}, frame limiter "
                "{:.1f}, the rest {:.1f}",
                g_usf4_game_frames.load(std::memory_order_relaxed), dt / 1000.0, w[usf4::probe::kGpu] / 1000.0,
                w[usf4::probe::kJobs] / 1000.0, w[usf4::probe::kLimiter] / 1000.0,
                (dt > waits ? dt - waits : 0) / 1000.0);
  }
  for (int i = 0; i < usf4::probe::kCount; ++i) w[i] = 0;
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

// usf4-nx (2026-10-23): the game thread stopped presenting at screen changes under Renut (Ryujinx) with no renderer
// wait reported. From the first frame on, a thread watches the frame count; when it stops for usf4_stall_report_s,
// wait_watch says what each guest thread waits on and from which game function.
// usf4-nx (2026-10-23, tests): usf4_test_clock_override = "CPU_MHZ,GPU_MHZ" sets Horizon OC's CPU and GPU overrides at
// the first frame (never the memory's). They stay after the game exits until set again.
void ApplyTestClockOverride() {
  static std::atomic<bool> done{false};
  const std::string spec = REXCVAR_GET(usf4_test_clock_override);
  if (spec.empty() || done.exchange(true)) return;
  double cpu = 0, gpu = 0;
  if (std::sscanf(spec.c_str(), "%lf,%lf", &cpu, &gpu) != 2 || cpu < 0 || gpu < 0 || cpu > 2400 || gpu > 1100) {
    REXLOG_WARN("[clocks] usf4_test_clock_override '{}': not CPU_MHZ,GPU_MHZ", spec);
    return;
  }
  const bool ok = rex::ui::switch_sysclk::SetCpuGpuOverride(uint32_t(std::lround(cpu * 1000.0)) * 1000u,
                                                          uint32_t(std::lround(gpu * 1000.0)) * 1000u);
  REXLOG_WARN("[clocks] usf4_test_clock_override: CPU {} MHz, GPU {} MHz: {}", cpu, gpu, ok ? "set" : "FAILED");
}

void StartStallWatch() {
  ApplyTestClockOverride();
  static std::atomic<bool> started{false};
  const int32_t limit_s = REXCVAR_GET(usf4_stall_report_s);
  if (limit_s <= 0 || started.exchange(true)) return;
  // Never joined nor destroyed (it watches for the whole session). Not detached: std::thread::detach throws on the
  // Switch (libnx's pthread_detach fails) and the throw ended the process at the first frame.
  new std::thread([limit_s] {
    uint64_t last = g_usf4_game_frames.load(std::memory_order_relaxed);
    int still_ms = 0;
    int next_report_ms = limit_s * 1000;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      const uint64_t now = g_usf4_game_frames.load(std::memory_order_relaxed);
      if (now != last) {
        if (still_ms >= limit_s * 1000)
          REXLOG_WARN("[waits] frames again after {:.1f} s (frame {})", still_ms / 1000.0, now);
        last = now;
        still_ms = 0;
        next_report_ms = limit_s * 1000;
        continue;
      }
      still_ms += 250;
      if (still_ms >= next_report_ms) {
        const std::string why = fmt::format("no frame for {:.1f} s (frame {})", still_ms / 1000.0, now);
        rex::system::wait_watch::Dump(why.c_str());
        next_report_ms += 10000;
      }
    }
  });
}

}  // namespace

extern "C" REX_FUNC(__imp__sub_821FAA00);
REX_HOOK_RAW(sub_821FAA00) {
  sa2::capture::BeforeFlip();  // sa2_video_lockstep: the recorder takes every frame (sa2-nx's recorder)
#if SA2_RENUT_RENDERER
  if (::banjo::renut_gpu::Active())
    renut_native_sub_821FAA00(ctx, base);  // Renut's Swap (banjo_shared_hooks.h); the frame is counted all the same
  else
#endif
    __imp__sub_821FAA00(ctx, base);
  g_usf4_game_frames.fetch_add(1, std::memory_order_relaxed);
  g_sa2_game_frames.fetch_add(1, std::memory_order_relaxed);
  usf4::late_input::OnFrame();  // usf4-nx (2026-10-23): the native renderer's late input
  StartStallWatch();
  if (REXCVAR_GET(usf4_fps_log)) Report();
}
