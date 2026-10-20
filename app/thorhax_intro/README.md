# Thorhax intro

The short video Thorhax ports show at startup on the Switch, before the game opens the window and the audio device.
The first 3 seconds cannot be skipped; after that any button (A, B, X, Y, +, -, L, R, ZL, ZR) ends it. The cvar
`thorhax_intro = false` turns it off.

| File | Contents |
|---|---|
| `thorhax_intro.h`, `thorhax_intro.cpp` | The player: a decode thread on core 1, frames shown at the audio clock on a linear libnx framebuffer, the sound through audout |
| `thorhax_intro.mpg`, `thorhax_intro.pcm` | The video (MPEG-1, 1280x720) and its sound (raw 48 kHz stereo s16), linked into the executable |
| `make_intro.sh` | Makes both from a source video with FFmpeg |
| `pl_mpeg.h`, `pl_mpeg_impl.cpp` | [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (MIT, unmodified), the MPEG-1 decoder |
| `thorhax_intro.cmake` | `thorhax_intro_add(<target>)` adds all of it to a target |

## In another port

1. Copy this folder into the port's `app/`.
2. In `app/CMakeLists.txt`, after the executable target is created:
   `include(thorhax_intro/thorhax_intro.cmake)` and `thorhax_intro_add(<target>)`.
3. Call `thorhax::intro::Play()` on the main thread after the config is loaded and before the graphics system or the
   audio device is created: at the start of the app's `OnPreSetup` override in a ReXGlue port.
