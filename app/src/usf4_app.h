// Ultra Street Fighter IV: the ReXGlue application object (paths, GPU plugin choice, analysis aids).

#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>

#include "cpu/sa2_enhancements.h"
#include "me/me_native_system.h"
#include "usf4_script_input.h"
#include "usf4_snapshot.h"

class Usf4App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Usf4App>(new Usf4App(ctx, "usf4", PPCImageConfig));
  }

  void OnPostInitLogging() override {
    // The runtime defaults to no GPU plugin: select ReXGlue's Xenos implementation (stage 3, emulated GPU), keeping
    // an explicit command-line or config-file choice.
    if (!rex::cvar::HasNonDefaultValue("gpu_plugin")) {
      rex::cvar::SetFlagByName("gpu_plugin", "xenos");
    }
    // Which renderer enhancements run (sa2-nx's switchable set), in one line; before any guest thread exists.
    sa2::enh::Resolve();
  }

  // Without --game_data_root, use a game_root/ folder next to the executable, and on a PC build tree the
  // repository's assets/game_root. The disc names the executable Default.xex. An explicit path always wins.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) {
      return;
    }
    std::error_code ec;
    const auto folder = rex::filesystem::GetExecutableFolder();
    if (folder.empty()) {
      return;
    }
    for (auto candidate = folder; !candidate.empty(); candidate = candidate.parent_path()) {
      for (const auto& sub : {std::filesystem::path("game_root"),
                              std::filesystem::path("assets") / "game_root"}) {
        auto pack = candidate / sub;
        pack += ".nxpack";
        if (std::filesystem::is_regular_file(candidate / sub / "Default.xex", ec) ||
            std::filesystem::is_regular_file(candidate / sub / "default.xex", ec) ||
            std::filesystem::is_regular_file(pack, ec)) {
          paths.game_data_root = candidate / sub;
          return;
        }
      }
      if (candidate == candidate.parent_path()) {
        break;
      }
    }
  }

  // Input: the SDK's default devices plus, for tests, the scripted gamepad (usf4_input_script[_file]).
  // Stage 4: with sa2_renderer_native = true the graphics system is the native Vulkan renderer (src/me), and the
  // emulated Xenos GPU plugin is not loaded (ReXApp only loads it when config.graphics is empty).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.input_factory = usf4::script_input::CreateInputSystem;
    sa2::enh::ApplyGpuBoost();
    if (me::native::Enabled()) {
      config.graphics = me::native::CreateGraphicsSystem();
      REXLOG_INFO("graphics: native Vulkan renderer (PM4 ring)");
    }
  }

  // Test snapshots of the game image (usf4_snapshot_s), read from the presenter's guest output.
  void OnPostSetup() override {
    usf4::snapshot::Start([this]() -> rex::ui::Presenter* {
      const auto* rt = runtime();
      const auto* graphics = rt ? rt->graphics_system() : nullptr;
      return graphics ? graphics->presenter() : nullptr;
    });
  }

  void OnShutdown() override { usf4::snapshot::Stop(); }

  // Analysis aid: with USF4_DUMP_IMAGE=<file>, write the loaded guest image (image_base..image_base+image_size) so
  // code pointers stored in data can be scanned for (tools/pointer_scan.py). Does nothing unless the variable is set.
  void OnPostLoadXexImage() override {
    if (!runtime() || !runtime()->memory()) {
      return;
    }
    const char* path = std::getenv("USF4_DUMP_IMAGE");
    if (!path) {
      return;
    }
    const uint8_t* image =
        runtime()->memory()->TranslateVirtual<const uint8_t*>(PPCImageConfig.image_base);
    if (std::FILE* f = std::fopen(path, "wb")) {
      std::fwrite(image, 1, PPCImageConfig.image_size, f);
      std::fclose(f);
      REXLOG_INFO("guest image written to {}", path);
    }
  }
};
