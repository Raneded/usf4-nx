// Ultra Street Fighter IV: the main thread sleeps while its jobs run instead of spinning on Sleep(0) (CPU enhancement
// usf4_cpu_job_wait_block).
//
// WHAT THE GAME DOES
//   sub_82966C90 publishes a batch of jobs and then loops until every job of the batch is done: collect the finished
//   jobs (state [+28] == 2), run one job itself if the list has one (sub_82967288), then Sleep(0) (sub_8220FC00 ->
//   SleepEx -> KeDelayExecutionThread with a zero interval) and look again. While the last jobs run on the workers
//   the loop is a spin: Switch 0.6.9 profiling build (2026-10-17), fights: 0.65-0.92 million Sleep(0) per second
//   from this one call site (each a svcSleepThread yield), 10-17 % of the main thread's samples.
//
// WHAT THIS DOES
//   Every job run (sub_82967288) that popped a job bumps a sequence number when it returns; that is when the job's
//   state has become 2, the only change the loop waits for. The Sleep(0) of this call site (return address
//   0x82966E40) returns at once when a job finished since the last one, else yields for a short spin
//   (usf4_job_wait_spin_us) and then sleeps on a condition variable until the next job finishes (2 ms at most, a
//   safety net: the loop just looks again). The game takes the same decisions; only the wait between two looks is a
//   sleep. Every other Sleep call is untouched.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/platform.h>
#include <rex/ppc.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

#include "cpu/sa2_enhancements.h"

SA2_ENHANCEMENT(g_job_wait_block, usf4_cpu_job_wait_block, sa2::enh::kCpu, true,
                "The main thread's wait for its jobs (Sleep(0) loop in sub_82966C90) sleeps until a job finishes "
                "instead of spinning on yields");

REXCVAR_DEFINE_INT32(usf4_job_wait_spin_us, 20, "USF4",
                     "usf4_cpu_job_wait_block: microseconds of yielding before the main thread sleeps on the job "
                     "condition variable")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace usf4 {
// Set by the job-list pop hook (usf4_job_pop_yield.cpp) when the pop returned a job.
thread_local bool t_job_popped = false;
}  // namespace usf4

namespace {

constexpr uint32_t kWaitLoop = 0x82966C90;
constexpr uint32_t kWaitLoopBytes = 488;
constexpr uint64_t kWaitLoopHash = 0xE2345D869AAE78DBull;
constexpr uint32_t kRun = 0x82967288;
constexpr uint32_t kRunBytes = 120;
constexpr uint64_t kRunHash = 0xEDE7609C7E388BEFull;
constexpr uint32_t kSleepReturn = 0x82966E40;  // return address of the Sleep(0) call in sub_82966C90

std::atomic<int> g_state{0};  // 0 unknown, 1 on, -1 off (code check failed or key off); set by the first call
std::atomic<uint64_t> g_done{0};
std::atomic<uint32_t> g_waiters{0};
std::mutex g_mutex;
std::condition_variable g_cv;
thread_local uint64_t t_seen = 0;

bool On(uint8_t* base) {
  int state = g_state.load(std::memory_order_relaxed);
  if (state == 0) {
    state = (g_job_wait_block.active &&
             sa2::enh::GuestCodeMatches(base, kWaitLoop, kWaitLoopBytes, kWaitLoopHash, "usf4_cpu_job_wait_block") &&
             sa2::enh::GuestCodeMatches(base, kRun, kRunBytes, kRunHash, "usf4_cpu_job_wait_block"))
                ? 1
                : -1;
    g_state.store(state, std::memory_order_relaxed);
  }
  return state > 0;
}

void Yield() {
#if REX_PLATFORM_SWITCH
  svcSleepThread(YieldType_WithCoreMigration);
#else
  std::this_thread::yield();
#endif
}

void WaitForJob() {
  uint64_t done = g_done.load(std::memory_order_acquire);
  if (done != t_seen) {  // a job finished since the last look: look again at once
    t_seen = done;
    return;
  }
  static const auto spin = std::chrono::microseconds(REXCVAR_GET(usf4_job_wait_spin_us));
  const auto t0 = std::chrono::steady_clock::now();
  do {
    Yield();
    done = g_done.load(std::memory_order_acquire);
    if (done != t_seen) {
      t_seen = done;
      return;
    }
  } while (std::chrono::steady_clock::now() - t0 < spin);
  g_job_wait_block.Use();
  g_waiters.fetch_add(1, std::memory_order_seq_cst);
  {
    std::unique_lock<std::mutex> lock(g_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    while (g_done.load(std::memory_order_seq_cst) == t_seen) {
      if (g_cv.wait_until(lock, deadline) == std::cv_status::timeout) break;
    }
  }
  g_waiters.fetch_sub(1, std::memory_order_relaxed);
  t_seen = g_done.load(std::memory_order_acquire);
  g_job_wait_block.Add(uint64_t(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count()));
}

const bool g_unit = [] {
  g_job_wait_block.amount_unit = "us waiting";
  return true;
}();

}  // namespace

REX_EXTERN(__imp__sub_82967288);
REX_HOOK_RAW(sub_82967288) {
  usf4::t_job_popped = false;
  __imp__sub_82967288(ctx, base);
  if (!usf4::t_job_popped || g_state.load(std::memory_order_relaxed) <= 0) return;
  g_done.fetch_add(1, std::memory_order_seq_cst);
  if (g_waiters.load(std::memory_order_seq_cst) != 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cv.notify_all();
  }
}

REX_EXTERN(__imp__sub_8220FC00);
REX_HOOK_RAW(sub_8220FC00) {
  if (ctx.lr == kSleepReturn && ctx.r3.u32 == 0 && On(base)) {
    WaitForJob();
    ctx.r3.u64 = 0;  // SleepEx's result for a sleep that was not alerted
    return;
  }
  __imp__sub_8220FC00(ctx, base);
}
