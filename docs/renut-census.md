# Renut census: USF4's Direct3D against SA2's (2026-10-22)

Step 1 of the deko3d route ([sa2-nx docs/deko3d-renderer-guide.md](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/deko3d-renderer-guide.md),
section 2): find every function Renut's renderer hooks in USF4, and check every device offset it reads against USF4's
own code. Analysis only; nothing is built from it yet.

Method: there is no SA2 image here, so functions were not matched by shape (`tools/d3d_match.py`) but by what they
do, what they call and who calls them, from the generated code's PPC listing (`tools/callgraph.py`, a listing helper
like `tools/d3d/ppcfn.py`). SA2's addresses and offsets are Renut's (sa2-nx `app/src/renut_engine`, adf3a4e).

## Result

USF4 links XDK 2.0.21256 (`lis r3,21256` in the hardware path), SA2 2.0.21076. **The device layout is SA2's**: the
allocation is 24,704 bytes (0x6080) in both, and every private offset Renut uses that was checked appears in USF4's
Direct3D with the same kind of access (below). So Renut's SA2 offsets (`d3d.h`, `d3d_native.h`, `hooks/device.cpp`)
can be taken as they are; only the function addresses change, plus the differences listed at the end.

## The hooks

| Function | SA2 | USF4 | Evidence in USF4 |
|---|---|---|---|
| Direct3D_CreateDevice | 8254FC10 | **inlined** into the game's renderer setup `sub_82757EC0` | allocates 24832, aligns to 128, clears 24704; writes 11008 (global qword), 11016 (thread id), 11020 (process type == 2), 17136 (4800-byte side buffer), 24392 (behaviour flags 0x0C000001); then calls the hardware path. Takes the game's object (device stored at +8), not Direct3D_CreateDevice's arguments |
| hardware path (SA2 sub_8255CDA0) | 8255CDA0 | `sub_82201978` (dev, params) | critical sections at 15348 and 15376, the XDK check `lis r3,21256`, stores the device into the VdGlobalDevice slot, fence word at 11024, −1 at 21968/21972, VdInitializeEngines, SetRingBufferParameters(dev, params + 72) |
| CDevice::KickOff | 8254EBB8 | `sub_821F8EA8` | (dev) → returns m_pRing (+0x30); 63 callers (SA2: 71); calls KickOffSegment, AllocateInlineData(dev, 4, 4), the submit (sub_821F8BF0: spin lock, KeSetEvent) and BlockOnFence |
| CDevice::KickOffSegment | 8254E830 | `sub_821F8A68` | only caller KickOff; writes the segment's indirect-buffer packet; calls the segment allocator |
| BeginRingBig | 8254EDF8 | `sub_821F78B8` | the third caller of the segment allocator (SA2's three: KickOffSegment, BeginRingBig, Swap) |
| ring segment allocator | 8254DA90 | `sub_821F8050` | callers exactly BeginRingBig, KickOffSegment and Swap; reads 13920, 17136, 15344 |
| RingBufferDeviceAllocate | 8254D4B8 | `sub_821F7C80` | (dev, flags, *size, alignment); AllocateInlineData calls it as (dev, 1, &bytes, alignment) |
| AllocateInlineData | 8254D9A8 | `sub_821F7EB0` | (dev, dwords, alignment); the command buffer at 13920, else RingBufferDeviceAllocate; 11 callers |
| SetRingBufferParameters | 8254EF40 | `sub_821F9588` | (dev, params); BlockUntilIdle first, VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack; also called by the game (sub_82757CB8) |
| CDevice::BlockOnFence | 8254D6E8 | `sub_821F8FA8` | (dev, fence, type, no_kick); kicks when the fence is the current one and 13600 is 0, then polls CBlocker::Check |
| CBlocker::Check | 8254F400 | `sub_821F54E0` | the Direct3D spin wait (already known: 32 × db16cyc, the GPU read pointer, 5 s timeout) |
| IsFencePending | 8254ECF8 | `sub_821F94A0` | (fence); the device from VdGlobalDevice, KickOff if fence == 11036, then completion word (11024) against the tag |
| InsertCallback | 8254EEE8 | `sub_821F9530` | (dev, type, callback, context); writes a CP register 1404 packet with callback and context (sub_821F90A0); callers EndTiling (sub_821FDDE0) and one game function |
| SynchronizeToPresentationInterval | 825589D8 | `sub_821F9CB0` | the only Direct3D writer of the swap-pending bit (11069 \|= 0x10); reads the presentation interval at 14000; called from Swap's helpers, Present and the game |
| D3DDevice_Swap | 82558E98 | `sub_821FAA00` | the only caller of VdSwap; swap count 16968, fence tags 11056 and 15344, gamma ramp at 15408 |
| DrawVertices | 82565C60 | `sub_821F4B88` | already hooked by the native renderer |
| DrawIndexedVertices | 82566078 | `sub_821F4FA0` | already hooked by the native renderer |
| DrawVerticesUP | 82565C18 | `sub_821F4560` | calls BeginVertices, copies the vertices, m_pRing = [13844]. **Correction:** usf4-notes.md calls sub_821F40A0 DrawVerticesUP |
| BeginVertices | 82565758 | `sub_821F40A0` | stores m_pRing at 13844; called by DrawVerticesUP, the Present helper sub_821FA300 and one game function |
| ClearF | 82563978 | `sub_821FC558` | (dev, flags, rect, color*, Z in f1, stencil); a null rect becomes the whole target (12816 or 12832) |
| Clear (D3DCOLOR) | 82563AB0 | `sub_821FC498` | `vupkd3d` of the colour, then ClearF per rect |
| Resolve | 825656F8 | `sub_821F1810` | arguments 10 and 11 read at +92/+100 as in SA2; called by the game, Present (sub_821FB238) and EndTiling (sub_821FDDE0). No small wrapper in USF4: this is the core |
| XGRegisterVertexShader | 8255E658 | `sub_821E5C68` | (shader, physical): clears 872 bytes, type 6, physical part at +32; only caller CreateVertexShader sub_821E5D60 |
| XGRegisterPixelShader | 8255DBF8 | `sub_821E5980` | only caller CreatePixelShader sub_821E59B8 |
| CreateVertexDeclaration | 8255E198 | `sub_821E6128` | (elements) → declaration: counts 12-byte elements to stream 255, allocates 56 + 12n bytes |
| D3DResource_Destroy | 8254A200 | `sub_821F28F0` | called by D3DResource_Release (sub_821F31A0, 56 callers) when the reference count at +4 reaches 0; waits on the fence, frees |

Also found: BlockUntilIdle `sub_821F93B8` (8 callers), Present `sub_821FB238` (reads 13052, 15232, 15236), EndTiling
`sub_821FDDE0`, the BeginIndexedVertices / DrawIndexedVerticesUP pair `sub_821F45A8` / `sub_821F4AE8` (see below),
D3DResource_Release `sub_821F31A0`.

## Device offsets

Every offset below was found in USF4's Direct3D with the access Renut expects (lwz/stw on words, lbz/stb on the
flag bytes; arrays as `addi rX,rY,N`, or an index plus N / 4 for the streams and textures).

