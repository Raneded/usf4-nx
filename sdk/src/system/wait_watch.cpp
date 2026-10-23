/**
 * @file    system/wait_watch.cpp
 * @brief   What each guest thread is waiting on (rex/system/wait_watch.h).
 *
 * usf4-nx (2026-10-23). One slot per guest thread, taken on its first wait and kept for the thread's life (a
 * finished thread's slot just stays idle). The waiting thread writes only its own slot, with relaxed atomics; Dump()
 * reads them all from another thread. A torn read can only mix two waits of one thread in one line of a report.
 */

#include <rex/system/wait_watch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>

#include <rex/logging.h>
#include <rex/system/thread_state.h>
#include <rex/system/xthread.h>

namespace rex::system::wait_watch {
namespace {

constexpr int kSlots = 128;

struct Slot {
  std::atomic<uint32_t> thread_id{0};  // 0 = free; published after name is written
  char name[40] = {};
  std::atomic<uint32_t> kind{kNone};
  std::atomic<uint32_t> object{0};
  std::atomic<uint32_t> object_type{0};
  std::atomic<uint32_t> lr{0};
  std::atomic<uint64_t> since_us{0};
  std::atomic<uint64_t> waits{0};  // waits begun, to tell a thread stuck in one wait from one that keeps waiting
};

Slot g_slots[kSlots];
std::atomic<int> g_used{0};
thread_local Slot* t_slot = nullptr;
thread_local bool t_no_slot = false;

uint64_t NowUs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

Slot* MySlot() {
  if (t_slot || t_no_slot) return t_slot;
  if (!XThread::IsInThread()) {
    t_no_slot = true;
    return nullptr;
  }
  const int index = g_used.fetch_add(1, std::memory_order_relaxed);
  if (index >= kSlots) {
    t_no_slot = true;
    return nullptr;
  }
  XThread* thread = XThread::GetCurrentThread();
  Slot& slot = g_slots[index];
  std::strncpy(slot.name, thread->name().c_str(), sizeof(slot.name) - 1);
  slot.thread_id.store(thread->thread_id() ? thread->thread_id() : uint32_t(index + 1), std::memory_order_release);
  t_slot = &slot;
  return t_slot;
}

const char* KindName(uint32_t kind) {
  switch (kind & 0xFF) {
    case kWait: return "wait";
    case kWaitMultiple: return (kind >> 8) ? "wait any" : "wait all";
    case kSignalAndWait: return "signal and wait";
    case kDelay: return "delay";
    default: return "?";
  }
}

const char* TypeName(uint32_t type) {
  static const char* const kNames[] = {"undefined", "enumerator", "event", "file",  "io completion",
                                       "module",    "mutant",     "notify", "semaphore", "session",
                                       "socket",    "symlink",    "thread", "timer"};
  return type < sizeof(kNames) / sizeof(kNames[0]) ? kNames[type] : "?";
}

}  // namespace

void Begin(Kind kind, uint32_t object, uint32_t object_type) {
  Slot* slot = MySlot();
  if (!slot) return;
  uint32_t lr = 0;
  if (auto* state = XThread::GetCurrentThread()->thread_state(); state && state->context())
    lr = uint32_t(state->context()->lr);
  slot->object.store(object, std::memory_order_relaxed);
  slot->object_type.store(object_type, std::memory_order_relaxed);
  slot->lr.store(lr, std::memory_order_relaxed);
  slot->since_us.store(NowUs(), std::memory_order_relaxed);
  slot->waits.fetch_add(1, std::memory_order_relaxed);
  slot->kind.store(kind, std::memory_order_release);
}

void End() {
  if (t_slot) t_slot->kind.store(kNone, std::memory_order_release);
}

void Dump(const char* why) {
  const uint64_t now = NowUs();
  const int used = std::min(g_used.load(std::memory_order_relaxed), kSlots);
  REXLOG_WARN("[waits] {}: guest threads that are waiting now ({} known):", why, used);
  int waiting = 0;
  for (int i = 0; i < used; ++i) {
    Slot& slot = g_slots[i];
    const uint32_t id = slot.thread_id.load(std::memory_order_acquire);
    const uint32_t kind = slot.kind.load(std::memory_order_acquire);
    if (!id) continue;
    if (kind == kNone) {
      REXLOG_WARN("[waits]   thread {:>3} {:<24} running ({} waits so far)", id, slot.name,
                  slot.waits.load(std::memory_order_relaxed));
      continue;
    }
    ++waiting;
    const uint64_t since = slot.since_us.load(std::memory_order_relaxed);
    REXLOG_WARN("[waits]   thread {:>3} {:<24} {} {} {:08X} for {:.1f} ms, called from {:08X} ({} waits so far)", id,
                slot.name, KindName(kind),
                (kind & 0xFF) == kDelay ? "ms" : TypeName(slot.object_type.load(std::memory_order_relaxed)),
                slot.object.load(std::memory_order_relaxed), now > since ? (now - since) / 1000.0 : 0.0,
                slot.lr.load(std::memory_order_relaxed), slot.waits.load(std::memory_order_relaxed));
  }
  REXLOG_WARN("[waits] {} of {} waiting", waiting, used);
}

}  // namespace rex::system::wait_watch
