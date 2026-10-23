# Renut on deko3d with good performance: the USF4 case study

How usf4-nx (Ultra Street Fighter IV, Xbox 360, title 434307FC) went from a Vulkan renderer on NVK to Renut's
Direct3D-level renderer on deko3d, the Switch's own graphics API, and reached a locked 60 fps at 1280x720 in both
handheld and docked mode (v0.8.0, 2026-10-23). It is written to be handed to the next ReXGlue Switch port that wants
the same result.

**Read this with sa2-nx's guide.** The step-by-step recipe (census, PC bring-up, shader chain, emulator, dropping the
driver, render-thread work, latency, release shaders) is
[sa2-nx `docs/deko3d-renderer-guide.md`](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/deko3d-renderer-guide.md),
and its full record is [sa2-nx `docs/renut-renderer.md`](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/renut-renderer.md).
This document does not repeat that recipe. It records what a second game needed on top of it, the console
measurements that decided each change, the mistakes that cost time and how each was found, and the test rig that made
unattended console work possible. usf4-nx's own census of the port is [renut-census.md](renut-census.md), and the
version notes are in [usf4-notes.md](usf4-notes.md) ("0.8.0: Renut on deko3d on the Switch").

## Contents

1. [Result](#1-result)
2. [The projects this is built from](#2-the-projects-this-is-built-from)
3. [Architecture in one page](#3-architecture-in-one-page)
4. [Timeline: what was done, in order](#4-timeline-what-was-done-in-order)
5. [Console fixes that mattered](#5-console-fixes-that-mattered)
6. [Performance work](#6-performance-work)
7. [Input latency](#7-input-latency)
8. [Measuring honestly: the renderer comparison](#8-measuring-honestly-the-renderer-comparison)
9. [Shipping: no game shaders in the NRO](#9-shipping-no-game-shaders-in-the-nro)
10. [Switch button icons](#10-switch-button-icons)
11. [The unattended console test rig](#11-the-unattended-console-test-rig)
12. [Diagnostics worth building early](#12-diagnostics-worth-building-early)
13. [Traps, each one paid for](#13-traps-each-one-paid-for)
14. [Open items](#14-open-items)
15. [Checklist for the next port](#15-checklist-for-the-next-port)

---

## 1. Result

Console: a V1 Switch, firmware 22.5.0 (emuMMC, Atmosphère), docked. Training fight (Juri vs Hakan), two minutes each,
same NRO for the two renderers (the Renut build still had the native renderer linked; only the renderer key changed),
CPU 1887 MHz, GPU 844.8 MHz, memory 1862.4 MHz:

| | Native Vulkan renderer (NVK) 540p | NVK 720p | Renut on deko3d 720p |
|---|---|---|---|
| Frame rate | 59.9 | **53.5** | 59.9 |
| GPU per frame | 6.9 ms | 9.3 ms | 8.4 ms |
| Ring / render thread per frame | 7.4 ms | **18.6 ms** | 2.8 ms |
| CPU total (of 400 %) | 167 % | 155 % | 110 % |

Renut at 720p with the GPU at 768 MHz (the floor the release targets in handheld mode):

| Scene | Frame rate | GPU per frame | Render thread per frame |
|---|---|---|---|
| Training fight, 2 minutes | 59.9 | 8.8 ms | 2.8 ms |
| Attract-mode demo fights, 4 stages, 12 minutes | 59.2-59.9 | 8-11 ms | 4.7-8.2 ms |

Release NRO: 56.3 MB (the NVK build: 66.7 MB), no game shader inside, no Vulkan driver linked.

## 2. The projects this is built from

| Project | What it gave | Licence |
|---|---|---|
| [reNut](https://github.com/masterspike52/reNut) by masterspike52 | The Direct3D-level renderer itself (`app/src/renut_engine`) | No licence file; its author gave permission for its use in these ports. Ask before you take it |
| banjo-nx by ChanseyIsTheBest (Banjo-Kazooie: Nuts & Bolts; not public) | The deko3d back end for plume, the shader chain (`tools/deko3d_*.py`), the GLSL passes, the render-thread tuning (`banjo_renut_*`), late input and display sync, all as sa2-nx carries them | GPL-3.0 (as carried by sa2-nx) |
| [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx) by ChanseyIsTheBest | The port usf4-nx was copied from, Renut retargeted to an XDK 2.0.21076 game, the no-NVK SDK option, the release-shader build, the guide above | GPL-3.0 |
| [sa2-nx-installer](https://github.com/ChanseyIsTheBest/sa2-nx-installer) (commit `51c73ee`) | The page that makes `deko3d_glsl.bin` in the browser (`lib/deko3d*.js`, the WebAssembly build script) | GPL-3.0 |
| [peggle2-nx-installer](https://github.com/ChanseyIsTheBest/peggle2-nx-installer) | Where the browser shader chain and `renut_hlsl_frontend.cpp` were first written | GPL-3.0 |
| [plume](https://github.com/renderbag/plume) by renderbag | The graphics API layer Renut draws through (D3D12/Vulkan on the PC, deko3d on the Switch) | MIT |
| [deko3d](https://github.com/devkitPro/deko3d) and [uam](https://github.com/devkitPro/uam) by fincs | The Switch's graphics API and its GLSL compiler (Mesa's nouveau code generator); uam carries SwitchWakerHD's and banjo-nx's patches | zlib; MIT (Mesa files); MPL-2.0 (patches) |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (reblue fork, as reNut carries it) | Xenos microcode to HLSL | MIT |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) commit `22b22f56` | SPIR-V to GLSL | Apache-2.0 |
| [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler) | HLSL to SPIR-V (1.8.2407 natively, v2025.1 in the browser) | NCSA |
| [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) by NebadasSwifty | The native PM4 renderer usf4-nx used before, the profiler | GPL-3.0 |
| [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) and [nfsmw-nx-installer](https://github.com/StevensND/nfsmw-nx-installer) by StevensND | The Horizon layer of the SDK, the installer page's structure and service worker | GPL-3.0; BSD-3-Clause (SDK parts) |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, with Xenia's code | Recompiler, kernel, runtime | BSD-3-Clause |
| usf4-nx ([repository](https://github.com/Thorhax/usf4-nx), [installer](https://github.com/Thorhax/usf4-nx-installer)) by Thorhax | This port; v0.8.0 is commit `a8edfaf`, the installer `951b2a3` | GPL-3.0 |

Every change to third-party code goes to `patches/`, every project gets a row in `THIRD_PARTY_NOTICES.md`, and the
release's `LICENSES.txt` lists what is linked into the NRO (for 0.8.0: deko3d, uam, plume, reNut, Mesa's nouveau code
through uam; no NVK, no Expat).

## 3. Architecture in one page

```text
game's Direct3D (D3DDevice_*, Swap, KickOff, fences, Resolve, Clear)
  --hooks (app/src/renut_engine/gpu/d3d/hooks)--> Renut: reads the device's own state, streams, textures, targets
  --device deltas, queued--> one render thread replays each draw on plume
  --plume_deko3d--> deko3d command buffers --> the GPU
shaders: game containers --Renut's XenosRecomp--> HLSL --DXC--> SPIR-V --SPIRV-Cross--> GLSL --passes--> package
         package beside the NRO (deko3d_glsl.bin, made by the installer page)
         --uam on the console, once per variant, on the background core--> DKSH (kept in deko3d_shaders.bin)
```

There is no PM4 ring for the GPU, no EDRAM model and no Vulkan driver. The game's Direct3D still runs for everything
that is not a GPU command, so device memory the game reads stays correct.

Why it is faster: deko3d writes the GPU's command words in user space, with no validation and no descriptor
management. banjo-nx measured NVK's per-call costs on the A57 at about 3 µs a pipeline bind and 2.4-2.9 µs a draw. In
USF4 the native renderer's ring thread needed 7.4 ms a frame at 540p and 18.6 ms at 720p for the same fight Renut
records in 2.8 ms at 720p.

## 4. Timeline: what was done, in order

1. **Census** ([renut-census.md](renut-census.md)): USF4 links XDK 2.0.21256, and its device layout turned out to be
   SA2's byte for byte (24,704 bytes, every private offset checked against USF4's own code). Only addresses changed,
   plus: CreateDevice is inlined into the game (Renut's device setup moved to the hardware path, `sub_82201978`); the
   VdGlobalDevice slot is chosen at run time; USF4 calls DrawIndexedVerticesUP, which SA2 never did.
2. **PC first** (`-DUSF4_RENUT_RENDERER=ON`, `sa2_renderer = "renut"`), against the existing renderer. Three things
   were USF4's own:
   - EndTiling's segment end submits straight to the hardware ring, which no longer exists: the first write went to
     guest address 0. It is hooked (`Usf4EndTilingSegment_hook`).
   - Renut's tiling hooks carry N&B's raw offsets, so predicated tiling drew one strip of three. USF4 draws one tile.
   - The translator needed three fixes for USF4's shaders: FOG0 as an interpolator, vertex exports without a declared
     interpolator going to TEXCOORDN, and fetches whose swizzle is all constants writing only the constants. All 874
     containers then translated and compiled.
3. **deko3d in Ryujinx.** It drew the intro, movies, menus, character select and a fight. Stalls at screen changes
   were measured (section 12) and found to be shader compiles and loading work, not deadlocks.
4. **Console bring-up** (section 11 for the rig). Two console-only bugs: the game froze after about 37 s (the log
   network sink, section 13), and menus and fights showed only their top-left 320x180 (section 5).
5. **Performance:** linear texture checks (render thread 6.4 -> 2.8 ms), compile queue priority, late input.
6. **The honest comparison** (section 8), which first came out wrong because the native renderer had no shaders.
7. **Release:** shaders made by the installer page, NVK dropped, button icons, licences, update notes.

## 5. Console fixes that mattered

### A depth target smaller than the colour target clips the draw

Symptom on the console only: the main menu's 3D background and every fight drawn into the top-left 320x180 of the
screen (a quarter of 1280x720 on each axis); the rest blank or the bottom HUD only. The corner was not scaled: it was
the frame's real top-left pixels.

Cause: USF4 leaves the 320x180 depth target of a quarter-size pass bound while it draws full-screen passes into the
1280x736 front target. On the Xbox 360 both live in EDRAM and nothing clips. On Maxwell, rendering is limited to the
smallest bound target, so every pixel outside 320x180 was discarded. Ryujinx does not enforce that, so the emulator
never showed it. Renut already logged it ("Draw binds a depth target smaller than its colour target") without acting.

Fix (`app/src/renut_engine/gpu/render/draw.cpp`): a draw whose RB_DEPTHCONTROL has neither depth test nor stencil
(bits 1 and 0) does not bind a depth target smaller than its colour targets; a colour clear without the depth/stencil
flags (0x10, 0x20) does the same. A draw that does test depth against a smaller target is logged ("clipped on
Maxwell") so a remaining case shows up. sa2-nx's guide calls this "a GPU exception on Maxwell: log it"; in USF4 it was
silent clipping, not an exception.

### Renut's depth hand-over looked guilty and was not

`banjo_deko3d_depth_handover` (a whole-target depth resolve not drawn, its texture sampled from the target's image)
was the first suspect, and turning it off made the menu right. The fight was still clipped with it off, which pointed
at the depth size instead. With the size fix in, the hand-over renders correctly and saves about 0.4 ms of GPU a frame,
so it stayed on. Bisect with the scene that still fails, not the one that improved.

## 6. Performance work

### Find where the render thread goes before changing anything

The SDK's profiler (`sdk/src/ui/switch_perf.cpp`) samples every thread's stack at 1 kHz with
`switch_profile_stacks = true` and writes `logs/rex/rex_profile.log` beside the NRO. Symbolize with addr2line on the
unstripped ELF (`out/nx-renut/usf4`, not the `.stripped.elf`), inside the `devkitpro/devkita64` image. In a fight about
70 % of the render thread's busy time was in `banjo::pmull_fingerprint::Fingerprint`, under
`RefreshLinearLocked -> CheckedHash -> ContentHash64` (textures) and `BindStreams` (vertex streams).

### Stable linear textures: spot-check, then verify (`usf4_renut_static_linear_textures`)

A per-texture diagnostic (`TextureHashReport`, a line every 10 s) showed the render thread hashing **11.3 GB every
10 s** (about 19 MB a frame): a 2048x2048 and several 1024x1024 DXT5 textures of the stage and characters, stored
linear in guest memory and hashed whole every frame they were bound. Tiled textures already get a cheap sampled hash
(the first and last 256 bytes and eight 64-byte probes).

sa2-nx avoids this with store cards: the generated code marks every 4 KB page it writes, and a copy whose pages are
clean is not hashed. usf4-nx's generated code marks no store cards (`g_banjo_store_cards = false`), so that route
would have meant instrumenting every guest store. Instead, in `render/textures.cpp`:

- a linear texture whose full hash was unchanged at 60 checks in a row becomes static;
- a static texture gets the sampled hash every frame and its full hash every 16 frames, staggered by address
  (`(frame + (va >> 12)) % 16`), so they do not all land on one frame;
- any change either hash finds sends it back to full hashes every frame and uploads it.

Result: 11.3 GB -> 0.7 GB hashed per 10 s, render thread 6.4 -> 2.8 ms a frame, and on the busiest stage the render
thread went from 16 ms (at the edge of the frame) to under 10 ms. The cost: a CPU write that both the samples and the
full hash miss for up to 16 frames shows up to 16 frames late. If your game writes into large linear textures
(a glyph atlas, a decal map), watch for that or keep the key off for those textures.

### Compiles a draw waits for go first

The uam compile thread took jobs FIFO, so a compile the render thread waited for could sit behind the pipeline
prebuild worker's jobs. `plume_deko3d.cpp` now marks prebuild threads as background
(`Deko3DMarkBackgroundCompileThread`) and inserts a waited-for compile before the first background job. (Each prebuild
worker waits for its own compile, so at most one to three background jobs are ever queued; the gain is small but the
worst case is gone.)

### First play: the shader compile burst

With empty caches, the first fight's loading screen held one 7.6 s frame: about 90 shaders compiled at 84 ms each.
It happens once; `deko3d_shaders.bin` and `pipeline_list.bin` keep them. In Ryujinx the same compiles took about
0.5 s each (25 at the first fight made a 12.9 s frame), so emulator timings are not a guide here. The next step is to
prebuild a known list during the intro and the menus (section 14).

## 7. Input latency

banjo-nx's late input (`banjo_late_input`) delays the frame's first controller read by the slack the game thread had
at its Swap. USF4 is paced by its own frame limiter (`sub_82756DE8`, already turned from a Sleep(0) spin into a
sleep by `usf4_cpu_frame_limiter_sleep`): the game reads input right after Swap, works about 8 ms, then the limiter
waits about 8 ms before the next Swap. That wait is not at the Swap, so late input saw no slack and did nothing.

The fix counts the limiter's wait as slack. At the limiter's first look in a frame, the time still missing to the
frame's due point (less a margin) is reported (`NoteFrameLimiterSlack` for Renut, `usf4::late_input` for the native
renderer, `src/usf4_job_wait.cpp`). The read is delayed by the least slack of the last 16 frames, so a run of slow
frames shrinks it at once.

| Margin | Input -> display | Frames over 20 ms in a fight |
|---|---|---|
| no late input | 34 ms | 0 a minute |
| 2 ms (late input's own) | 29 ms | about 1 a minute |
| 2 + 3 ms (`usf4_late_input_limiter_margin_us = 3000`, shipped) | about 31 ms | 0 in 150 s |

The game's work varies by a few milliseconds frame to frame; a margin that only covers the average costs dropped
frames. Measure the dropped frames, not only the latency.

## 8. Measuring honestly: the renderer comparison

The first comparison said NVK was faster. It was wrong, and the way it was wrong is worth knowing:

- The test card had never had the native renderer's shader library (`sa2_shaders.mesp` + `.idx`); the installer makes
  it for players. Without it the native renderer drew **nothing**: every draw "no VS entry, no PS entry", the
  presented frame "0 draws", and still 60 fps with a few milliseconds of GPU time.
- sys-botbase screenshots of those runs were black. That was taken for a capture limitation of NVK. It was the black
  screen.
- With the library on the card, the native renderer drew, its screenshots worked, and the numbers in section 1 came
  out: NVK cannot hold 60 fps at 720p (ring thread 18.6 ms), Renut holds it with room to spare.

Rules that came out of it:

1. Before comparing renderers, prove each one draws the scene: a screenshot, and a draw count on the frame that is
   presented.
2. Use the same NRO and change only the renderer key, the same clocks, the same scene, the same profiler.
3. The player's `usf4.toml` overrides command-line settings in this SDK. A "720p" run with a 540 toml rendered 540.
   Check the resolution line in the log (`[res] docked mode: render ...`), and change the toml when a test needs it
   (and put it back).
4. Earlier numbers from other builds and clocks are not a baseline. Re-measure both sides.
5. Clocks: the profiler's `clocks:` line says what the console actually ran at.

## 9. Shipping: no game shaders in the NRO

Translated shaders are game data. 0.8.0 does what sa2-nx 1.0.3 did:

- **Build:** `-DUSF4_RENUT_RENDERER=ON -DSA2_RENUT_RELEASE_SHADERS=ON -DREXGLUE_SWITCH_NO_NVK=ON` (usf4-nx's
  `out/nx-release`, with LTO). `tools/renut_release_shaders.py` writes the shader table without SPIR-V
  (`app/out/renut_switch_release`). The built-in defaults of a Renut build set `sa2_renderer = "renut"`,
  `renut_gpu_backend = "deko3d"`, and 720p in both modes (Renut has no 540p: its render scale is a 3-bit code and
  every value is used).
- **No NVK:** usf4-nx's SDK did not have sa2-nx's `REXGLUE_SWITCH_NO_NVK`; it was ported
  (`sdk/cmake/rexglue_switch.cmake`, `sdk/src/ui/switch_no_vulkan.c`). Only four Vulkan symbols are linked by name;
  the stub answers them with "no driver" and supplies `posix_memalign`.
- **The installer page makes `deko3d_glsl.bin`** (usf4-nx-installer `lib/deko3d.js`):
  - the containers come from the build's recipe (the same 874 the native library was made from: EMZ streams of the
    game's archives, game files, `default.xex`'s image, and the 15 shaders Direct3D makes at run time);
  - `wasm/renut_hlsl` is Renut's translator built from **usf4-nx's** `app/thirdparty/reblue-XenosRecomp` (with the
    three USF4 fixes) and sa2-nx's `tools/renut_hlsl_frontend.cpp`; `wasm/spirv_cross` is SPIRV-Cross `22b22f56`.
    Both are built by `wasm/src/deko3d/build_wasm_deko3d.sh` in the `emscripten/emsdk:4.0.10` Docker image;
  - DXC (`wasm/dxc_web`, v2025.1), SPIRV-Cross, the conversion and passes (`lib/deko3d_glsl.js`) and the package
    writer (`lib/deko3d_package.js`, zstd frames of raw blocks, no dictionary; the NRO's reader accepts both);
  - the result is pinned in the build's manifest entry (`"deko3d": { passes, shaders, package_sha256,
    package_size }`) and the page refuses anything else.
- **Check the page's output against your tested package.** `test/deko3d_node.mjs` runs the page's code in Node and
  compares each shader's text with the native chain's `out/deko3d_shaders/glsl_opt`. For USF4, 148 of 874 were
  identical and 726 differed: the native chain used DXC 1.8.2407 and the page v2025.1, so temporaries are renumbered and
  some expressions take equivalent forms (`a - clamp(x - y)` against `a + (-clamp((-y) - (-x)))`). sa2-nx got 97 of
  103 identical because its native chain and page were closer. Since the page cannot run uam, every differing shader
  was compiled with uam on the PC (the alpha-test variants too, `tools/deko3d_glsl_opt.py`'s `check_one`): 0 failures.
  Then the release NRO was run on the console with the page-made package from a fresh state (no DKSH cache, no
  pipeline list): intro, menus, a fight, all right. Ideally, build the native package with the page's DXC so they
  match byte for byte.
- **The early alpha pass:** the native build runs `tools/renut_early_alpha.py`; the page does not. For USF4 it
  rewrote none of the 503 pixel shaders (20 "little", 483 unsupported), so nothing is lost. Check this for your game
  before dropping it.
- **Tell players how to update.** Replacing the NRO alone is not enough for the first deko3d version: it needs the new
  shader file. From 0.7.x: make the update zip on the page, extract it, delete `sa2_shaders.mesp`,
  `sa2_shaders.mesp.idx` and `cache/sa2_native_pipelines.bin`; keep `game_root/`, the saves and `usf4.toml`. usf4-nx put
  this on the release page and the installer page.
- **Icons:** a personal `app/icon_local.jpg` wins at configure time. Move it aside, reconfigure, build the release, put
  it back, and check the published NRO carries `app/icon.jpg` (the bytes of the JPEG are inside the NRO).

## 10. Switch button icons

USF4's menus take each button prompt from a 32x32 texture of its own (Scaleform images): the A, B and X balls, the
RB key, and BACK in two forms. They are redrawn at upload as Switch buttons (`app/src/usf4_button_icons.cpp`, after
the Mortal Kombat: Shaolin Monks port's `port/vk/button_icons.cpp`), by position, matching `input_xbox_layout`
(Xbox A shows as B, B as A, X as Y, RB as R, BACK as -). `usf4_button_icons = "xbox"` turns it off.

How the icons were found and matched:

- A dump option writes each small texture's first level as the renderer uploads it (`usf4_icon_dump_dir`;
  `usf4_dump_textures_dir` with `usf4_dump_textures_max_size` in Renut), once per content. `tools/texture_dump_png.py`
  decodes BC1/2/3, R8 and RGBA8 to PNG and contact sheets; the icons were picked by eye.
- **Hash only the meaningful bytes.** Upload staging rows are padded to 256 bytes and the padding is uninitialised:
  the same icon came out under many hashes until the key became FNV-1a 64 of each row's real bytes.
- **Each renderer has its own bytes.** Renut's RGBA8 has alpha last; the native renderer's untiled data has alpha
  first and its BC3 blocks still have their 16-bit units byte-swapped at that point (the swap happens later). The
  table holds both renderers' hashes; the painter takes the alpha byte and a BC3 swap flag.
- BC3 icons are re-encoded by a small block encoder (alpha by min/max, colour endpoints from the darkest and lightest
  visible texels). The grey and white Switch art compresses cleanly.

## 11. The unattended console test rig

All console work in 0.8.0 ran with nobody at the console. What it took:

| Piece | What it is for | Notes |
|---|---|---|
| hbmenu in title takeover, netloader open (Y) | Starting a new NRO without touching the console | Reopen the netloader after the console's network comes up, or nxlink cannot connect. A bare TCP probe of port 28280 makes hbmenu show "Error getting name length": do not probe it |
| `nxlink` (devkitPro) | Sends the NRO and its arguments: `nxlink -a <ip> -p usf4-nx/usf4_test.nro usf4.nro -- --cvar=value ...` | `-p` is relative to `sdmc:/switch/` and the folder must exist. `nxlink -s` takes port 28771, so the game's log stream uses another |
| sys-botbase (on :6000, LAN only: it has no authentication) | Buttons (`click`, `press`, `release`, `setStick`), screenshots (`pixelPeek`, JPEG as hex), the running title | Closing a game: HOME, X, A. Relaunching hbmenu: hold R while starting the title and choosing the user. The game's buttons are positional (Switch B confirms) |
| [ftpsrv](https://github.com/ITotalJustice/ftpsrv) by ITotalJustice as a sysmodule | Logs, profiles, dumps, the shader package and settings to and from the SD card | Reliable: 20/20 sessions, 2.2 MB/s up, 8.6 MB/s down. sys-ftpd-light was dropped: on the Switch its poll() missed about half of the passive data connections. A file the game holds open cannot be read: close the game first |
| `log_network=<pc ip>:<port>` | The game's log streamed live | The listener must serve each connection on its own thread (section 13) |
| The SDK profiler (`logs/rex/rex_profile.log`) | Per-thread CPU, stacks, clocks, every 10 s | Stack sampling costs about 100 % of a core; leave it off for timing runs |
| Horizon OC's override through the game (`usf4_test_clock_override = "CPU_MHZ,GPU_MHZ"`) | Testing at stock or chosen CPU and GPU clocks | Only CPU and GPU, never memory: a memory clock change can need a reboot, which on this console lands in hekate with nobody there. The override stays after the game exits; set the previous values back afterwards (read them from the profiler's `clocks:` line first) |
| Auto-sleep set to Never (System Settings > Sleep Mode) | Long unattended sessions | A sleeping console drops the network and cannot be woken remotely |

Scripts (in the session's scratch space; easy to rewrite): relaunch hbmenu and open the netloader; send an NRO and
take screenshots on a schedule; walk from boot to the main menu, to a Training fight, or to Arcade; a random-input
player for long runs; a per-stage summary of the 10-second renderer lines (the log names each stage as it loads,
`battle\stage\XXX.`). The title screen's attract-mode demo fights give random-stage fights with no input at all,
which is the easiest way to cover several stages.

## 12. Diagnostics worth building early

- **A wait tracker** (`sdk/src/system/wait_watch.cpp`): every guest wait (`XObject::Wait`, `WaitMultiple`,
  `SignalAndWait`, `KeDelayExecutionThread`) records what it waits on, since when and from which game function
  (`ctx.lr`). A watchdog (`usf4_stall_report_s`) dumps it when no frame has been presented for 3 s. In Ryujinx it
  showed that the "stalls" were shader compiles and main-thread loading, not deadlocks.
- **Per-renderer 10-second lines:** frames, render thread busy, GPU per frame, draws, compiles, the late input
  delay, input -> display latency. Renut and the native renderer each print their own.
- **A hitch line** for every frame over a threshold, split into GPU ring wait, job wait, frame limiter and "the rest".
- **The texture hash report** (section 6), and the dumps (section 10).
- On libnx, `std::thread::detach` throws (pthread_detach fails) and ends the process: give a long-lived diagnostic
  thread a leaked `new std::thread` instead.

## 13. Traps, each one paid for

- **The log stream froze the game.** The game stopped presenting about 37 s in, every time on the console. The
  profiler's stacks showed a file thread inside `send()` in the network log sink, holding spdlog's sink mutex, with the
  game thread and the watchdog waiting behind it. The PC listener was still blocked on a dead connection from a
  crashed run, so nobody read the new one and the TCP window filled; `MSG_DONTWAIT` alone did not stop `send()` from
  blocking on Horizon. Fix: the log socket is made non-blocking (`fcntl(O_NONBLOCK)`, `sdk/src/core/logging.cpp`), and
  the listener serves every connection on its own thread with TCP keepalive.
- **A system-module crash looked like our crash.** The first run took the whole console down 1.3 s in. The Atmosphère
  fatal report (`/atmosphere/fatal_errors/report_*.bin`, an "AFE2" record) named program `0x010041544D530000`, which is
  ams.mitm, aborting on a kernel result: not the game. It went away when the memory overclock and undervolt were
  lowered. Decode the report before debugging the game.
- **The native renderer without its shader library draws nothing and reports 60 fps** (section 8).
- **sys-botbase screenshots are black when the game draws nothing.** Do not explain a black capture away.
- **Maxwell clips to the smallest bound target** (section 5); Ryujinx does not.
- **Upload staging padding is uninitialised:** never hash a whole staging row.
- **A texture's guest size, not its pooled image's size,** identifies it in the native renderer: its pool can give a
  larger image.
- **The player's toml wins over the command line** (section 8).
- **Docker cannot see files outside its mounts:** a build script in a scratch folder was "No such file or directory"
  inside the container. Keep build scripts under the mounted tree.
- **Do not run the SDK's ninja on the host** for a tree configured in the devkitA64 image: it rewrote `.ninja_log` and
  forced a full rebuild.
- **The GitHub release API rejects a short commit SHA** as `target_commitish`; use the full SHA or the branch.
- **A Pages deploy that copies a fixed file list** misses new files (`renut_shader_common.h` here): update the list
  with the page.
- **Emulator timings are not console timings,** in both directions: Ryujinx compiled shaders six times slower, and
  hid the depth clipping.

## 14. Open items

- **Prebuild the shaders a fresh install needs** during the intro and menus, from a list shipped with the build (the
  list is shader hashes and states, not game data; `pipeline_list.bin` already drives the prebuild worker), to remove
  the one-time 7.6 s compile on the first fight.
- **Build the native shader package with the page's DXC** so the page's output matches the tested package byte for
  byte.
- **Handheld mode measured on the console** (720p at 768 MHz is expected to hold, from the docked runs at the same
  clock and resolution).
- **Store cards** for usf4-nx's generated code would let Renut skip the remaining stream hashing (about 2.5 GB per 10
  s, about 1 ms of render thread in a fight).
- **Button names in text** (Button Config lists "X: LIGHT PUNCH", "LB", ...) come from the game's strings, not icons.

## 15. Checklist for the next port

1. Follow sa2-nx's guide sections 2-6: census with a source for every offset, Renut on the PC behind a boot key,
   the shader chain with `--check`, deko3d in Ryujinx, drop NVK.
2. Get onto the console early, with the rig in section 11 (netloader, nxlink, sys-botbase on the LAN only, ftpsrv,
   a threaded log listener, auto-sleep off). Decode Atmosphère reports before blaming the game.
3. Look for Maxwell-only differences: depth targets smaller than colour targets, anything Ryujinx is lenient about.
4. Profile the render thread with stack sampling before optimizing. If hashing dominates and you have no store cards,
   consider the static linear texture check. Log a per-texture hash report.
5. If the game paces itself with its own frame limiter, count the limiter's wait as late-input slack, with a margin
   chosen by counting dropped frames.
6. Compare renderers only after proving each draws the scene, with one NRO, one scene, the same clocks, and the
   resolution read from the log.
7. Ship without game shaders: release shader table in the NRO, `deko3d_glsl.bin` from the installer page, translator
   module built from your own translator copy, every page-made shader compiled with uam, the page's package run on the
   console from a fresh state, pinned by SHA-256.
8. Write the update steps and the files to delete on the release page and the installer page.
9. Licences: reNut's permission, deko3d, uam (with its patches' MPL-2.0), plume, Mesa's nouveau code; drop what you
   no longer link.