| Offsets (SA2 = USF4) | What |
|---|---|
| 11008, 11016, 11020, 24392, 17136 | set by CreateDevice: global qword, thread id, title process, behaviour flags, side buffer (4800 bytes) |
| 11024, 11036, 11056, 15344, 16968 | fence completion word, current fence tag, Swap fence tag, presented fence tag, swap count |
| 11068-11071, 11040, 11060, 11064 | flag bytes, command-buffer tag mask, tile / run predication |
| 12240, 12812, 12816 (4), 12832 | vertex declaration, index buffer, render targets, depth |
| 12836 (16, as `addi r,r,3209`), 12904 (u8[16]) | stream sources, strides |
| 12920 (as `addi r,r,3230`), 13068, 13072 | textures, pixel shader, vertex shader |
| 13052 | SetScissorRect's rectangle |
| 13084, 13088, 13092, 13104, 13124, 13128, 13368, 13556 | predication and tiling state |
| 13600, 13844, 13920 | in-KickOff word, inline-draw ring end, command buffer being recorded |
| 14000 | presentation interval (device copy of D3DPRESENT_PARAMETERS + 52) |
| 15228, 15232, 15236 | automatic depth, front and back buffers |
| 15340, 15348, 15376, 15408 | ring bookkeeping, the two critical sections, gamma ramp |
| 21968, 21972, 21996 | the −1 words, performance frequency |

Not found, and only used by N&B-only code in Renut: 23380 (`kDeviceOffsetTitleMinusOne`) and 0x2A88
(`kDeviceOffsetOwnerThread`). Check each when the hooks are written.

## What differs from SA2

1. **CreateDevice is inlined into the game.** Renut's CreateDevice hook (run the original as a no-hardware device,
   then set up the title device without the GPU) has no function to sit on. Plan: hook the hardware path
   `sub_82201978` instead, and do there what Renut's `InitializeTitleDevice` does (critical sections, VdGlobalDevice,
   fence word, frequency, presentation parameters, InitializeApiState), without the GPU. The no-hardware flag and the
   immediate-fence bit (11068 | 0x80, 11069 | 0x02) must still be set as Renut's hook sets them.
2. **The VdGlobalDevice slot** is chosen at run time: `0x820007F4` when KeGetCurrentProcessType() != 2, `0x820007F8`
   when it is 2 (SA2: one slot, 0x82000710). Read it the way USF4's code does.
