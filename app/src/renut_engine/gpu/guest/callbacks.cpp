/**
 * @file    gpu/guest/callbacks.cpp
 */
#include "renut_engine/gpu/guest/callbacks.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>

#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/backend/render_thread.h"
#include "renut_engine/gpu/render/textures.h"
#include "renut_engine/gpu/banjo_renut_checks.h"  // banjo-nx (package 27c)

// sa2-nx (2026-10-20): see g_waiting.
REXCVAR_DEFINE_BOOL(sa2_renut_callbacks_fast, true, "SA2/Renderer",
                    "Renut's renderer: every draw, clear and resolve hook first runs the guest's GPU callbacks the "
                    "render thread has reached; with none waiting (nearly always: SA2 inserts few) it returns without "
                    "taking the two locks it took to find that out. Off: the locks every time")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace renut::gpu {

namespace {

struct Callback {
  uint64_t seq = 0;
  u32 function = 0;
  u32 context = 0;
};

std::mutex g_mutex;
std::deque<Callback> g_callbacks;  // inserted, not run yet, in order
uint64_t g_last_seq = 0;
// Set while a thread runs callbacks: they run one at a time, in order.
bool g_running = false;
// sa2-nx (2026-10-20, sa2_renut_callbacks_fast): g_callbacks.size(), kept with it under g_mutex and read without it.
// A hook that reads 0 while another thread inserts one runs it at its next call; on the inserting thread the count is
// in program order.
std::atomic<size_t> g_waiting{0};

// usf4-nx (2026-10-22): Ryujinx stalls at movie changes. A callback the render thread has reached runs only at a guest
// thread's next Direct3D call; if none comes (the game thread waiting for the movie thread, which waits for the
// callback), nothing runs it. This reports a reached callback that waits more than 2 s, every 5 s. Joined at exit
// (std::thread::detach ends the process on the Switch).
struct StallWatch {
  std::mutex mutex;
  std::condition_variable cv;
  bool stop = false;
  std::thread thread;
  void Run() {
    std::unique_lock lock(mutex);
    uint64_t seen_seq = 0;
    auto seen_at = std::chrono::steady_clock::now();
    auto next_report = seen_at;
    while (!cv.wait_for(lock, std::chrono::seconds(1), [this] { return stop; })) {
      Callback front{};
      size_t waiting = 0;
      {
        std::lock_guard callbacks_lock(g_mutex);
        if (!g_callbacks.empty())
          front = g_callbacks.front();
        waiting = g_callbacks.size();
      }
      const auto now = std::chrono::steady_clock::now();
      if (!front.seq || front.seq > render_thread::CallbacksReached()) {
        seen_seq = 0;
        continue;
      }
      if (front.seq != seen_seq) {
        seen_seq = front.seq;
        seen_at = now;
        next_report = now + std::chrono::seconds(2);
      } else if (now >= next_report) {
        next_report = now + std::chrono::seconds(5);
        RGPU_WARN("InsertCallback: callback {} ({:08X}, context {:08X}) reached {:.1f} s ago and not run: no guest "
                  "thread has made a Direct3D call since ({} waiting)", front.seq, front.function, front.context,
                  std::chrono::duration<double>(now - seen_at).count(), waiting);
      }
    }
  }
  ~StallWatch() {
    {
      std::lock_guard lock(mutex);
      stop = true;
    }
    cv.notify_all();
    if (thread.joinable())
      thread.join();
  }
};
StallWatch g_stall_watch;
std::once_flag g_stall_watch_once;

} // namespace

void InsertCallback(u32 function, u32 context) {
  std::call_once(g_stall_watch_once, [] { g_stall_watch.thread = std::thread([] { g_stall_watch.Run(); }); });
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 8)
    RGPU_INFO("InsertCallback: {:08X} (context {:08X})", function, context);  // usf4-nx: who inserts them
  uint64_t seq;
  {
    std::lock_guard lock(g_mutex);
    seq = ++g_last_seq;
    g_callbacks.push_back({seq, function, context});
    g_waiting.store(g_callbacks.size(), std::memory_order_release);
  }
  // The linear textures of the work before it are read now, not when the
  // command list is submitted: after the callback the game may rewrite them
  // (the XMV player's frames).
  // banjo-nx (package 27c): the vertex fills too, for the same reason.
  banjo::renut_gpu::checks::QueueKickFlush();
  render_thread::QueueCallbackMarker(seq);
}

uint64_t LastCallback() {
  std::lock_guard lock(g_mutex);
  return g_last_seq;
}

std::atomic<int> g_hook_fast_paths{0};  // sa2-nx: callbacks.h

bool HookFastPathsInit() {
  const bool on = REXCVAR_GET(sa2_renut_callbacks_fast);
  g_hook_fast_paths.store(on ? 1 : -1, std::memory_order_relaxed);
  return on;
}

void RunReadyCallbacks() {
  if (HookFastPathsOn() && g_waiting.load(std::memory_order_acquire) == 0)
    return;  // sa2-nx: nothing inserted and not run
  const uint64_t reached = render_thread::CallbacksReached();
  std::vector<Callback> ready;
  {
    std::lock_guard lock(g_mutex);
    if (g_running || g_callbacks.empty() || g_callbacks.front().seq > reached)
      return;
    while (!g_callbacks.empty() && g_callbacks.front().seq <= reached) {
      ready.push_back(g_callbacks.front());
      g_callbacks.pop_front();
    }
    g_waiting.store(g_callbacks.size(), std::memory_order_release);
    g_running = true;
  }
  for (const Callback &callback : ready) {
    PPCFunc *function = rex::runtime::ResolveIndirectFunction(callback.function);
    if (!function) {
      RGPU_ERROR("InsertCallback: no guest function at {:08X}", callback.function);
      continue;
    }
    rex::ppc::GuestToHostFunction<void>(function, callback.context);
  }
  {
    std::lock_guard lock(g_mutex);
    g_running = false;
  }
  render_thread::CallbacksRun(ready.back().seq);
}

} // namespace renut::gpu
