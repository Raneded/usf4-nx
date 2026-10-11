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
