// The addresses both renderers hook (banjo-nx's tools/renut_gpu_import.py generated this list for N&B). sa2-nx
// (2026-10-20): SA2's. sa2-nx's own hook there (src/me, src/cpu) calls renut_native_sub_X when Renut's renderer is
// chosen (sa2_renut_yield.h). usf4-nx (2026-10-22): USF4's, the addresses usf4-nx's own hooks share with Renut's.
#pragma once

#include <rex/ppc.h>

extern "C" void renut_native_sub_821F54E0(PPCContext& ctx, uint8_t* base);  // CBlocker::Check (src/usf4_gpu_wait.cpp)
extern "C" void renut_native_sub_821F40A0(PPCContext& ctx, uint8_t* base);  // D3DDevice_BeginVertices (src/me/usf4_me_hooks.cpp)
extern "C" void renut_native_sub_821F4B88(PPCContext& ctx, uint8_t* base);  // D3DDevice_DrawVertices (src/me/usf4_me_hooks.cpp)
extern "C" void renut_native_sub_821F4FA0(PPCContext& ctx, uint8_t* base);  // D3DDevice_DrawIndexedVertices (src/me/usf4_me_hooks.cpp)
extern "C" void renut_native_sub_821FAA00(PPCContext& ctx, uint8_t* base);  // D3DDevice_Swap (src/usf4_frame_counter.cpp)
