// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak. Compiled into DSperate (GPLv3) by
// patches/0006-shaders.patch.
//
// rgds-core's screen shaders, and the libretro ones it ships, on the CPU.
//
// They are GLES2 fragment shaders there, run by hooking SDL_RenderCopy. The
// pak's devices draw through fbdev or the Allwinner display engine, where
// there is no GL stage to run them in, so each preset is split in two:
//
//  - COLOUR, a function of the pixel alone, baked into one table with an
//    entry for each of the DS's 2^18 colours and looked up on each
//    256-pixel DS line before it is scaled. That works on every display tier.
//  - GLOW, light spread from bright parts onto their surroundings, blurred
//    at DS resolution after COLOUR, so it also works on every tier.
//  - PATTERN, a function of where a panel pixel sits inside its DS pixel,
//    precomputed per view as one row of multipliers per class of panel row,
//    and applied to the scaled rows on the scanline tiers. The patterns that
//    only darken are also drawn as the display engine's overlay layer.
//
// The two compose as the shaders do: colour first, then the pattern scales
// it. Where a shader reads its neighbours (natural_glow, natural_smooth,
// vivid_smooth) only its colour is kept. TECHNICAL.md has the mapping.
//
// Credits, with the licence each original carries (THIRD_PARTY.md):
//  - The RG DS presets: kleinrein/rgds-core, GPL-3.0-or-later.
//  - lcd1x: jdgleaver, GPL-2.0-or-later, from Gigaherz's lcd3x (public domain).
//  - zfast_lcd: Greg Hogan (SoltanGris42), GPL-2.0-or-later.
//  - nds-color: hunterk and Pokefan531, public domain.
//  - The GBA colour model: Pokefan531, public domain.
//  - sharp-bilinear: Themaister, public domain.
//  - Natural Vision: ShadX, ported by Hyllian and parameterised by
//    Sp00kyFox, with no licence stated, so its maths is written out here
//    afresh rather than copied.
#pragma once
#include "frontend/sdl/minui_shader_presets.h"
#include "core/gpu/gpu.h"
#include "core/types.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace ds::sdl::shader {

// The ini's names, kColours/kPatterns. False on an unknown name, with `out` Off.
bool parse_colour(const std::string& v, Colour& out);
bool parse_pattern(const std::string& v, Pattern& out);
const char* colour_name(Colour c);
const char* pattern_name(Pattern p);

// Drawn from per-view tables over the scaled rows (all but Off and SharpBilinear).
bool pattern_is_table(Pattern p);
// Only ever darkens, the same in every channel: the display engine can draw
// it as a black overlay with per-pixel alpha.
bool pattern_is_mask(Pattern p);

// ---- colour ----------------------------------------------------------------

// The colour of a pixel, each channel 0..1, in double precision. `gba_gamma`
// is zfast_lcd's "fake gamma" (c * (0.6 + 0.4c)), which the frontend folds in
// here whenever that pattern is drawn, so it lands before the pattern's border
// rather than after it.
void colour_of(Colour c, bool gba_gamma, const double in[3], double out[3]);

// Index into a colour table: the top 6 bits of each channel of 0xAARRGGBB,
// which are the 6 the DS drew (the output stage widens them to 8 by repeating
// the top bits), so every pixel it can show has its own entry.
inline u32 colour_index(u32 c) { return ((c >> 6) & 0x3F000u) | ((c >> 4) & 0xFC0u) | ((c >> 2) & 0x3Fu); }
constexpr u32 COLOUR_ENTRIES = 1u << 18;

// One line (SCREEN_W pixels) through a table: Gpu::ScaleTarget::colour.
void colour_line(const u32* lut, const u32* src, u32* dst);

