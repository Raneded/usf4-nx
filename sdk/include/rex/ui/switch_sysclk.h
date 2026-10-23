/*
 * Ask Horizon OC (sys-clk and its forks) to apply the configured docked column while Reverse-NX
 * says docked. Details and why it is needed: src/ui/switch_sysclk.cpp.
 */
#ifndef REX_UI_SWITCH_SYSCLK_H_
#define REX_UI_SWITCH_SYSCLK_H_

#include <cstdint>

#include "rex/platform.h"

namespace rex::ui::switch_sysclk {

#if REX_PLATFORM_SWITCH

// Once per second. "effective" is the mode the game obeys (Reverse-NX's if set) and "real" the
// hardware's: something is done whenever they differ, in either direction, which is exactly what
// the clock sysmodule cannot see (it looks at the hardware).
//   effective docked   + real handheld -> clocks go up to the configured docked column.
//   effective handheld + real docked   -> they go down to the handheld column, and if that column
//                                         is empty to the stock clocks (GPU 307.2 MHz and memory
//                                         1331.2), which is what "Fake Handheld" is expected to do.
//                                         Note: the sysmodule's overlay will keep saying "Docked"
//                                         because that is its profile; what changes are the actual MHz.
void FollowMode(bool effective_docked, bool docked_real);

// Turns all of this on or off (the game passes it from its cvar). Turning it off releases the clocks.
void Enable(bool enabled);

// usf4-nx (2026-10-23, tests): the CPU's and the GPU's override (Hz; 0 releases that one), never the memory's (a
// memory clock change can need a reboot). Not released at exit: it stays until set again here or in the overlay.
bool SetCpuGpuOverride(uint32_t cpu_hz, uint32_t gpu_hz);

#else

inline void FollowMode(bool, bool) {}
inline void Enable(bool) {}
inline bool SetCpuGpuOverride(uint32_t, uint32_t) { return false; }

#endif  // REX_PLATFORM_SWITCH

}  // namespace rex::ui::switch_sysclk

#endif  // REX_UI_SWITCH_SYSCLK_H_
