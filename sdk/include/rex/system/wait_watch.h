/**
 * @file    rex/system/wait_watch.h
 * @brief   What each guest thread is waiting on, for stall reports.
 *
 * usf4-nx (2026-10-23): the game thread stopped presenting at screen changes under Renut (Ryujinx) with no
 * renderer wait reported. Every kernel wait of a guest thread (XObject::Wait / WaitMultiple / SignalAndWait and
 * KeDelayExecutionThread) records here what it waits on, since when, and the guest return address of the call
 * (ctx.lr: the game function that waits). Dump() logs the threads that are waiting; the frame watchdog
 * (app/src/usf4_frame_counter.cpp) calls it when no frame has been presented for a while.
 */
#pragma once

#include <cstdint>

namespace rex::system::wait_watch {

enum Kind : uint32_t {
  kNone = 0,
  kWait = 1,          // XObject::Wait (KeWaitForSingleObject, NtWaitForSingleObjectEx, critical sections)
  kWaitMultiple = 2,  // XObject::WaitMultiple (wait_type in the high byte: 0 all, 1 any)
  kSignalAndWait = 3,
  kDelay = 4,         // KeDelayExecutionThread
};

// The calling guest thread starts waiting on 'object' (an XObject handle, or the delay in ms for kDelay).
void Begin(Kind kind, uint32_t object, uint32_t object_type);
// The calling guest thread stops waiting.
void End();
// Logs every guest thread that is waiting, with how long, on what and from where. 'why' heads the report.
void Dump(const char* why);

// Begin/End for a scope.
struct Scope {
  Scope(Kind kind, uint32_t object, uint32_t object_type) { Begin(kind, object, object_type); }
  ~Scope() { End(); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

}  // namespace rex::system::wait_watch