// The table the frontend hands the scaler, double-buffered: stage() starts
// building the buffer not in use on a thread of its own (a megabyte of
// entries is several frames' work on the slower devices), and commit(),
// called between frames when no line is in flight (the core joins at line
// 0), makes it the one lut() gives once it is done. Until then the picture
// keeps the colour it had. finish() waits for it instead.
class ColourTable {
public:
  ColourTable() = default;
  ColourTable(const ColourTable&) = delete;
  ColourTable& operator=(const ColourTable&) = delete;
  ~ColourTable();
  void stage(Colour c, bool gba_gamma);
  bool commit();   // true when the table changed
  void finish();   // stage()'s build, waited for and committed
  // What the last stage() asked for, committed or not.
  bool wants(Colour c, bool gba_gamma) const {
    return pending_ ? staged_colour_ == c && staged_gamma_ == gba_gamma : live_colour_ == c && live_gamma_ == gba_gamma;
  }
  const u32* lut() const { return live_ < 0 ? nullptr : buf_[live_].data(); }   // null: nothing to do
  Colour colour() const { return live_colour_; }
  bool gba_gamma() const { return live_gamma_; }
  // `n` pixels of a whole frame copy (the paths that never reach the scaler).
  void apply(const u32* src, u32* dst, size_t n) const;
private:
  std::vector<u32> buf_[2];
  int live_ = -1, staged_ = -1;
  bool pending_ = false;
  std::thread build_;
  std::atomic<bool> built_{false}, cancel_{false};
  void stop();   // a build still going is abandoned
  Colour live_colour_ = Colour::Off, staged_colour_ = Colour::Off;
  bool live_gamma_ = false, staged_gamma_ = false;
};

// ---- pattern ---------------------------------------------------------------

// What pattern `p` makes of a panel pixel at phase (fx, fy) inside its DS
// pixel (0..1, left to right and top to bottom): out = c * mul + add, per
// channel (r, g, b). `ax`/`ay` are DS pixels per panel pixel (<= 1), the
// shaders' anti-aliasing width; the RG DS patterns fade in over 1x..2x by it.
// `bright`, when asked for, is the multiplier for the brightest pixels; a
// pattern that spreads (pattern_spread) blends between the two by a pixel's
// brightness, and for the others the two are the same.
void pattern_at(Pattern p, double fx, double fy, double ax, double ay, double mul[3], double add[3], double bright[3] = nullptr);
// Whether `p` changes with a pixel's brightness, and over which range of the
// channels' mean it moves from mul to bright (smoothstep, 0..255): `crt`'s
// beam widens as it brightens, and `rgb_stripes` drops its stripes on
// highlights as rgds-core's vivid_lcd does.
bool pattern_spread(Pattern p, u8& lo, u8& hi);

// Owns the tables a Gpu::PatternRows points at.
struct PatternStore {
  std::vector<u8> mul, add, plain, bright, weight;
  std::vector<u16> row_class;
  ds::gpu::Gpu::PatternRows rows;
  u32 classes = 0;
  void clear() { *this = PatternStore{}; }
  const ds::gpu::Gpu::PatternRows* get() const { return rows.w ? &rows : nullptr; }
};
// Tables for a view of rect_w x rect_h panel pixels of which columns
// [x0, x1) are drawn (integer overscale crops the rest). Phase follows the
// runs the nearest scaler draws, so every seam is on a DS pixel's edge.
// False, with `out` cleared, for a view at 1x or smaller or a pattern that
// has no table.
bool build_pattern(Pattern p, u32 rect_w, u32 rect_h, u32 x0, u32 x1, PatternStore& out);

// The spread's weights: smoothstep from lo to hi, 0..255, by the mean of a
// pixel's channels.
void spread_weights(u8 lo, u8 hi, u8 out[256]);
// The mean the kernels index `weight` by: (b + g + r) * 21846 >> 16.
inline u32 channel_mean(u32 b, u32 g, u32 r) { return ((b + g + r) * 21846u) >> 16; }
// n pixels of src through one class's row. With `bright` (else null), each
// pixel's multipliers move from mul to bright by weight[mean] / 255, in
// fixed point both versions share.
void modulate_row(const u32* src, const u8* mul, const u8* bright, const u8* weight, const u8* add, u32 n, u32* dst);
namespace ref { void modulate_row(const u32* src, const u8* mul, const u8* bright, const u8* weight, const u8* add, u32 n, u32* dst); }
// Gpu::ScaleTarget::modulate: row class `cls` of `p` over src.
void modulate_class(const ds::gpu::Gpu::PatternRows& p, u32 cls, const u32* src, u32* dst);

