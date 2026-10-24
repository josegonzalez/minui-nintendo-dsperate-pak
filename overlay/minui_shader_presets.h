// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak. Compiled into DSperate (GPLv3) by
// patches/0006-shaders.patch.
//
// The Visual FX page's COLOUR, PATTERN and GLOW rows: the names the ini takes and
// the labels the menu shows, in one place so settings.cpp, the frontend and
// the tests agree. Data only, so including it adds no link input. What each
// one does is minui_shader.cpp's business; README.md maps rgds-core's and
// libretro's preset names onto the two rows.
#pragma once
#include "frontend/sdl/settings.h"

namespace ds::sdl::shader {

// In the order of the arrays below.
enum class Colour : u8 {
  Off, Nds, NaturalVision, NdsNaturalVision, DsClassic, DsLite, Natural, Vivid, Bold, Gba,
  WarmPaper, CoolCrisp, PocketGreen, Amber, Count
};
enum class Pattern : u8 {
  Off, SharpBilinear, Lcd, ZfastLcd, Scanlines, SoftGrid, DsClassic, DsLite, Gba, RgbStripes,
  Beads, Tiles, Crt, Count
};

// GLOW: auto is the crt pattern's own halation when that pattern is drawn, else off.
enum class Glow : u8 { Auto, Off, Glow, Bloom, Halation, Count };

inline constexpr Choice kColours[] = {
  {"off", "OFF"},
  {"nds", "NDS"},
  {"natural_vision", "NAT. VISION"},
  {"nds_natural_vision", "NDS+NAT. VIS"},
  {"ds_classic", "DS CLASSIC"},
  {"ds_lite", "DS LITE"},
  {"natural", "NATURAL"},
  {"vivid", "VIVID"},
  {"bold", "BOLD"},
  {"gba", "GBA"},
  {"warm_paper", "WARM PAPER"},
  {"cool_crisp", "COOL CRISP"},
  {"pocket_green", "POCKET GREEN"},
  {"amber", "AMBER"},
};
inline constexpr Choice kPatterns[] = {
  {"off", "OFF"},
  {"sharp_bilinear", "SHARP BILIN."},
  {"lcd", "LCD"},
  {"zfast_lcd", "ZFAST LCD"},
  {"scanlines", "SCANLINES"},
  {"soft_grid", "SOFT GRID"},
  {"ds_classic", "DS CLASSIC"},
  {"ds_lite", "DS LITE"},
  {"gba", "GBA"},
  {"rgb_stripes", "RGB STRIPES"},
  {"beads", "BEADS"},
  {"tiles", "TILES"},
  {"crt", "CRT"},
};
inline constexpr Choice kGlows[] = {
  {"auto", "AUTO"},
  {"off", "OFF"},
  {"glow", "GLOW"},
  {"bloom", "BLOOM"},
  {"halation", "HALATION"},
};
inline constexpr u8 kColourCount = static_cast<u8>(sizeof kColours / sizeof kColours[0]);
inline constexpr u8 kPatternCount = static_cast<u8>(sizeof kPatterns / sizeof kPatterns[0]);
static_assert(kColourCount == static_cast<u8>(Colour::Count), "one choice per colour");
static_assert(kPatternCount == static_cast<u8>(Pattern::Count), "one choice per pattern");
inline constexpr u8 kGlowCount = static_cast<u8>(sizeof kGlows / sizeof kGlows[0]);
static_assert(kGlowCount == static_cast<u8>(Glow::Count), "one choice per glow");

} // namespace ds::sdl::shader
