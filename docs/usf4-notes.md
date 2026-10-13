# Ultra Street Fighter IV (Xbox 360): analysis notes

Facts about the game that the port relies on, each with where it came from. Addresses are guest addresses of
`Default.xex` unless stated otherwise.

## The disc

- Title ID `434307FC`, media ID `7E76B603`, version 0.0.0.5, disc 1 of 1, XGD3 (game partition at `0x02080000`).
- 1,122 files, 8.17 GB (`tools/extract_iso.py --list`). Besides `$SystemUpdate/` (dashboard update, unused):
  - `Default.xex` (12.7 MB) and `Capture.xex` (3.5 MB): the only code.
  - `.eaf` archives, 4.65 GB in 14 files (`archive/battle/battle.eaf` alone 2.8 GB; `dl0`-`dl2` are the
    Super/Arcade/Ultra content, `dvd_patch/` and `patch_ae2/` patch archives).
  - `.cpk` (CRI File System packs, 313 files), `.emz` (zlib containers, including the shaders in `archive/shader/`:
    `VsUserShader`, `PsUserShader`, `VsBgShader`, `PsBgShader`, `setting_technique`), `.gfx`/`.emb`/`.m4s`
    (Scaleform UI and its localisation).
  - 90 `.wmv` movies, 3.06 GB: ASF, **WMV9 Main@High (`WMV3`) + WMA 10 Professional 5.1** (stream headers).

## Default.xex

| | |
|---|---|
| Image base / size | `0x82000000` / `0x00DD0000` |
| Entry point | `0x82212AE0` |
| Encryption / compression | retail AES / basic (no LZX) |
| XDK | 2.0.21256 (all XDK libs); compiler 16.0.11886 (`C1`, `C2`), `LINK` 10.0.11886 |
| Code (`.text`) | `0x82170000`-`0x82A6CD14` (`REX_CODE_BASE`/`REX_CODE_SIZE` in the generated pch) |
| `.pdata` | 26,003 function entries |
| Prologues | 25,793 `mflr r12` (Xenon MSVC), 2 `mflr r0`: one compiler throughout |

Static libraries (XEX header `0x200FF`): XAPILIB, XBOXKRNL, XNET, XONLINE, LIBCPMT, XHV2, XPARTY, XAPOBA, XAUDIO2,
XMCORE, XHTTP, XAUTH, XMP, D3DX9, XGRAPHC, **D3D9LTCG**. Imports: 222 from `xam.xex`, 339 from `xboxkrnl.exe`.

- **Direct3D is the link-time-code-generation build (`D3D9LTCG`)**: its functions can be inlined into the game, so
  matching them by name (`tools/d3d_match.py`) is expected to find fewer clean hook points. To check in stage 4.
- Middleware (version strings in the image): CRI Audio 3.43.05, CRI File System 2.16.22, CRI ADX decoder 1.00.07,
  CRI AHX 1.90 (all "xdk21256.3", Jan 30 2014); Scaleform GFx; Lua 5.1.1.
- Movies are played by the XDK's WMV decoder linked into the game (paths
  `xdk\del\codecs\wmv\codecdsp-cayenne\video\wmv\decoder_c9e\dxva_pk\xenon\...` with GPU shaders `Shader_Deblock*`,
  `Shader_Mocomp*`). The SDK already carries an FFmpeg WMV3 configuration from Mass Effect
  (`sdk/thirdparty/FFmpeg/config_masseffect_wmv3.h`).

## Capture.xex

Image base `0x88000000`, module flags `0x9` (title DLL), entry `0x882D6218`, same XDK. The replay recorder for the
YouTube upload feature: `[EmCaptureXenon]` log strings, an ASF writer, the XAV encoder, XHTTP/XAUTH, and in
`Default.xex` "No youtube account linked to the authorised google account", `UPDATE:\capture.xex`. The service no
longer exists. Not translated; the plan is to keep the game from loading it (check at the first boot).

## setjmp / longjmp

