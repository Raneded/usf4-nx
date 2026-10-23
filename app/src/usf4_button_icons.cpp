// usf4-nx (2026-10-23): see usf4_button_icons.h.
#include "usf4_button_icons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(usf4_button_icons, "switch", "USF4",
                      "The game's button prompts: \"switch\" draws Switch buttons in place of the Xbox ones (by "
                      "position with input_xbox_layout: Xbox A shows as B, B as A, X as Y, RB as R, BACK as -), "
                      "\"xbox\" keeps the game's own icons");

REXCVAR_DEFINE_STRING(usf4_icon_dump_dir, "", "USF4",
                      "Diagnostic: every 32x32 texture that could be a button icon, written once per content into this "
                      "folder (<hash>_<layout>.bin, rows packed). Empty: off");

namespace usf4::button_icons {
namespace {

constexpr Format kRgba8 = Format::Rgba8;
constexpr Format kBc3 = Format::Bc3;
constexpr uint32_t kSize = 32;

enum class Icon { None, A, B, X, Y, Rb, Back };

// FNV-1a 64 of the icon's bytes, each row (of pixels, or of 4x4 blocks) without its padding to the row pitch: the
// copies of one icon differ only there. From the console's and the PC's dumps (usf4_dump_textures_dir).
struct Known {
  uint64_t hash;
  Format format;
  Icon icon;
};
constexpr Known kKnown[] = {
    {0x11A578D47E0EEED5ull, kRgba8, Icon::A},
    {0xB1D1C8326AC70F4Eull, kRgba8, Icon::B},
    {0xA998D5621BC089C4ull, kRgba8, Icon::X},
    {0x2967D3474A065410ull, kRgba8, Icon::Rb},
    {0x4436FCC04FD23D9Eull, kRgba8, Icon::Back},  // the trial's SHOW INPUT, the command list's EXPLANATION
    {0x95AF78035703C15Eull, kBc3, Icon::Back},    // character select's Controls (the ball with BACK under it)
    // The native Vulkan renderer's bytes (A first, 32-bit units byte-swapped: me/native/sa2_native_draws.cpp)
    {0xEF6AC8FDA56F65ADull, kRgba8, Icon::A},
    {0xA567ED2D7518C400ull, kRgba8, Icon::B},
    {0xE28AE55ADA7D6806ull, kRgba8, Icon::Rb},
    {0x6363DC92A7ADF20Eull, kRgba8, Icon::X},
    {0x838C9FB334D8F2ACull, kBc3, Icon::Back},  // 16-bit units still swapped there
    {0x4FF340753C9FD8C0ull, kRgba8, Icon::Back},
};

uint64_t RowHash(const uint8_t *data, uint32_t rows, uint32_t row_bytes, uint32_t row_pitch) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (uint32_t r = 0; r < rows; ++r)
    for (uint32_t i = 0; i < row_bytes; ++i)
      h = (h ^ data[size_t(r) * row_pitch + i]) * 0x100000001B3ull;
  return h;
}

// 5 x 7 letters, a row per byte, the high bit of the five on the left (as in Shaolin Monks)
struct Glyph {
  char c;
  uint8_t rows[7];
};
constexpr Glyph kLetters[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
};

// RGBA, 32 x 32, cleared to transparent and drawn over
struct Canvas {
  uint8_t px[kSize * kSize * 4] = {};
  void Blend(int x, int y, const uint8_t rgba[4], float cover) {
    if (x < 0 || y < 0 || x >= int(kSize) || y >= int(kSize) || cover <= 0) return;
    uint8_t *p = px + (size_t(y) * kSize + size_t(x)) * 4;
    cover = std::min(cover, 1.0f) * rgba[3] / 255.0f;
    const float a = p[3] / 255.0f, out = cover + a * (1 - cover);
    for (int i = 0; i < 3; i++)
      p[i] = uint8_t(out > 0 ? std::lround((rgba[i] * cover + p[i] * a * (1 - cover)) / out) : 0);
    p[3] = uint8_t(std::lround(out * 255));
  }
};

// The Switch's buttons: dark grey with a light rim and a white label. Grey, so the same whichever of R and B the host
// format keeps first.
constexpr uint8_t kFill[4] = {48, 48, 48, 255};
constexpr uint8_t kRing[4] = {205, 205, 205, 255};
constexpr uint8_t kInk[4] = {255, 255, 255, 255};
constexpr uint8_t kShadow[4] = {0, 0, 0, 110};

// coverage of a pixel by a shape, 4 x 4 samples
template <class Inside>
float Cover(int x, int y, Inside inside) {
  int n = 0;
  for (int sy = 0; sy < 4; sy++)
    for (int sx = 0; sx < 4; sx++) n += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
  return n / 16.0f;
}

template <class Inside>
void Fill(Canvas &c, const uint8_t rgba[4], Inside inside) {
  for (int y = 0; y < int(kSize); y++)
    for (int x = 0; x < int(kSize); x++) c.Blend(x, y, rgba, Cover(x, y, inside));
}

// a letter (5 x 7 scaled to 'height'), or '-' as a bar, centred on (cx, cy)
void Label(Canvas &c, char letter, float cx, float cy, float height) {
  if (letter == '-') {
    const float hw = height * 0.42f, hh = height * 0.11f;
    Fill(c, kInk, [&](float x, float y) { return std::fabs(x - cx) <= hw && std::fabs(y - cy) <= hh; });
    return;
  }
  const Glyph *g = nullptr;
  for (const Glyph &l : kLetters)
    if (l.c == letter) g = &l;
  if (!g) return;
  const float scale = height / 7.0f, x0 = cx - 2.5f * scale, y0 = cy - 3.5f * scale;
  Fill(c, kInk, [&](float x, float y) {
    const int col = int(std::floor((x - x0) / scale)), row = int(std::floor((y - y0) / scale));
    return col >= 0 && col < 5 && row >= 0 && row < 7 && (g->rows[row] >> (4 - col) & 1);
  });
}

// a face button where the Xbox ball was (pixels 3..28 across, 4..29 down), with a soft shadow under it as the game's
void FaceButton(Canvas &c, char letter) {
  const float cx = 16.0f, cy = 16.5f, r = 12.5f;
  Fill(c, kShadow, [&](float x, float y) { return std::hypot(x - cx, y - (cy + 1.5f)) <= r + 0.5f; });
  Fill(c, kRing, [&](float x, float y) { return std::hypot(x - cx, y - cy) <= r; });
  Fill(c, kFill, [&](float x, float y) { return std::hypot(x - cx, y - cy) <= r - 1.6f; });
  Label(c, letter, cx, cy, 13.0f);
}

// a shoulder button: a rounded key with its letter, where the RB key was
void ShoulderKey(Canvas &c, char letter) {
  const float x0 = 2.5f, x1 = 29.5f, y0 = 7.5f, y1 = 26.5f, r = (y1 - y0) / 2, cy = (y0 + y1) / 2;
  auto key = [&](float inset, float dy) {
    return [=](float x, float y) {
      const float ax = std::clamp(x, x0 + r, x1 - r);
      return std::hypot(x - ax, y - (cy + dy)) <= r - inset;
    };
  };
  Fill(c, kShadow, key(-0.5f, 1.5f));
  Fill(c, kRing, key(0, 0));
  Fill(c, kFill, key(1.6f, 0));
  Label(c, letter, 16.0f, cy, 11.0f);
}

// --- BC3: the canvas encoded block by block (the BACK button the character select draws is BC3)
uint16_t To565(const uint8_t *p) { return uint16_t(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3)); }
void From565(uint16_t c, int out[3]) {
  out[0] = ((c >> 11) & 31) * 255 / 31;
  out[1] = ((c >> 5) & 63) * 255 / 63;
  out[2] = (c & 31) * 255 / 31;
}

