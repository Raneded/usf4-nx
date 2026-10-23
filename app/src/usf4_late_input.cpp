// usf4-nx (2026-10-23): see usf4_late_input.h.
#include "usf4_late_input.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/input/read_hook.h>
#include <rex/logging.h>
#include <rex/platform.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(usf4_late_input, true, "USF4",
                    "Native renderer: the frame's first controller read waits for as long as the game's frame limiter "
                    "would have waited at the end of the frame (the least of the last 16 frames, less 2 ms and "
                    "usf4_late_input_limiter_margin_us), so the frame starts with newer input and still makes its "
                    "Swap. A [late input] line every 10 s says how much. Renut has its own (banjo_late_input)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace usf4::late_input {
namespace {

constexpr uint32_t kFrames = 16;
constexpr uint64_t kMarginUs = 2000;

using Clock = std::chrono::steady_clock;

std::atomic<uint32_t> g_limiter_slack_us{0};  // the frame's largest (NoteLimiterSlack)
std::atomic<uint32_t> g_applied_us{0};         // the delay the frame's first read had
std::atomic<uint32_t> g_next_delay_us{0};      // the delay for the next frame's first read
std::atomic<bool> g_read_pending{false};       // the next frame's first read has not come yet

// Game thread only (OnFrame).
uint64_t g_slack[kFrames] = {};
uint32_t g_slack_next = 0, g_slack_count = 0;
uint64_t g_interval_us = 0;
Clock::time_point g_last_swap{}, g_report_start{};
uint64_t g_delay_total_us = 0, g_delay_frames = 0, g_overruns = 0;

int g_state = 0;  // 0 not decided, 1 on, -1 off

void BeforeRead() {
  if (!g_read_pending.exchange(false, std::memory_order_acq_rel)) return;
  const uint32_t delay = g_next_delay_us.load(std::memory_order_relaxed);
  if (delay) {
#if REX_PLATFORM_SWITCH
    svcSleepThread(int64_t(delay) * 1000);
#else
    std::this_thread::sleep_for(std::chrono::microseconds(delay));
#endif
  }
  g_applied_us.store(delay, std::memory_order_relaxed);
}

}  // namespace

void NoteLimiterSlack(uint32_t us) {
  uint32_t seen = g_limiter_slack_us.load(std::memory_order_relaxed);
  while (us > seen && !g_limiter_slack_us.compare_exchange_weak(seen, us, std::memory_order_relaxed)) {
  }
}

void OnFrame() {
  if (g_state == 0) {
    // Renut takes the limiter's wait into its own late input; the read hook is one function for the whole process.
    g_state = REXCVAR_GET(usf4_late_input) && rex::cvar::GetFlagByName("sa2_renderer") != "renut" ? 1 : -1;
    if (g_state > 0) {
      rex::input::SetReadHook(&BeforeRead);
      REXLOG_INFO("[late input] on (usf4_late_input): the frame's first controller read waits out the frame limiter");
    }
  }
  if (g_state < 0) return;
  const Clock::time_point now = Clock::now();
  if (g_last_swap != Clock::time_point{}) {
    const uint64_t gap = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - g_last_swap).count());
    const uint64_t g = std::min<uint64_t>(gap, 1000000);
    g_interval_us = g_interval_us ? g_interval_us + (int64_t(g) - int64_t(g_interval_us)) / 16 : g;
    if (g > 20000) ++g_overruns;  // a frame that took more than a refresh and a bit
  } else {
    g_report_start = now;
  }
  g_last_swap = now;
  // This frame's slack: what the limiter still had to wait plus what its first read already waited.
  const uint32_t applied = g_applied_us.exchange(0, std::memory_order_relaxed);
  g_slack[g_slack_next] = uint64_t(g_limiter_slack_us.exchange(0, std::memory_order_relaxed)) + applied;
  g_slack_next = (g_slack_next + 1) % kFrames;
  g_slack_count = std::min(g_slack_count + 1, kFrames);
  g_delay_total_us += applied;
  ++g_delay_frames;
  uint64_t delay = 0;
  if (g_slack_count == kFrames) {
    uint64_t least = UINT64_MAX;
    for (uint64_t s : g_slack) least = std::min(least, s);
    const uint64_t ceiling = g_interval_us * 4 / 5;
    delay = least > kMarginUs ? std::min(least - kMarginUs, ceiling) : 0;
  }
  g_next_delay_us.store(uint32_t(delay), std::memory_order_relaxed);
  g_read_pending.store(true, std::memory_order_release);
  if (now - g_report_start >= std::chrono::seconds(10)) {
    REXLOG_INFO("[late input] 10 s: the first controller read of a frame delayed {:.1f} ms on average ({} frames), "
                "{} frames over 20 ms",
                g_delay_frames ? double(g_delay_total_us) / 1000.0 / double(g_delay_frames) : 0.0, g_delay_frames,
                g_overruns);
    g_report_start = now;
    g_delay_total_us = g_delay_frames = g_overruns = 0;
  }
}

}  // namespace usf4::late_input
