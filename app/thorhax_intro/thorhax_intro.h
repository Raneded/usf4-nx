// Thorhax intro: the short video every Thorhax port shows at startup (Switch only; elsewhere it does nothing).
// Self-contained so it can be copied into another port: this folder plus one call (see README.md).

#pragma once

namespace thorhax::intro {

// Plays the intro on the default window with a CPU framebuffer and audout, then gives both back. Call it on the main
// thread after the config is loaded and before anything opens the window or the audio device (ReXApp::OnPreSetup,
// before the graphics system is created). The first kUnskippableSeconds cannot be skipped; after that any button
// ends it. Does nothing when the thorhax_intro cvar is false.
void Play();

}  // namespace thorhax::intro