Lua reports errors with `longjmp`. Neither function has a `.pdata` entry; found by shape (stores of the non-volatile
registers into r3's buffer and the mirrored loads) and set in `app/overrides.toml`:

- `setjmp` `0x8251C1E0`: if the pointer at `0x82C62A24` is set, tail-calls it (`bnectr`); else saves f14-f31
  (0..136), r13-r31 (152..296), the vector registers (stvx128), cr/lr (304/308), r1 (144), zeroes 312, returns 0.
- `longjmp` `0x82517AF0`: the mirror image through r7; lr from 308, r1 from 144, cr from 304, returns r6. With a
  non-zero 312 it takes an unwinding path (`0x82517DDC`).

## Code generation

First run (2026-10-11, rexglue 0.10.0 of the sa2-nx fork, Linux, clang 19.1.7 with `-march=x86-64-v2`):
4 "target not in any function" errors, all tail calls to small functions without `.pdata` entries, declared in
`app/overrides.toml`. Second run: 307 files, 39,960 functions, 101 s.

Jump tables (`tools/switch_check.py --code 0x82170000:0x82A6CD14`): 1,384 `bctr` (1,179 indirect, 108 byte, 63
halfword, 34 absolute tables); generated 198 switches and 803 indirect jumps; **0 truncated, 0 wrong targets,
0 missed**. Five without a bound in the code: three function-pointer tables (all entries are function starts, kept
as indirect calls) and two switches (`sub_82574850` at `0x82574D5C`, 17 cases; `sub_8262B390` at `0x8262BAB4`,
16 cases) whose index is not compared before the jump: unverified, a candidate if a "Switch case out of range" shows
up. Twelve odd shapes are indirect calls through tables in `.data`; `tools/pointer_audit.py` covers their targets.

Pointer-reached functions (`tools/pointer_fixpoint.py`, 2026-10-11): pass 1 over 20,493 unclaimed gaps found 170
candidates and accepted 166 (written to `app/overrides.toml` with where each pointer is stored). They exposed one
more tail-call target, `0x82950E80`. The next pass found 2 candidates and accepted none: fixpoint.
`tools/pointer_audit.py` afterwards: 40,392 registered functions; one vtable-like pointer to a non-start,
`0x82950F60` (pointer at `0x8212A5A8`), rejected by the fixpoint only because a one-word stub inside it had been
registered on its own; declared with `size = 0x40`. Also reported, not acted on yet (most are SEH/EH continuations or
resume addresses built with lis/addi; watch for them in boot crashes): 14 code addresses built in code, e.g.
`0x82510420`, `0x825122E8`, `0x8251C978` (`lwz r3,N(r31)` after a branch: SEH handler bodies) and `0x82534F5C`,
`0x8253CC84`, `0x8257C268`, `0x825CCD14` (`lis r11,0x8209` after a bctr). The rest are SEH scopes (204), jump-table
targets (190), EH state maps (38) and other data (160).

Gap classes not swept yet: `tools/gap_fixpoint.py` (code between functions that nothing reaches statically). Run it if
a boot dies in code that exists in the XEX but not in the C++.

## Tool changes in this tree (from sa2-nx)

- `tools/pointer_fixpoint.py`, `tools/gap_fixpoint.py`, `tools/pointer_audit.py`, `tools/pointer_scan.py`: the
  project name (`usf4_manifest.toml`), `Default.xex`, USF4's code range; the code range is read from the generated
  pch (`REX_CODE_BASE`), where this SDK writes it; on Linux the regeneration step runs `tools/codegen.sh`.
- `tools/codegen.sh` (new): the Linux codegen entry point. `tools/linux/boot_test.sh` (new): the boot test on a hidden
  Xvfb display.
- The host tools build on Linux with clang 19 (the SDK needs >= 18) and `-march=x86-64-v2` (without it
  `sdk/src/core/memory.cpp` fails: `_mm_shuffle_epi8` needs SSSE3); `libx11-xcb-dev` was the only missing package.

## First boots on the PC (stage 3, emulated GPU)

Linux, Intel HD 530 (Vulkan, Mesa ANV), hidden Xvfb display, `tools/linux/boot_test.sh`, 2026-10-11.

1. `librexruntimerd.so` not found: the SDK writes its shared libraries to `sdk/out/linux-amd64`; the harness sets
   `LD_LIBRARY_PATH`.
2. Died in audio init: SDL's `dummy` driver is not compiled into this SDL (only ALSA, PipeWire, PulseAudio). Muted
   output through the normal driver works.
3. `[FATAL] Call to invalid or unregistered function at guest address 0x8265BC98`: a qsort comparator whose address
   is built in code (declared in `app/overrides.toml`, evidence there).
4. **Runs** 45 s, then 180 s, no crash: CRIWARE and Scaleform logos, the opening movie (`movie\opening\opening.wmv`,
   decoded by the game's own WMV decoder and its GPU shaders, so no FFmpeg replacement was needed for it), the
   title logo, then the attract-mode demo fight (Ken against Crimson Viper, skate-park stage) fully drawn. Audio
   streams with no gaps (3,752 frames of data per 10 s). 197 graphics pipelines created in 3 minutes.
   Snapshots: `out/pc/captures/` (`usf4_snapshot_s`, read from the presenter's guest output; the X window itself
   shows black on Xvfb).

Seen in the log, harmless so far: `UPDATE:` paths fail ("device not found": the game looks for a title update first,
then reads the disc); `game:\test\...` debug files missing; `XamVoiceSetMicArrayIdleUsers` stub (voice chat).
`Capture.xex` was not requested in 3 minutes of boot and attract mode.

Not verified yet: input (menus, a match), the frame rate (nothing logs it), saving and loading, the other movies,
comparison against console footage.

## Input, menus and gameplay (stage 3, continued)

Test aids added to the app (2026-10-11):
- `usf4_input_script` / `usf4_input_script_file`: a scripted gamepad on player 1 (`app/src/usf4_script_input.cpp`;
  the SDK puts synthetic devices on player 1 and merges them with real pads). Every press/release is logged.
- `usf4_fps_log` (on by default): "[fps]" every 5 s, from a hook on the game's Direct3D swap `sub_821FAA00`, the
  only caller of the kernel's `VdSwap` (import thunk `0x82A6C924`).

Verified with scripts (snapshots in `out/pc/captures/`):
- START skips the opening movie; the title shows PRESS START.
- First start: "No saved data found. Create new saved data?". NO loops back to the prompt (the game re-lists the
  saves); YES creates `SF4_SaveData` through `XamContentCreate` (314 files, 16 MB, in the SDK's save folder
  `~/.local/share/usf4/<xuid>/434307FC/00000001/`), and the next boot opens it instead of asking.
- Main menu (Arcade, Versus, Xbox Live Battle, Challenge, Training, Player Data, Options, Xbox Games Store, Game
  Manual), Arcade settings with its info box, character select (44 characters, Ultra selection), the arcade
  prologue movie with subtitles (START skips it), the world map, the VS screen, both intros, ROUND/FIGHT, a fight
  with HUD, timer and hits landing ("FIRST ATTACK"): Juri vs Ibuki, Pitch-black Jungle.

Frame rate (i7-6700HQ, emulated GPU, RelWithDebInfo, classic codegen):
- Intel HD 530: 58-60 fps in boot, menus and movies; **12.5 fps in the fight**.
- GTX 960M (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json`): **30.0 fps in the fight** (exactly half:
  frames miss the 16.7 ms vblank; host presentation is IMMEDIATE, no host vsync), ~20 fps in menus.
- Where the time goes in the fight (perf, 8 s, NVIDIA): threads `Main XThread` 99.7 %, `GPU Command Processor`
  94 %, `EmMovie` 61 %. Of the main thread, ~95 % is **waiting for the emulated GPU**: `sub_821F54E0` 40 % (the
  XDK Direct3D spin wait: `cctpl`, 32 x `db16cyc`, compares the GPU read pointer at device+11024 with a fence,
  5000 ms timeout), `sub_821F8FA8` 22.5 % (waits for space in the command ring, same read pointer), the
  `savegprlr_29`/`restgprlr_29` helpers they call 23 %, `sub_8252C9D8` 6.7 % (time query for the timeout). The GPU
  command processor thread spends its time in kernel memory copies (`rep_movs_alternative`).
- So on this PC the limit is the emulated GPU, not the game's code; the game's own CPU cost per frame is not
  measured yet (it needs the native renderer, stage 4). `EmMovie` (`sub_82791438`, `sub_82790F78`, and the SDK's
  `rex::perf::IncrementCounter`) burns 61 % of a core during a fight, probably a polling loop: on the Switch's three
  cores that matters, to look at.

## Direct3D (stage 4)

No reference game with named XDK functions is available here (sa2-nx matched against banjo-nx's), so the functions
were found by what they write and where they sit (2026-10-11). The library block is about `0x821E0000`-`0x8220C000`
(`tools/d3d_surface.py --lo 0x821E0000 --hi 0x82212000`: 789 functions, 236 called from outside; from `0x8220E000`
on it is the CRT and XAPI: memcpy `0x8220EEA8`, memset `0x8220FFE8`, XAM wrappers).

The PM4 headers are built with the **predicate bit set** (`0xC0nn_oo01`), so a scan for `..00` headers misses them.

| Function | USF4 | Evidence |
|---|---|---|
| `D3DDevice_Swap` | `sub_821FAA00` | the only caller of `VdSwap` |
| `D3DDevice_DrawVertices` | `sub_821F4B88` | predicated `DRAW_INDX` (`0xC0012201`), auto-index source (`ori ...,128`), no inline data; 3 call sites |
| `D3DDevice_DrawIndexedVertices` | `sub_821F4FA0` | predicated `DRAW_INDX` n=4 (`0xC0032201`, index address and size), no inline data; 1 call site |
| `D3DDevice_DrawVerticesUP` | `sub_821F40A0` | like DrawVertices plus the inline-data allocator `sub_821F7EB0`; 3 call sites |
| `D3DDevice_DrawIndexedVerticesUP` | `sub_821F45A8` | like DrawIndexedVertices plus two inline allocations; 1 call site |
| draw-time shader load | `sub_82200438` | 3rd call of every Draw*'s flush; the only Direct3D writer of `IM_LOAD` (`0x2700`/`0x2701`); reads the bound **VS at device+13072, PS at device+13068**, then the VS object's +896, +872, +904: the same layout as sa2-nx's 2012 XDK |
| `D3DDevice_CreateVertexShader` | `sub_821E5D60` | r3 = container with magic `0x102A1101` (vertex): 255 calls in 40 s of boot (diagnostic hooks on all 182 entry points below `0x8220C000`, `out/diag`) |
| `D3DDevice_CreatePixelShader` | `sub_821E59B8` | r3 = container with magic `0x102A1100` (pixel): 392 calls; same 60-instruction shape as CreateVertexShader |
| container parse helper | `sub_821E5930` | 20 instructions: fills a descriptor (blob, microcode pointer, size) from a container; 18 calls |

The common Draw* flush calls, in order: `sub_821FF6A8` (twice), `sub_82200438` (shader load), `sub_821FEF80` (render
targets and depth: compares five surface pairs at device+12816.. against +13104.., writes `SET_BIN` packets), then
`sub_821FF310` (repeated), `sub_821FF550`, `sub_821FF3D0`, `sub_821F5830`, `sub_821F8EA8` (command buffer space),
`sub_821F7978`.

## Native renderer: first runs (2026-10-11)

Ported from sa2-nx without changes to its core (`app/src/me`, `app/src/me/native`, `app/src/cpu/sa2_enhancements*`,
`sa2_gpu_boost.cpp`, `app/src/profiler`; file and cvar names kept so sa2-nx fixes can be taken over). Not ported:
SA2's hooks, its native Direct3D layer (`sa2_api_*`), its census (`sa2_d3d_api.cpp`) and its GPU wait hook
(`sa2_gpu_wait.cpp`, on SA2's `0x8254F400`; USF4's equivalent spin is `sub_821F54E0`). New: `usf4_me_hooks.cpp` (the
addresses above), `usf4_api_stubs.cpp` (the dropped parts' "off" behaviour). Host tools: glslangValidator built
from `sdk/thirdparty/glslang` into `out/glslang`; DXC 1.9.2609 (Microsoft's Linux release) in `out/tools/dxc`;
`tools/build_shader_library.sh` takes them on Linux.

Shader package: one run through boot, menus and a fight with `usf4_dump_shaders = true` dumped 565 containers (223
VS, 342 PS); all 565 translated (XenosRecomp) and compiled (DXC), 0 failures (`out/shaders/sa2_shaders.mesp`, 15.8
MB). Direct3D's own shaders arrive as immediate loads: 1,766 "unidentified immediate" in 10 s of boot. Next: the
discovery loop (`SA2_SHADER_DISCOVERY`, `tools/wrap_discovered.py`).

Runs with `sa2_renderer_native = true` (GTX 960M): boot logos, opening movie, menus, character select, prologue
movie and a fight (Juri vs Ken, volcano stage, HUD, combos) all drawn, no crash in 200 s. Visible bugs in the fight:
horizontal stripes over the top-left HUD (player 1 health bar and name), a flat yellow sky.

Speed, and what limits it on this machine:
- Fixed: the draw-extent estimator asked `rex::memory::QueryProtect`, which on Linux reads all of `/proc/self/maps`
  (179 times in 4 s; sa2-nx had made the same change on Windows for `VirtualQuery`). Fight 10 -> 15 fps.
- The rest is the test setup, not the game: (1) the Vulkan driver presents to Xvfb by copying every frame through
  the X socket (`writev`, ~16 ms each, ~25 % of the ring thread; minimizing does not stop it without a window
  manager); (2) **the GeForce stays in P8, its idle state (232 MHz core, 405 MHz memory)**, at 40-56 % use: an
  Optimus laptop GPU rendering offscreen next to Xvfb is never boosted. The ring thread spins (`sched_yield`, ~17,000
  a second) waiting for it. So the fight's 15 fps (a quarter of 60: frames held to vblank multiples) measures
  neither the renderer nor the game.

## First Windows test (2026-10-11, the user's PC)

Windows 11, Visual Studio 2026 Community, LLVM 20.1.8, a desktop GPU: all build steps passed (codegen 24.6 s), the
native renderer plays boot, menus and fights at a steady **60 fps** (5-second windows 59-60 fps, worst 54.2 during a
load), and the user reports it plays perfectly and the PC stays cool. Keyboard input needed `mnk_mode = true` (the SDK
leaves it off); `tools/win/play.ps1` now sets it. `tools/win/env.ps1` now finds a VS install through vswhere when
the 2022 Build Tools folder is absent.

Graphics bug seen there and here: bright, horizontally smeared bands over the top-left HUD and across the screen at
waist height (Ryu vs Gen, Ha Long Bay stage; screenshot `usf4-logs/usf4-1.PNG` next to the project). It looks like
the glow/bloom post-process: the bands are horizontal streaks, and the log shows 1/16-size targets (96x45, 80x45:
"resolved logical crop 19410000"). Not caused by sa2-nx's enhancements: with `sa2_cpu_enhancements` and
`sa2_noncpu_enhancements` off the band is still there. Not a missing shader either: the discovery dump
(`sa2_shader_misses`) over a fight found only 9 Direct3D vertex shaders (and 1 rebuilt container); the repeated "PS
identity mismatch" lines (loaded `CAAF1EEB17A85076`, 252 words, and others) are rejected object pairings, after which
the load is identified by its code. Next: a single-frame trace of the post-process passes, against the emulated GPU's
frame.

## The bands: missing stage and character shaders (2026-10-11)

The first shader package came from one run (one stage, two characters). Other stages load their own shaders: the
renderer then paired loads with the nearest known shader ("PS identity mismatch", "variants" its repair tool rejects:
"NON-fetch ALU/CF differs") or left draws without shaders ("unresolved draw shader pairs": 85,896 draws with Direct3D's
own vertex shader `E35EA97ADB2AB935`). That drew the bands.

Fix: `tools/extract_game_shaders.py` reads every container from the game files (USF4 keeps them in EMB archives packed
as EMZ: `#EMZ`, checksum, unpacked size, data offset, raw deflate; 43,522 EMZ streams in 880 files): 456 containers,
290 with microcode the dump lacked (the other 165 differ only in header bytes the game rewrites at load). Plus
Direct3D's own shaders wrapped from the discovery dumps (9) and one rebuilt container: 865 shaders, all translated
and compiled. Result: fights on the Korean street (T.Hawk) and distillery (Gen) stages render cleanly; "unidentified
loads ... immediate 0", no unresolved draws. Still seen: a black vertical bar beside Gen during a hit (one frame);
18 vertex-shader variants dumped (Direct3D's rewrites).

Foliage (the user's emulated-GPU screenshots, jungle stage): leaves show the alpha-to-coverage dither pattern, which
shimmers in motion. USF4 draws its scene with 2x MSAA ("guest MSAA source 01"); the samples averaging at the resolve is
what smooths the dither on the console. The native renderer currently uses a "single-sample approximation" for these
transfers. To look at next.

## Black streaks on hits (native renderer only; 2026-10-11)

The user saw black lines where Juri's attacks land, with the native renderer only (`-Emulated` is clean). Caught
here with burst snapshots (`usf4_snapshot_s = "a-b@0.1"`) and a single-frame black-pixel detector: a thin black
diagonal streak growing from the impact point over 3-4 frames (counter hit on Makoto). Likely cause: Direct3D's
**interpolator relink**. The vertex-shader "variants" the renderer dumps differ from their templates in the vertex
fetches *and* in export instructions (`C80F8002` -> `C80F8003`: an output moved to the next slot; `C80F8002 E2020200`
-> `C8000000 02000000`: an output disabled), i.e. Direct3D renumbered the VS outputs to match the bound pixel shader.
`repair_vertex_variant_declarations` rejects those ("NON-fetch ALU/CF differs"). The dumped variants are whole
containers with the loaded microcode, and this port's XenosRecomp links interpolators by hardware register, so they
went into the package as they are (`out/pc/shaders_variants`, 8 of 9; `vs_vc43fc0b1422d5b97` is skipped by the
translator). Package v3: 873 shaders. After it: "FINAL VS selection audit: 1,213,958 proven, 0 unproven", and no
single-frame black streak in 601 snapshots of a fight with hits (not a proof: the streak needs a particular hit).

Still seen in that fight (bay stage, Juri vs Guy): a dark rectangle standing next to Juri (the same kind as the bar
next to Gen earlier) and a patch of white speckle noise (spray particles?). Next to look at, with the foliage dither.

Diagnostics added: `usf4_trace_until_s` (with `banjo_trace_frame_s`: trace every frame until then); note the log
rotates at 5 MB x 20 files (`log_max_file_size_mb`, `log_max_files`), so long traces need a larger limit.

## The black shapes on hits: reverse-subtract blending (fixed 2026-10-11)

Reproduced reliably in Training mode (`out/pc/training_script.txt`: menu -> TRAINING, Juri vs the dummy, walk in and
attack; the plain grey room makes dark shapes obvious). `banjo_native_skip_vs=434,78` removed them, so they came from
the hit-effect draws (PS n538 = texture x vertex colour to target 0, zeros to target 1). Their blend
`RB_BLENDCONTROL 01860186` is source alpha, **reverse subtract** (op 4), one: the effect darkens the scene a little.
The renderer drew op 4 as `VK_BLEND_OP_SUBTRACT` (source - destination, with a "subtract is used" warning, in both
`FillFixedState` and `OperationBlend` of `sa2_native_draws.cpp`): negative almost everywhere, clamped to black quads
and streaks. Now `VK_BLEND_OP_REVERSE_SUBTRACT`; the effects show as the intended faint grey swish.

Fixed on the way (`me_native_system.cpp`, `PairDraw`): with predicated tiling the GPU replays each command buffer once
per EDRAM tile (USF4 draws its 1280x720 2x MSAA scene in three tiles, scissors 0-544, 544-1088, 1088-1280). A draw
paired by address (`PairByAddress`, which consumes the Draw* record) did not store its pairing for the replays, so
tiles 2-3 drew with the ring's own pick: two library vertex shaders that differ only in fetch fields (n434 in tile
1, n78 in tiles 2-3). The pairing is now stored, and a replay takes the first tile's vertex shader when it still
matches. Trace now also shows target 1 (`rt1`, `blend1`).

## Stage 4 result on the user's PC (2026-10-11)

With update 2 (reverse subtract, tile-replay pairing) and shader package v3 (873 shaders), the native renderer
plays with no graphical glitch the user can see, at 60 fps (5-second windows 57.2-60.0; the dips in the first 30 s
come with 70-118 ms frames: pipelines compiled the first time a state is seen, which sa2-nx's pipeline cache
(`cache/`) removes on later runs). GPU time per 10 s, heaviest passes: the scene (VS249/PS168) 1.56 s, VS777/PS641
0.74 s. Open: the jungle foliage dither (seen with `-Emulated`; the native renderer draws alpha-to-coverage as an
alpha test at 0.5, `banjo_native_alpha_to_mask_test`), the white speckles seen once on the bay stage here.

## Stage 5: the first Switch build (2026-10-11)

Toolchain: devkitPro's Docker image `devkitpro/devkita64` (devkitA64 r29.2, GCC 15.2, binutils 2.45.1, libnx 4.12.0;
devkitpro.org's installer is behind a Cloudflare check that blocks scripted downloads). The build runs in the
container with the repository mounted at the same path; glslangValidator is built in the container
(`out/glslang-dkp`), the host's needs a newer glibc. First build without LTO (`-DUSF4_SWITCH_LTO=OFF`): 759 steps.

The Mesa driver:
- **Mesa 26.2.4** (`mesa-26.2.4-switch-unified-horizon-sdk.zip`, from the user): **unusable as shipped**. Its Rust
  parts (NAK, NIL) reference the `core` and `alloc` crates (hash `R2NHd66MWo`), but no archive of the package
  contains them (libvulkan.a bundles compiler_builtins, nak_rs and nil only; libnak_rs.a / libnil.a have std,
  panic_unwind, ... but no core/alloc): 4,944 undefined references at the link.
- **Mesa 26.2.2** (nfsmw-nx's build, the one sa2-nx shipped; it was in `/opt/devkitpro/portlibs/switch`): complete
  (nothing referenced is missing from the archive), but devkitA64's ld still left 4,939 Rust runtime symbols
  undefined, even for a program that only calls vkCreateInstance: the archive holds two Rust static libraries with
  their own runtime copies, 564 member names twice, and ld does not pull those members through the index. Passing the
  objects explicitly resolved them. `tools/switch/repack_archive.py` repacks the members in order under unique names
  with a fresh index (no object changed): the test program and the game link. The build uses
  `out/tools/mesa-26.2.2-repacked/switch` (the 26.2.2 SDK with the repacked libvulkan.a).

`tools/check_switch_build.py --elf out/nx/usf4 --nro out/nx/usf4.nro --sdk-vulkan ...`: `_start` at 0, Mesa 26.2.2 in
the NRO, 0 vector fused multiply-adds; "flavour" fails by design (classic codegen); three branch-to-self loops, all the
game's own: `sub_8229CCD8` (guest `b .` at 0x8229CCF8) and `sub_822D2A38` (guest `b .` at 0x822D2ADC) are halts,
`sub_82245530` is a list search whose "not found" path loops on registers only (the guest spins there too).

Settings compiled in: `app/usf4_switch_defaults.toml` (native renderer, logging on for the first tests, no asset-pack
build: the game folder is read directly, a 7.7 GB pack would not fit FAT32); the player's `usf4.toml` from
`app/usf4_switch.toml`. Package: `tools/linux/package_switch.sh` -> `out/nx/usf4-nx-switch.zip` (NRO 68 MB + shaders).

## First console test (0.1.0, 2026-10-11) and the profiling build (0.2.0)

0.1.0 (no LTO) on the user's Switch (Atmosphère, Status Monitor, an overclock: memory 2700 MHz): boots, menus and
fights with no visible glitch. Frame rate: boot 45-55, menus 39-52, **fights 10-24**, results ~59. A CPU overclock
(1785 MHz) changed nothing: the fight is **GPU-bound** (Status Monitor: GPU 1228.8 MHz, load 96.6 %, 24 fps;
renderer log: GPU busy ~6.05 s per 10 s in ~56 ms intervals, ~571 ms per second in fence waits; main thread 95 %,
which on PC was mostly Direct3D's spin wait). App memory: 3,281 of 3,285 MB.

Where the GPU time goes, from the PC's per-pass report (10 s at 60 fps, ~4.9 s in all): the scene tiles (pitch 560,
"320" category: VS249/PS168 1.56 s, VS777/PS641 0.74 s) ~2.3 s; EDRAM emulation ~2.1 s (9-pass stencil depth imports
0.63 s, colour aliases 0.69 s, depth imports 0.28 s, copies to textures 0.46 s). USF4's 2x MSAA, predicated tiling
and colour/depth aliasing in EDRAM make the mode-4 transfers far heavier than SA2's.

0.2.0: LTO (6 LTRANS jobs), `sa2_gpu_marks_categories` and `switch_profile_stacks` on, to break the console's GPU
and CPU time down. Checks: one more branch-to-self, `sub_822D43A0`: the guest halt of `sub_822D2A38` inlined into
its caller by LTO.

## 0.2.0 on the console, and what limits the fights (2026-10-11)

LTO made a large difference: fights ~30 fps (28-36, up to 40), menus 60 (user's run with clocks between CPU
1020-2703 MHz, GPU 460.8-1228.8 MHz, memory 2700). `sa2_render_height_* = 540` raised fights to ~37 fps.

GPU time per 10 s of fighting (~290 frames), `sa2_gpu_marks_categories`: EDRAM 9-pass stencil depth imports 1.28 s,
colour alias conversions 1.84 s, depth imports/exports 0.79 s, copies to textures 0.28 s, the scene itself 1.47 s:
**~70 % of the GPU is EDRAM emulation**. CPU (`switch_profile_stacks`): ring thread 88 % (CopyVertices, memcpy,
Fingerprint64: vertex uploads and their dedupe), main thread 69 %, mostly in Direct3D's waits (sub_821F54E0,
sub_821F8FA8).

The 9-pass imports are dominated by depth coming back from a 4x MSAA alias (pitch 280) to the 2x scene (pitch 560):
`D508:280x720:mx1my1 -> 508:560x720` 156,600 tiles and `D0E0 -> 0E0` 55,680 per 10 s (83 %). Those are Direct3D
Clears through the 4x alias (sa2-nx's notes, "How SA2 clears the screen"). The renderer redirects such clears into the
tiles' owners, but declined USF4's: "also writes color (color 0: ...)". With the colour planner's reasons logged
(`[usf4diag]`, first 12 of each): **owner unsupported**, the colour clear's tiles (colour base 000/4F8, 280x720 4x) are
owned by a *depth* view (0E0 or 060, D24S8/D24FS8 4x or 1x), which the planner does not handle; **rect not
tile-aligned**, clears along the predicated-tile seam (x 480-544, 544 not a multiple of 80) and thin strips (y 176-180).

Next (not done): let the colour half clear into depth owners (the colour word as depth = word >> 8, stencil = word &
0xFF, float24 for D24FS8), and handle seam/strip partial tiles. Caution: in these clears the colour surface (base 000,
630 tiles) and the depth surface (base 0E0) overlap in EDRAM, so the order of the two writes in the overlapped tiles
must match the hardware's; verify against the emulated GPU frame by frame before turning it on.

Test harness: `/dev/shm` filled up with `xenia_memory_*` files (4.8 GB each) left by killed runs, and the next start
died with SIGBUS; `tools/linux/boot_test.sh` now removes the ones its run created.

## Clears into depth owners (usf4_edram4_clear_into_depth, 2026-10-12)

`PlanClearColor0EDRAM4` now accepts tiles owned by a D24S8 / D24FS8 depth view: the clear's 32-bit EDRAM word
(byte c = colour channel c, as the depth redirect's colour owners assume) is that view's depth and stencil (depth =
word >> 8 as unorm24 or 20e4, stencil = word & 0xFF), cleared in place with `ClearDepthInPass`, with the same
bookkeeping as the depth redirect's in-place clears (`edram4_bits_stencil`, `StencilRealEDRAM4`, tile versions).
The colour half is committed after the depth half, as before.

Verification (the guide's A/B, on PC): lockstep runs (`vblank_per_swap`, the training script on the game-frame clock
`usf4_input_script_frames`, sa2-nx's recorder `sa2_video_s`/`sa2_video_frame`; `tools/linux/lockstep_run.sh`).
USF4 is not bit-exact between two identical runs (the fighters' animation phase differs: base1 vs base2 mean MAD
0.39, median 0.24, `tools/compare_raw.py --follow`), so the check is "only the fighters differ": base1 vs clr1 mean
MAD 0.34, median 0.20, the worst pairs differ on the fighters only, background and HUD identical. Tested on the
training stage only.

Effect per report (PC): 9-pass depth import tiles 76,110 -> 14,688 (-81 %); the 4x-alias imports `D0E0` (16,512)
and `D508` (46,440) are gone; the "owner unsupported" declines are gone. Still declined: "rect not tile-aligned"
(the predicated-tile seam at x 544 and thin strips), the next item.

## Crash-safe pipeline cache file (2026-10-12)

After a crash on the Switch (a screen recording), the next boot stayed on "Now Loading" until `cache/` was deleted:
`cache/sa2_native_pipelines.bin` had the right size but damaged contents, and the reader only checked sizes. The file
is now version 2 (`sa2_native_draws.cpp`, `kVersionFilePipelines`): its header carries an XXH3-64 of the list and the
cache, and the writer flushes and `fsync`s (`_commit` on Windows) the `.tmp` file before renaming it. On a mismatch
the reader logs `C6: pipeline file damaged (checksum): starting from scratch` and the session rebuilds the cache.
Verified on Linux: a version-1 file is rejected ("from another version"), a new version-2 file is saved after the
60 s timer, read back intact (6 pipelines), and the same file with one byte flipped in the middle is rejected.
The first boot of 0.4.0 therefore starts with an empty cache, once.

## Resolves that "nobody reads" (2026-10-12): not skipped

The copy report's `*** NOBODY READS IT ***` only counted draws that sample the texture. Checked before skipping any:

- **1F6F8000 1280x720 is the presented frame** (`sa2_diag_alias_frames`: every `PRESENT` is 1F6F8000). The Swap reads
  `resolved_` directly. The report now says `(presented by the Swap)` for it, and EDRAM restores from a resolved
  texture count as reads too.
- `sa2_native_lazy_front` (present from the render target) does not help USF4: of 4,156 deferred copies, 4,138 were
  recorded anyway before the target was drawn again; 18 were dropped.
- **19326000 / 19362000 (320x180) are read in some sessions and not in others** (Switch logs: 918 reads over 9
  reports, 4 reports with none), so learning "unread" per address would break the image later in a session.
- What is left: in fights the scene render target is resolved three times in a row (no draws in between) into
  three 1280x720 textures and the 544x720 tile halves; the first of each (Switch: 130AF000 / 13B77000; PC session:
  18328000 / 18DF0000, the addresses move with the stage) is never sampled in our runs. That is ~1 Mpixel per frame at
  540p, ~0.6-0.8 ms of a ~33 ms frame. Not skipped: our test fights never ran an Ultra or Super cinematic, the likely
  reader, and a wrong skip shows a stale image with no way to recover the content.

## Color-0 clears into their consumer (2026-10-12)

With `usf4_edram4_clear_into_depth` (0.3.0) the color half of Direct3D's Clear went to the depth views that owned its
tiles, and the color view that drew there next pulled it back through depth exports (Switch: category 11 from 210 to
~715 ms per 10 s). `usf4_edram4_clear_color_consumer` stamps the color-0 clear's tiles like the depth redirect does
(a site = drawn view + ordinal of its clears in the frame); the view that pulls them next, seen twice in a row, gets
the clear and the tiles (`PlanColor0ConsumerEDRAM4`). Report: `color 0 clears into their consumer`.

PC lockstep A/B (Training, Juri vs Hakan, frames 7000-7119): MAD 0.234 (baseline 0.2-0.4), differences only on the
fighters. Of ~870 color-0 clears per 10 s, 393 went to a color view; declined: 351 with no consumer seen twice (the
next reader alternates), 124 with tiles outside the consumer. GPU per unit of scene time (cat 4): depth exports
-45 %, color aliases -12 %, depth imports (cat 10 + 13) +13 %, EDRAM total -4 % (PC GPU at P8: indicative). The
depth view now imports back what it no longer owns, and 9-pass imports are dear on the Switch: judge it there at
fixed clocks. On in the 0.4.0 Switch defaults; `usf4_edram4_clear_color_consumer = false` in usf4.toml for the A run.

## 0.4.0 on the console, and the next limits (2026-10-12)

User's console, 540p, CPU 1785 / GPU 998.4 / memory 2700: fights ~38.4 fps (0.3.0: 30.8), visibly slowed down (the
game logic runs per frame). Both sides are now at the limit: GPU 26.2 ms per frame (EDRAM 15.7 ms of it: colour
aliases 7.8, 9-pass imports 4.4, imports 2.2, exports 1.3; scene tiles 7.5) and the ring thread 25.9 ms (22.7 us x
1,143 draws). PC timings on this box cannot measure the ring: under Xvfb 26 % of its time is the NVIDIA driver
writing frames to the X socket, and the GTX 960M stays at P8.

- Vertex dedupe content check (`usf4_dedupe_audit_each`, 16): fingerprinting was ~9 % of the console's ring thread and
  never found a stale range. Now one copied range in 16 is fingerprinted and every hit on it is checked; a mismatch
  logs a DIFFERENCE and checks every range for the session. PC fight: 19,204 -> 1,644 fingerprints per 10 s, same
  hits (30 %), no mismatch. Misses (new `C6 vertex dedupe misses` line): 5,395 not in the table, 11,090 copied in an
  older work slot, 11,708 forgotten at a guest wait.
- **Predicated tiling** is the structural cost: 45 % of all draws are tile replays (765,348 of 1,677,758), and the
  renderer already draws the guest's 2x MSAA targets with one sample, so the tiles buy nothing on screen.
  `BeginTiling` is `sub_821FDA68` (r4 flags 1, r5 count 3, r6 rects 0-544 / 544-1088 / 1088-1280, all 720 high),
  called from the engine's render-command handler `sub_82754260` (lr `0x827542DC`), which takes flags, count and
  rects from a prepared command (+4, +8, +12 word offset of the rects). Textures of 1280x720 / 544x720 are created
  through `sub_821F0B80` from `0x82792184`, `0x8253085C`, `0x822A4630`, `0x829FBF80`, `0x827579F4`, and
  `sub_821F0CA0` from `0x827926F4`, `0x82792604`, `0x827579B8`, `0x82757930` (the engine's render-target setup is
  around `0x8275xxxx`/`0x8279xxxx`). Diagnostic used: `out/diag/usf4_diag_tiling.cpp` (not in the build).

## One tile instead of three (`usf4_single_tile`, 2026-10-13)

How USF4 chooses its tiling (found with temporary hooks, `out/diag/usf4_diag_scan.cpp`, not in the build):

- `sub_82971F68` (render-target setup) reads the display height (engine config +2724) and picks a mode: 720 -> 0,
  768 -> 1, 576 -> 2, other -> 3. Mode 0 sets the tile preset r14 = 6 and the MSAA index r24 = 1 (2x).
- Tile presets: a table at `0x820DE8F0` of {count, rects} (index 1 = one tile 0,0-1280,720; 6 = three strips of
  544). `sub_82791BE0` copies the chosen preset into each render-target object (+40 count, +44 rects), and the
  render-command handler `sub_82754260` passes them to `BeginTiling` (`sub_821FDA68`). MSAA index -> type through
  `0x820DE654`; `sub_82792670` creates the EDRAM surface (`sub_821F0CA0`, CreateRenderTarget).
- The same function's second mode switch sizes the tile-local textures ("Main Tile Depth" and others): 720p r26 = 544.
- `sub_8296E028` copies the post-process strips as floats from `0x821295A4` into its object (+684 count, +696 rects).

Three `[[midasm_hook]]` entries (`app/overrides.toml`, `src/usf4_tiling.cpp`) turn this into one tile: preset 1 and
MSAA 0 at `0x82972060`, tile textures 1280 wide at `0x829728B4`, one post-process rect (0,0-1280,720) after
`0x8296E85C`. The game then lays out EDRAM as colour 0 at 0, colour 1 at 736, depth at 1472 (1x, 720 tiles each);
the depth runs to tile 2208, past the 2048 of real EDRAM, and wraps over colour 0's top 160 rows. No visible effect
and no significant transfer pair for it in the PC runs; a 4096-tile EDRAM model would remove it if the console shows
a cost.

PC results (Training fight, 150 frames each): draws per frame 394 -> 202 (-49 %), native draws 444,350 -> 242,620
per 10 s; colour alias operations 11,097 -> 0, depth exports 15,898 -> 976, depth imports 40,497 -> 19,983.
Lockstep image A/B (frames 7000-7119): no offset (best alignment 0,0), MAD 3.45 spread evenly: edges are a little
harder, because the 3-strip path's 2x MSAA did smooth them. GPU time can only be judged on the console.

## 0.5.0 on the console: the EDRAM wrap was the cost (2026-10-13)

0.5.0 with the one tile: GPU load down, but fights at 28-33 fps (CPU 2091, GPU 768 that day). The ring thread was
82 % busy, half of it in `PublishEDRAM4` and the lazy-stencil map lookups it makes: 30 M EDRAM tiles walked per second,
publish fast path 8 % (three strips: 0.36 M tiles, 95 %). The depth's last 144 tiles wrapped onto colour 0's first
rows (above), so the two took those tiles from each other on every draw, and each change of owner bumps the global
epoch that the fast path needs. Fix (0.5.1): the mode-4 EDRAM model has 4096 tiles (`kTilesEDRAM4` in
`sa2_native_targets.cpp`, `kOwnershipEdramTiles` in `me_edram_ownership.h`); a surface is still at most 2048 tiles,
the three strips (0..1918) never reach past 2048. PC one-tile fight: publish 34.2 M -> 1.8 M tiles per 10 s, sync
21.7 M -> 2.1 M.

0.5.1 on the console, one tile, CPU 2193 / GPU 998.4: **~52 fps in fights** (Status Monitor: GPU 67 %, cores 0-2
85-95 %). The limit is now the three CPU cores together: main thread 70 % (22 % of it the D3D spin wait), Vulkan
recording worker 68 % (NVK: `vk_dynamic_graphics_state_copy` 11 %, `nvk_CmdBeginRendering` 7 %, inline data 7 %), ring
thread 65 % (waits 2.8 s per 10 s for the worker in drains), game audio thread 21 %, four job workers 10 % each.
Ring per draw: uploads 5.3 us (VS constants re-uploaded 137,695 times per 10 s, all with new contents), textures
2.2 us, vertices/targets 2.3 us, pipeline 1.4 us, record 1.2 us.

## The D3D GPU wait sleeps (`usf4_cpu_gpu_wait_sleep`, 2026-10-13)

`src/usf4_gpu_wait.cpp`: sa2-nx's `sa2_gpu_wait.cpp` on USF4's copy of the XDK iteration function `sub_821F54E0`
(same record and device offsets: [0] device, [8] read pointer seen, [12] tick; device +11024 read-pointer word,
+11069 bit 0x02 hung; code hash checked, FNV 0x142E8261D1F076A1 over 0xD8 bytes). It sleeps until the ring thread
reports progress (at most `usf4_gpu_wait_max_us`, 1000) only when nothing can have changed since the previous
iteration. PC fight: ~8,000 sleeps and ~8.5 s asleep per 10 s on the main thread instead of spinning. The enhancement
use report (`[enh] uses in the last 10 s`) was not started in USF4; `usf4_app.h` now starts it.