void EncodeBc3Block(const Canvas &c, uint32_t bx, uint32_t by, uint8_t out[16]) {
  const uint8_t *px[16];
  for (uint32_t i = 0; i < 16; ++i) px[i] = c.px + (size_t(by * 4 + i / 4) * kSize + (bx * 4 + i % 4)) * 4;
  // alpha: its extremes and the six steps between them
  uint8_t amax = 0, amin = 255;
  for (const uint8_t *p : px) amax = std::max(amax, p[3]), amin = std::min(amin, p[3]);
  int apal[8] = {amax, amin};
  for (int i = 1; i <= 6; ++i) apal[i + 1] = ((7 - i) * amax + i * amin) / 7;
  uint64_t abits = 0;
  for (int i = 0; i < 16; ++i) {
    int best = 0;
    for (int k = 1; k < 8; ++k)
      if (std::abs(apal[k] - px[i][3]) < std::abs(apal[best] - px[i][3])) best = k;
    abits |= uint64_t(amax == amin ? 0 : best) << (3 * i);
  }
  out[0] = amax;
  out[1] = amin;
  for (int i = 0; i < 6; ++i) out[2 + i] = uint8_t(abits >> (8 * i));
  // colour: the darkest and lightest of the visible pixels (all of them if none is)
  int lo = -1, hi = -1, llo = 1 << 30, lhi = -1;
  for (int pass = 0; pass < 2 && lo < 0; ++pass)
    for (int i = 0; i < 16; ++i) {
      if (pass == 0 && px[i][3] < 16) continue;
      const int l = px[i][0] * 3 + px[i][1] * 6 + px[i][2];
      if (l < llo) llo = l, lo = i;
      if (l > lhi) lhi = l, hi = i;
    }
  uint16_t c0 = To565(px[hi]), c1 = To565(px[lo]);
  if (c0 < c1) std::swap(c0, c1);
  int e0[3], e1[3];
  From565(c0, e0);
  From565(c1, e1);
  int pal[4][3];
  for (int k = 0; k < 3; ++k) {
    pal[0][k] = e0[k];
    pal[1][k] = e1[k];
    pal[2][k] = (2 * e0[k] + e1[k]) / 3;
    pal[3][k] = (e0[k] + 2 * e1[k]) / 3;
  }
  uint32_t cbits = 0;
  for (int i = 0; i < 16; ++i) {
    int best = 0, bestd = 1 << 30;
    for (int k = 0; k < (c0 == c1 ? 1 : 4); ++k) {
      int d = 0;
      for (int j = 0; j < 3; ++j) d += (pal[k][j] - px[i][j]) * (pal[k][j] - px[i][j]);
      if (d < bestd) bestd = d, best = k;
    }
    cbits |= uint32_t(best) << (2 * i);
  }
  std::memcpy(out + 8, &c0, 2);
  std::memcpy(out + 10, &c1, 2);
  std::memcpy(out + 12, &cbits, 4);
}