// sharp-bilinear's sample positions for the bilinear path: `sx`/`wx` over the
// drawn columns as Gpu::ScaleTarget::lin_sx/lin_wx, and the rows as
// lin_ystart (SCREEN_H + 1 entries) and lin_wy (rect_h entries).
void build_sharp(u32 rect_w, u32 rect_h, u32 x0, u32 x1, std::vector<u16>& sx, std::vector<u8>& wx,
                 std::vector<u16>& ystart, std::vector<u8>& wy);

// A mask pattern over a panel rectangle of the display engine's overlay
// layer, `stage` being panel_w wide. [px0, px1) x [py0, py1) shows nx x ny
// composite pixels, each a DS pixel, with the DS picture turned by `rot`
// (0/90/180/270) as the composite is. Writes black at the pattern's alpha.
void draw_mask(Pattern p, u32* stage, int panel_w, int px0, int px1, int py0, int py1, int nx, int ny, int rot);

// ---- glow ------------------------------------------------------------------

// GLOW: light spread from the picture onto what is around it, at DS
// resolution after COLOUR and before the scaler, so it works on every display
// path. A Gaussian blur of the light (all of it, or for bloom only what is
// above the threshold), screen-blended back over the picture: c + k g (1 - c).
// Halation tints it warm and spreads it wider, as light scattered in a CRT's
// glass does. The blur needs the lines below each one, so the scaler holds
// back `radius` lines (Gpu::ScaleTarget::filter_*).
struct GlowSpec {
  Glow kind = Glow::Off;    // resolved: never Auto
  double strength = 0.0;    // 0..1
  int radius = 0;           // DS pixels, 1..8
  double threshold = 0.0;   // bloom: 0..1
};
// The kind's own values, for a row left on auto.
GlowSpec glow_defaults(Glow kind, bool for_crt = false);
// The rows as set: GLOW (auto resolves by `crt_drawn`), then GLOW STRENGTH,
// GLOW RADIUS and BLOOM THRESHOLD, "auto" or a number each.
GlowSpec glow_spec(Glow kind, const std::string& strength, const std::string& radius, const std::string& threshold, bool crt_drawn);
bool parse_glow(const std::string& v, Glow& out);
const char* glow_name(Glow g);
constexpr int GLOW_MAX_RADIUS = 8;

// One screen's glow. configure() between frames; push and pull on whichever
// thread scales that screen, in line order; frame() for a whole frame copy.
class GlowFilter {
public:
  void configure(const GlowSpec& s);
  bool on() const { return spec_.kind != Glow::Off && spec_.strength > 0.0 && r_ > 0; }
  const GlowSpec& spec() const { return spec_; }
  u32 delay() const { return static_cast<u32>(r_); }
  void push(u32 line, const u32* src);
  void pull(u32 line, u32* out) const;
  void frame(const u32* src, u32* dst);
  static void push_hook(void* self, u32 line, const u32* src) { static_cast<GlowFilter*>(self)->push(line, src); }
  static void pull_hook(void* self, u32 line, u32* out) { static_cast<GlowFilter*>(self)->pull(line, out); }
private:
  GlowSpec spec_;
  int r_ = 0;
  u32 ring_ = 1;
  std::vector<u16> w_;              // 2r + 1 weights, summing to 256
  u8 pass_[256] = {};               // the light that glows, per channel value
  u32 k_[3] = {};                   // per channel (r, g, b): strength x tint, 0..256
  std::vector<u32> orig_;           // ring_ lines as pushed
  std::vector<u16> blur_;           // ring_ lines x 3 channels x SCREEN_W, blurred across, x256
};

// ---- timing ----------------------------------------------------------------

// MINUI_SHADER_TIMING=1 (launch.sh, from shader-timing.txt): the hooks time
// themselves, and once a frame the frontend calls timing_frame(), which
// logs the average every five seconds.
bool timing_on();
void timing_frame(u32 now_ms, Colour c, Pattern p, Glow g);

} // namespace ds::sdl::shader
