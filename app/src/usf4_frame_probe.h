// Ultra Street Fighter IV: where the main thread's time goes in a long frame (usf4_hitch_log_ms; the "[hitch]" line of
// usf4_frame_counter.cpp). The waits of the main thread add their wall time here; the frame counter reads and clears
// the totals at every swap.
#pragma once

#include <chrono>
#include <cstdint>

namespace usf4::probe {

enum Wait : int { kGpu = 0, kJobs, kLimiter, kCount };
inline thread_local uint64_t t_us[kCount] = {};

// Adds the wall time of its scope to one total of this thread.
struct Scope {
  explicit Scope(Wait w) : wait(w), t0(std::chrono::steady_clock::now()) {}
  ~Scope() {
    t_us[wait] += uint64_t(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
  }
  Wait wait;
  std::chrono::steady_clock::time_point t0;
};

}  // namespace usf4::probe