Icon Identify(const uint8_t *data, Format format, uint32_t row_pitch) {
  const uint64_t h = Hash(data, kSize, kSize, row_pitch, format);
  for (const Known &k : kKnown)
    if (k.hash == h && k.format == format) return k.icon;
  return Icon::None;
}

bool SwitchIconsOn() {
  static const bool on = REXCVAR_GET(usf4_button_icons) != "xbox";
  return on;
}

}  // namespace

uint64_t Hash(const uint8_t *data, uint32_t width, uint32_t height, uint32_t row_pitch, Format format) {
  return format == kBc3 ? RowHash(data, (height + 3) / 4, (width + 3) / 4 * 16, row_pitch)
                        : RowHash(data, height, width * 4, row_pitch);
}

void Dump(const uint8_t *data, uint32_t width, uint32_t height, uint32_t row_pitch, Format format, const char *tag) {
  static const std::string dir = REXCVAR_GET(usf4_icon_dump_dir);
  if (dir.empty() || width != kSize || height != kSize) return;
  static std::mutex mutex;
  static std::unordered_set<uint64_t> seen;
  const uint64_t h = Hash(data, width, height, row_pitch, format);
  {
    std::lock_guard lock(mutex);
    if (!seen.insert(h).second) return;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  char name[512];
  std::snprintf(name, sizeof(name), "%s/%016llX_%s.bin", dir.c_str(), (unsigned long long)h, tag);
  if (FILE *f = std::fopen(name, "wb")) {
    const uint32_t rows = format == Format::Bc3 ? (height + 3) / 4 : height;
    const uint32_t row_bytes = format == Format::Bc3 ? (width + 3) / 4 * 16 : width * 4;
    for (uint32_t r = 0; r < rows; ++r) std::fwrite(data + size_t(r) * row_pitch, 1, row_bytes, f);
    std::fclose(f);
  }
}

bool Candidate(uint32_t width, uint32_t height, Format format) {
  (void)format;
  return width == kSize && height == kSize && SwitchIconsOn();
}

bool Paint(uint8_t *data, uint32_t width, uint32_t height, uint32_t row_pitch, Format format, uint32_t alpha_byte,
           bool bc3_swap16) {
  if (!Candidate(width, height, format) || alpha_byte > 3) return false;
  const Icon icon = Identify(data, format, row_pitch);
  if (icon == Icon::None) return false;
  // By position (input_xbox_layout, the default) the Xbox button shows as the Switch button in its place; by letter
  // it keeps its letter. (input_xbox_layout is the Switch input driver's: read by name, by position where there is
  // none.)
  static const bool by_position = rex::cvar::GetFlagByName("input_xbox_layout") != "false";
  Canvas c;
  switch (icon) {
    case Icon::A: FaceButton(c, by_position ? 'B' : 'A'); break;
    case Icon::B: FaceButton(c, by_position ? 'A' : 'B'); break;
    case Icon::X: FaceButton(c, by_position ? 'Y' : 'X'); break;
    case Icon::Y: FaceButton(c, by_position ? 'X' : 'Y'); break;
    case Icon::Rb: ShoulderKey(c, 'R'); break;
    case Icon::Back: FaceButton(c, '-'); break;
    case Icon::None: return false;
  }
  if (format == kBc3) {
    for (uint32_t by = 0; by < kSize / 4; ++by)
      for (uint32_t bx = 0; bx < kSize / 4; ++bx) {
        uint8_t *block = data + size_t(by) * row_pitch + bx * 16;
        EncodeBc3Block(c, bx, by, block);
        if (bc3_swap16)
          for (uint32_t i = 0; i < 16; i += 2) std::swap(block[i], block[i + 1]);
      }
  } else {
    // The canvas is grey (its three colour bytes equal): only where alpha goes depends on the layout.
    for (uint32_t y = 0; y < kSize; ++y)
      for (uint32_t x = 0; x < kSize; ++x) {
        const uint8_t *p = c.px + (size_t(y) * kSize + x) * 4;
        uint8_t *q = data + size_t(y) * row_pitch + x * 4;
        for (uint32_t i = 0; i < 4; ++i) q[i] = i == alpha_byte ? p[3] : p[0];
      }
  }
  return true;
}

}  // namespace usf4::button_icons
