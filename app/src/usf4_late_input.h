// usf4-nx (2026-10-23): late input for the native Vulkan renderer (usf4_late_input). USF4 reads its controller right
// after the Swap, does the frame's work, then its frame limiter (sub_82756DE8) waits out the rest of the 16.7 ms
// before the next Swap: the input is that wait older than it needs to be. The frame's first controller read is
// delayed by the least such wait of the last 16 frames (counting the delay itself), less a margin, so the frame
// starts later with newer input and still makes its Swap. Renut has its own (banjo_late_input, render_thread.cpp),
// which takes the same limiter wait; this one stays off under Renut.
#pragma once

#include <cstdint>

namespace usf4::late_input {

// The game thread at each Swap (usf4_frame_counter.cpp): ends a frame and works out the next frame's delay.
void OnFrame();

// The frame limiter's wait still to go at its first look in a frame, already less usf4_late_input_limiter_margin_us
// (usf4_job_wait.cpp).
void NoteLimiterSlack(uint32_t us);

}  // namespace usf4::late_input
