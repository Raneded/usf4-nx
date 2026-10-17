// Ultra Street Fighter IV: an empty pop from the XDK job list yields the core (CPU enhancement
// usf4_cpu_job_pop_yield). Needed by the optimised codegen (skip_msr).
//
// WHAT THE GAME DOES
//   The job workers (and the main thread while it waits for its jobs) loop "while jobs are outstanding: pop one and run
//   it" (sub_82966C28 -> sub_82967288 -> sub_82966BA8). The pop takes the whole list (head := 0) and then puts the rest
//   back, each step inside an interrupt-off section (mfmsr; mtmsrd r13; lwarx; stwcx.; mtmsrd). When the remaining
//   jobs are already running on other workers, the list is empty and the loop spins until they finish.
//   With the classic codegen every interrupt-off section took one process-wide recursive mutex, and a contended mutex
//   sleeps, so the spinning workers kept giving their core away. skip_msr removes that mutex. Switch 0.5.6
//   (2026-10-14): in fights the four job workers spent 80-90 % of their samples in this pop, all three cores at
//   100 %, 11 fps: Horizon does not time-slice threads of equal priority, so a spinning worker starves the worker that
//   holds the job (or the thread that is between the pop's two steps) on the same core.
//
// WHAT THIS DOES: runs the original pop; when it returns nothing, yields the core before returning. The game takes the
//   same decisions; only an empty pop takes longer (one yield, immediate when nothing else is ready on the core).
#include <atomic>
#include <cstdint>
#include <thread>

#include <rex/hook.h>
#include <rex/platform.h>
#include <rex/ppc.h>

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

#include "cpu/sa2_enhancements.h"

SA2_ENHANCEMENT(g_job_pop_yield, usf4_cpu_job_pop_yield, sa2::enh::kCpu, true,
                "An empty pop from the XDK job list (sub_82966BA8) yields the core, so job workers waiting for "
                "other workers' jobs do not starve them");

namespace usf4 {
extern thread_local bool t_job_popped;  // usf4_job_wait.cpp
}  // namespace usf4

namespace {

constexpr uint32_t kPop = 0x82966BA8;
constexpr uint32_t kPopBytes = 0x58;  // to the blr at 0x82966BFC
constexpr uint64_t kPopHash = 0xA74D6392CF5F4057ull;

std::atomic<int> g_state{0};  // 0 unknown, 1 on, -1 off (code check failed or key off); set by the first call

}  // namespace

REX_EXTERN(__imp__sub_82966BA8);
REX_HOOK_RAW(sub_82966BA8) {
  __imp__sub_82966BA8(ctx, base);
  if (ctx.r3.u32 != 0) {
    usf4::t_job_popped = true;
    return;
  }
  int state = g_state.load(std::memory_order_relaxed);
  if (state == 0) {
    state = (g_job_pop_yield.active &&
             sa2::enh::GuestCodeMatches(base, kPop, kPopBytes, kPopHash, "usf4_cpu_job_pop_yield"))
                ? 1
                : -1;
    g_state.store(state, std::memory_order_relaxed);
  }
  if (state < 0) return;
  g_job_pop_yield.Use();
#if REX_PLATFORM_SWITCH
  svcSleepThread(YieldType_WithCoreMigration);
#else
  std::this_thread::yield();
#endif
}