3. **The global qword** CreateDevice copies into +11008 is at `0x82AB4D40` (SA2 0x82980268).
4. **Indexed inline draws.** USF4 calls DrawIndexedVerticesUP (`sub_821F4AE8`, 2 callers, which calls
   BeginIndexedVertices `sub_821F45A8`). SA2's Renut hooks neither; Renut's N&B code has
   `D3DDevice_DrawIndexedVerticesUP_hook` (`hooks/draw.cpp`) to take.
5. **The window scissor.** SA2 needed a hook because its Direct3D wrote the window offset and scissor only into the
   command buffer. In USF4, SetScissorRect (`sub_821EE2F0`) stores the scissor into the device's window packet (+0x28C4,
   +0x28C8), so the hook may not be needed. To be confirmed on the PC run (and +0x28C0, the window offset, which
   nothing stored with a plain `stw`).
6. **Resolve has no wrapper**: the hook goes on the core, `sub_821F1810`, which is what every caller reaches.
7. **USF4-specific work the current renderer does** (one tile instead of predicated tiling, the clears into depth
   owners, column imports, the WMV decoder's GPU shaders) is either not needed with Renut (it works at Direct3D's
   level and has no EDRAM model) or must be checked: the movies' deblocking shaders, predicated tiling (Renut supports
   it: SetPredication, BeginTiling) and Scaleform's draws.

## Before any code

- **Licence:** reNut (masterspike52) has no licence file. sa2-nx states its author allows reuse by that developer's
  ports. Ask masterspike52 before Renut's code enters this public repository.
- The next step is section 3 of the guide: Renut on the PC behind a boot key (`sa2_renderer = "renut"`), against the
  current renderer, with `tools/direct_calls.py` taught the new hooks.

## Step 2: Renut on the PC (2026-10-22)

Built with `-DUSF4_RENUT_RENDERER=ON` (off by default) and chosen with `sa2_renderer = "renut"`; the PM4 renderer and the
emulated GPU are unchanged (a training fight with `sa2_renderer_native = true` still runs, 15 fps on the GTX 960M).

What it took on top of the census:
- Renut copied from sa2-nx adf3a4e (`app/src/renut_engine`, `app/thirdparty/{plume,reblue-XenosRecomp,uam,imgui}`,
  `patches/`), its CMake glue (`app/cmake/sa2_renut_gpu.cmake`), the hook addresses remapped to USF4's.
- Device creation on the hardware path (`Usf4InitializeHardwareDevice`, `hooks/device.cpp`); SetRingBufferParameters
  rewinds the sink; VdGlobalDevice from either of USF4's slots.
- **EndTiling's segment end** (`sub_82204310`, hooked: `Usf4EndTilingSegment_hook`). The game calls EndTiling even with
  one tile, and its segment end submits to the GPU's primary ring directly (`sub_821F8BF0` -> `sub_821F8410`), which
  does not exist without the hardware ring: the first write was to guest 0 (found with gdb on the non-ASLR run).
- **One tile with Renut** (`usf4_tiling.cpp`, `SingleTile()`): Renut's BeginTiling/EndTiling hooks are N&B's, with
  N&B's raw private offsets, so USF4's three predicated strips drew only the first strip. The Switch build already
  draws one tile.
- XenosRecomp (Renut's copy): vertex exports without a declared interpolator go to TEXCOORDN (5 shaders), FOG0 is an
  interpolator (USF4's fog), and fetches whose swizzle is all constants write only the constants (sa2-nx's fix in the
  native renderer's copy; 1 shader). All 874 containers now translate and compile.
- `tools/usf4_renut_shader_dump.py`: the containers under Renut's names (`out/renut_shader_dump`, 874 of 874 pass
  reNut's container check). `tools/direct_calls.py` reads Renut's hook macros.
- From sa2-nx: `rex/input/read_hook.h` (SDK), the recorder's Renut source and lockstep (`sa2_capture.*`,
  `profiler/sa2_thread_cycles_pc.*`), `sa2::render::RenderHeightOf`; `MarkStoreCardsAt` only (USF4 marks no store
  cards: `g_banjo_store_cards = false`, Renut hashes its copies).
- Linux DXC for the translator: Microsoft's release copied into `reblue-XenosRecomp/thirdparty/dxc-bin/{bin,lib}/x64`
  (git-ignored).

Result (GTX 960M, 1280x720): boot logos, the opening movie (WMV decoder's GPU shaders), title, main menu, Training,
character select, world map, VS screen and a Juri vs Hakan training fight with HUD and effects all drawn right; no
error. Fight: 23.6 fps, ~143 draws a frame, Renut's render thread 3.4 ms a frame, GPU 21 ms (the PM4 renderer: 15
fps). Not yet compared frame by frame with the PM4 renderer (lockstep captures), not yet run on deko3d.

Next: the Switch build on deko3d (plume's deko3d back end, uam, the deko3d shader tools and package), first in
Ryujinx.
