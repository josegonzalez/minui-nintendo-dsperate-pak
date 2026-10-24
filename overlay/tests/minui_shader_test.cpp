// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak, copied into DSperate's tests/ by
// the Makefile and registered by patches/0006-shaders.patch.
//
// The pak's COLOUR and PATTERN shaders: that the tables hold the shaders'
// own maths, that the NEON kernel matches its reference (GCC 8 has
// miscompiled a NEON kernel here before, patch 0002), that the scaler draws
// them where the frontend says and leaves everything else alone, and that
// the rows step the way the menu and the shortcuts expect.
#include "frontend/sdl/minui_shader.h"
#include "frontend/sdl/settings.h"
#include "frontend/video/layout.h"
#include "core/nds.h"
#include "check.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

extern "C" double ds_pow_compat(double x, double y);

using ds::u16;
using ds::u32;
using ds::u8;
using ds::SCREEN_H;
using ds::SCREEN_W;
namespace sh = ds::sdl::shader;
using Gpu = ds::gpu::Gpu;

namespace {

u32 expand5(u32 c5) { const u32 e6 = c5 << 1; return (e6 << 2) | (e6 >> 4); }   // a 2D colour, as the engines widen it
u32 expand6(u32 c6) { return (c6 << 2) | (c6 >> 4); }
u32 pixel(u32 r, u32 g, u32 b) { return 0xFF000000u | r << 16 | g << 8 | b; }
int byte_of(double v) { return static_cast<int>(std::lround(std::min(1.0, std::max(0.0, v)) * 255.0)); }

const ds::sdl::Setting* video_row(const char* key) {
  for (int i = 0; ds::sdl::kVideoSettings[i].key; ++i)
    if (!std::strcmp(ds::sdl::kVideoSettings[i].key, key)) return &ds::sdl::kVideoSettings[i];
  return nullptr;
}

// A test image: every 5-bit colour somewhere, in runs and in noise.
std::vector<u32> test_image(unsigned seed) {
  std::vector<u32> img(SCREEN_W * SCREEN_H);
  std::mt19937 rng(seed);
  for (u32 y = 0; y < SCREEN_H; ++y)
    for (u32 x = 0; x < SCREEN_W; ++x) {
      const u32 i = y * SCREEN_W + x;
      const u32 c = y < 128 ? i & 0x7FFF : rng() & 0x7FFF;
      img[i] = pixel(expand5(c >> 10 & 31), expand5(c >> 5 & 31), expand5(c & 31));
    }
  return img;
}

std::vector<u16> nearest_xrun(u32 w, u32 x0, u32 x1) {
  std::vector<u16> xr(SCREEN_W + 1);
  for (u32 x = 0; x <= SCREEN_W; ++x) xr[x] = static_cast<u16>(std::min(std::max((x * w + SCREEN_W - 1) / SCREEN_W, x0), x1) - x0);
  return xr;
}

// ---- tables ------------------------------------------------------------------

// Every name parses back to its own value, labels fit the menu's value column,
// an unknown name is off, and the rows use these lists.
void test_presets() {
  std::set<std::string> seen;
  for (u8 i = 0; i < sh::kColourCount; ++i) {
    sh::Colour c;
    CHECK(sh::parse_colour(sh::kColours[i].value, c) && static_cast<u8>(c) == i);
    CHECK(std::strcmp(sh::colour_name(c), sh::kColours[i].value) == 0);
    CHECK(std::strlen(sh::kColours[i].label) <= 12);
    CHECK(seen.insert(std::string("c:") + sh::kColours[i].value).second);
  }
  for (u8 i = 0; i < sh::kPatternCount; ++i) {
    sh::Pattern p;
    CHECK(sh::parse_pattern(sh::kPatterns[i].value, p) && static_cast<u8>(p) == i);
    CHECK(std::strcmp(sh::pattern_name(p), sh::kPatterns[i].value) == 0);
    CHECK(std::strlen(sh::kPatterns[i].label) <= 12);
    CHECK(seen.insert(std::string("p:") + sh::kPatterns[i].value).second);
  }
  sh::Colour c = sh::Colour::Gba;
  CHECK(!sh::parse_colour("sepia", c) && c == sh::Colour::Off);
  sh::Pattern p = sh::Pattern::Lcd;
  CHECK(!sh::parse_pattern("crt", p) && p == sh::Pattern::Off);
  // After BILINEAR, COLOUR then PATTERN, over the lists above, off by default.
  const ds::sdl::Setting* t = ds::sdl::kVideoSettings;
  int lin = -1;
  for (int i = 0; t[i].key; ++i) if (!std::strcmp(t[i].key, "video.linear")) lin = i;
  CHECK(lin >= 0);
  CHECK(!std::strcmp(t[lin + 1].key, "video.colour") && t[lin + 1].choices == sh::kColours && t[lin + 1].nchoices == sh::kColourCount);
  CHECK(!std::strcmp(t[lin + 2].key, "video.pattern") && t[lin + 2].choices == sh::kPatterns && t[lin + 2].nchoices == sh::kPatternCount);
  CHECK(!std::strcmp(t[lin + 1].def, "off") && !std::strcmp(t[lin + 2].def, "off"));
  CHECK(t[lin + 2].depends == ds::sdl::Dep::Pattern);
}

// Some presets written out again here from their GLSL, independently of
// minui_shader.cpp, at a few colours.
void test_colour_against_glsl() {
  auto p = [](double x, double e) { return x <= 0.0 ? 0.0 : ds_pow_compat(x, e); };
  auto cl = [](double v) { return std::min(1.0, std::max(0.0, v)); };
  const double samples[][3] = {{0, 0, 0}, {1, 1, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5, 0.25, 0.75}, {0.2, 0.6, 0.4}};
  for (const auto& s : samples) {
    double out[3];
    // warm_paper: mix(vec3(0.055,0.04,0.025), c, 0.94) * vec3(1.0,0.95,0.86)
    sh::colour_of(sh::Colour::WarmPaper, false, s, out);
    const double wp[3] = {(0.055 * 0.06 + s[0] * 0.94) * 1.0, (0.04 * 0.06 + s[1] * 0.94) * 0.95, (0.025 * 0.06 + s[2] * 0.94) * 0.86};
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(out[k] - wp[k]) < 1e-9);
    // pocket_green: mix(ink, paper, dot(c, vec3(0.299,0.587,0.114)))
    sh::colour_of(sh::Colour::PocketGreen, false, s, out);
    const double l = 0.299 * s[0] + 0.587 * s[1] + 0.114 * s[2];
    const double pg[3] = {0.10 + (0.77 - 0.10) * l, 0.19 + (0.84 - 0.19) * l, 0.15 + (0.57 - 0.15) * l};
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(out[k] - pg[k]) < 1e-9);
    // gba_color, its mat3 read column by column.
    sh::colour_of(sh::Colour::Gba, false, s, out);
    double c[3];
    for (int k = 0; k < 3; ++k) c[k] = cl(p(s[k], 2.7) * 0.94);
    const double g[3] = {cl(p(0.82 * c[0] + 0.24 * c[1] - 0.06 * c[2], 1 / 2.2)),
                         cl(p(0.125 * c[0] + 0.665 * c[1] + 0.21 * c[2], 1 / 2.2)),
                         cl(p(0.195 * c[0] + 0.075 * c[1] + 0.73 * c[2], 1 / 2.2))};
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(out[k] - g[k]) < 1e-9);
    // nds-color: color * screen, its mat4 columns (r, rg, rb), (gr, g, gb), (br, bg, b).
    sh::colour_of(sh::Colour::Nds, false, s, out);
    for (int k = 0; k < 3; ++k) c[k] = cl(p(s[k], 1.91) * 0.89);
    const double n[3] = {cl(p(0.87 * c[0] + 0.255 * c[1] - 0.125 * c[2], 1 / 1.91)),
                         cl(p(0.10 * c[0] + 0.645 * c[1] + 0.255 * c[2], 1 / 1.91)),
                         cl(p(0.10 * c[0] + 0.17 * c[1] + 0.73 * c[2], 1 / 1.91))};
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(out[k] - n[k]) < 1e-9);
    // zfast_lcd's gamma on top of anything.
    sh::colour_of(sh::Colour::Off, true, s, out);
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(out[k] - s[k] * (0.6 + 0.4 * s[k])) < 1e-9);
  }
  // Natural Vision leaves grey grey, and white white.
  const double grey[3] = {0.5, 0.5, 0.5}, white[3] = {1, 1, 1};
  double out[3];
  sh::colour_of(sh::Colour::NaturalVision, false, grey, out);
  CHECK(std::fabs(out[0] - out[1]) < 1e-6 && std::fabs(out[1] - out[2]) < 1e-6);
  sh::colour_of(sh::Colour::NaturalVision, false, white, out);
  for (double v : out) CHECK(std::fabs(v - 1.0) < 1e-6);
}

// The table holds every colour the DS can show (6 bits a channel, widened to
// 8 as the output stage does) exactly as the double-precision maths gives it.
void test_colour_table() {
  for (u8 i = 0; i < sh::kColourCount; ++i)
    for (bool gamma : {false, true}) {
      const sh::Colour c = static_cast<sh::Colour>(i);
      sh::ColourTable t;
      t.stage(c, gamma);
      t.finish();
      if (c == sh::Colour::Off && !gamma) { CHECK(t.lut() == nullptr); continue; }
      CHECK(t.lut() != nullptr && t.colour() == c && t.gba_gamma() == gamma);
      int bad = 0;
      for (u32 k = 0; k < sh::COLOUR_ENTRIES; ++k) {
        const u32 r = expand6(k >> 12 & 63), g = expand6(k >> 6 & 63), b = expand6(k & 63);
        const double in[3] = {r / 255.0, g / 255.0, b / 255.0};
        double out[3];
        sh::colour_of(c, gamma, in, out);
        bad += t.lut()[sh::colour_index(pixel(r, g, b))] != pixel(byte_of(out[0]), byte_of(out[1]), byte_of(out[2]));
      }
      CHECK(bad == 0);
    }
  // Every 6-bit colour has its own entry.
  std::set<u32> seen;
  for (u32 k = 0; k < sh::COLOUR_ENTRIES; ++k) seen.insert(sh::colour_index(pixel(expand6(k >> 12 & 63), expand6(k >> 6 & 63), expand6(k & 63))));
  CHECK(seen.size() == sh::COLOUR_ENTRIES && *seen.rbegin() == sh::COLOUR_ENTRIES - 1);
}

// stage() builds aside; nothing the scaler holds changes until commit().
void test_colour_double_buffer() {
  sh::ColourTable t;
  CHECK(!t.commit());
  t.stage(sh::Colour::Gba, false);
  CHECK(t.lut() == nullptr);
  t.finish();
  const u32* first = t.lut();
  CHECK(first && t.colour() == sh::Colour::Gba);
  CHECK(!t.commit());   // nothing new
  const u32 white = first[sh::COLOUR_ENTRIES - 1];
  t.stage(sh::Colour::Amber, false);
  // Built aside: until it is committed the live table is untouched.
  CHECK(t.lut() == first && first[sh::COLOUR_ENTRIES - 1] == white && t.colour() == sh::Colour::Gba);
  while (!t.commit()) std::this_thread::yield();
  CHECK(t.lut() != first && t.colour() == sh::Colour::Amber);
  // Two changes before a commit: the later one is what lands.
  t.stage(sh::Colour::Nds, false);
  t.stage(sh::Colour::Vivid, false);
  t.finish();
  CHECK(t.colour() == sh::Colour::Vivid);
  t.stage(sh::Colour::Off, false);
  CHECK(t.lut() != nullptr);
  CHECK(t.commit());
  CHECK(t.lut() == nullptr);
  // apply() with nothing staged copies.
  const std::vector<u32> img = test_image(1);
  std::vector<u32> out(img.size());
  t.apply(img.data(), out.data(), img.size());
  CHECK(out == img);
}

// ---- patterns ----------------------------------------------------------------

// The NEON kernel against the reference: every width up to past a panel row,
// a destination off the 16-byte grid, and both ends of the multipliers.
void test_modulate_neon() {
  std::mt19937 rng(7);
  std::vector<u32> src(1400), a(1404), b(1404);
  std::vector<u8> mul(1400 * 4), add(1400 * 4);
  for (int round = 0; round < 3; ++round) {
    for (u32& v : src) v = rng();
    for (size_t i = 0; i < mul.size(); ++i) {
      mul[i] = round == 0 ? static_cast<u8>(rng()) : round == 1 ? (i & 1 ? 255 : 0) : 128;
      add[i] = round == 0 ? static_cast<u8>(rng() & 63) : round == 1 ? (i & 2 ? 255 : 0) : 0;
    }
    for (u32 n = 1; n <= 1300; n += n < 40 ? 1 : 37) {
      std::fill(a.begin(), a.end(), 0x5A5A5A5Au);
      std::fill(b.begin(), b.end(), 0x5A5A5A5Au);
      sh::ref::modulate_row(src.data(), mul.data(), add.data(), n, a.data() + 1);
      sh::modulate_row(src.data(), mul.data(), add.data(), n, b.data() + 1);
      CHECK(a == b);
      CHECK(b[0] == 0x5A5A5A5Au && b[n + 1] == 0x5A5A5A5Au);
    }
  }
  // 1.0 and nothing added is the identity.
  for (u32& v : src) v = rng();
  std::fill(mul.begin(), mul.end(), 128);
  std::fill(add.begin(), add.end(), 0);
  sh::modulate_row(src.data(), mul.data(), add.data(), 1300, b.data());
  CHECK(std::equal(src.begin(), src.begin() + 1300, b.begin()));
}

struct Size { u32 w, h; };
const Size kPanels[] = {{640, 480}, {720, 720}, {1024, 768}, {1280, 720}};

// Each view the layouts place gets rows that hold the pattern at the phase of
// the run each panel pixel is drawn in, with no more classes than a DS line's
// rows can make, every row in one, and nothing at all at 1x or below.
void test_pattern_tables() {
  for (const Size& panel : kPanels)
    for (ds::frontend::Mode mode : {ds::frontend::Mode::Single, ds::frontend::Mode::Vertical, ds::frontend::Mode::Pip}) {
      ds::frontend::Layout l;
      l.mode = mode;
      ds::frontend::View v[2];
      ds::frontend::place(l, static_cast<int>(panel.w), static_cast<int>(panel.h), v);
      for (const ds::frontend::View& view : v) {
        if (!view.shown) continue;
        const u32 w = static_cast<u32>(view.rect.w), h = static_cast<u32>(view.rect.h);
        for (u8 pi = 0; pi < sh::kPatternCount; ++pi) {
          const sh::Pattern p = static_cast<sh::Pattern>(pi);
          sh::PatternStore st;
          const bool built = sh::build_pattern(p, w, h, 0, w, st);
          if (!sh::pattern_is_table(p) || w <= SCREEN_W || h <= SCREEN_H) { CHECK(!built && !st.get()); continue; }
          CHECK(built && st.get() && st.rows.w == w);
          CHECK(st.classes <= 2 * static_cast<u32>(std::ceil(h / 192.0)) + 1);
          const double ax = 256.0 / w, ay = 192.0 / h;
          for (u32 y = 0; y < h; ++y) {
            const u32 c = st.row_class[y];
            CHECK(c < st.classes);
            // The row's phase, from the line that draws it (emit_scaled's spans).
            const u32 L = y * SCREEN_H / h;
            const u32 y0 = (L * h + SCREEN_H - 1) / SCREEN_H, y1 = ((L + 1) * h + SCREEN_H - 1) / SCREEN_H;
            const double fy = (y - y0 + 0.5) / (y1 - y0);
            for (u32 x = 0; x < w; x += 7) {
              const u32 s = x * SCREEN_W / w, a = (s * w + 255) / 256, b = ((s + 1) * w + 255) / 256;
              double m[3], ad[3];
              sh::pattern_at(p, (x - a + 0.5) / (b - a), fy, ax, ay, m, ad);
              const u8* mb = &st.mul[(static_cast<size_t>(c) * w + x) * 4];
              const u8* ab = &st.add[(static_cast<size_t>(c) * w + x) * 4];
              for (int ch = 0; ch < 3; ++ch) {
                CHECK(std::fabs(mb[2 - ch] / 128.0 - std::min(m[ch], 255.0 / 128)) <= 0.5 / 128 + 1e-9);
                CHECK(std::abs(ab[2 - ch] - byte_of(ad[ch])) == 0);
              }
              CHECK(mb[3] == 128 && ab[3] == 0);
            }
          }
          // Plain classes are exactly the identity.
          for (u32 c = 0; c < st.classes; ++c) {
            bool ident = true;
            for (u32 i = 0; i < w * 4; ++i) ident = ident && st.mul[c * w * 4 + i] == 128 && st.add[c * w * 4 + i] == 0;
            CHECK(static_cast<bool>(st.plain[c]) == ident);
          }
        }
      }
    }
}

// The patterns the display engine draws as a black overlay only ever darken,
// the same in every channel, with nothing added; the rest do not, so they
// are refused there.
void test_mask_patterns() {
  for (u8 pi = 0; pi < sh::kPatternCount; ++pi) {
    const sh::Pattern p = static_cast<sh::Pattern>(pi);
    bool darken_only = sh::pattern_is_table(p);
    for (int j = 0; j < 32 && darken_only; ++j)
      for (int i = 0; i < 32 && darken_only; ++i)
        for (double scale : {1.25, 2.5, 4.0}) {
          double m[3], a[3];
          sh::pattern_at(p, (i + 0.5) / 32, (j + 0.5) / 32, 1 / scale, 1 / scale, m, a);
          darken_only = darken_only && m[0] <= 1.0 + 1e-9 && m[0] == m[1] && m[1] == m[2] && a[0] == 0 && a[1] == 0 && a[2] == 0;
        }
    CHECK(darken_only == sh::pattern_is_mask(p));
  }
  // On the overlay layer, black at 1 - mul of the same phase the scaler uses.
  const int pw = 640, ph = 480;
  std::vector<u32> stage(static_cast<size_t>(pw) * ph, 0x12345678u);
  for (sh::Pattern p : {sh::Pattern::Lcd, sh::Pattern::ZfastLcd, sh::Pattern::Scanlines, sh::Pattern::SoftGrid}) {
    sh::draw_mask(p, stage.data(), pw, 0, pw, 0, ph, 256, 192, 0);
    sh::PatternStore st;
    CHECK(sh::build_pattern(p, pw, ph, 0, pw, st));
    for (int y = 0; y < ph; y += 3)
      for (int x = 0; x < pw; x += 5) {
        const u32 px = stage[static_cast<size_t>(y) * pw + x];
        CHECK((px & 0x00FFFFFFu) == 0);
        const double mul = st.mul[(static_cast<size_t>(st.row_class[y]) * pw + x) * 4 + 1] / 128.0;
        CHECK(std::fabs((255 - static_cast<int>(px >> 24)) / 255.0 - mul) <= 1.0 / 255 + 0.5 / 128 + 1e-9);
      }
  }
  // Anything else draws nothing.
  std::fill(stage.begin(), stage.end(), 0u);
  sh::draw_mask(sh::Pattern::Beads, stage.data(), pw, 0, pw, 0, ph, 256, 192, 0);
  for (u32 px : stage) CHECK(px == 0);
}

// sharp-bilinear: nearest at whole scales, the shader's sample points between.
void test_sharp() {
  for (const Size s : {Size{512, 384}, Size{1024, 768}}) {
    std::vector<u16> sx, ystart;
    std::vector<u8> wx, wy;
    sh::build_sharp(s.w, s.h, 0, s.w, sx, wx, ystart, wy);
    CHECK(sx.size() == s.w && wy.size() == s.h && ystart.size() == SCREEN_H + 1);
    // The last DS column is only reached at weight 255 (upstream's edge rule, display.cpp).
    for (u32 x = 0; x < s.w; ++x) {
      const u32 want = x * SCREEN_W / s.w;
      if (want == SCREEN_W - 1) CHECK(sx[x] == SCREEN_W - 2 && wx[x] == 255);
      else CHECK(sx[x] == want && wx[x] == 0);
    }
    for (u8 w : wy) CHECK(w == 0);
  }
  const u32 w = 640, h = 480;
  std::vector<u16> sx, ystart;
  std::vector<u8> wx, wy;
  sh::build_sharp(w, h, 0, w, sx, wx, ystart, wy);
  CHECK(ystart[0] == 0);
  for (u32 l = 0; l < SCREEN_H; ++l) CHECK(ystart[l] <= ystart[l + 1] && ystart[l + 1] <= h);
  for (u32 x = 0; x < w; ++x) {
    // texel, prescale 2: f = (cd - clamp(cd, -0.25, 0.25)) * 2 + 0.5, sampled between centres.
    const double t = (x + 0.5) * SCREEN_W / w, fl = std::floor(t), cd = t - fl - 0.5;
    const double u = fl + (cd - std::min(0.25, std::max(-0.25, cd))) * 2 + 0.5 - 0.5;
    if (u <= 0) { CHECK(sx[x] == 0 && wx[x] == 0); continue; }
    CHECK(sx[x] == static_cast<u16>(std::min(u, 254.0)));
    if (u < 255) CHECK(std::abs(static_cast<int>(wx[x]) - static_cast<int>((u - std::floor(u)) * 256)) <= 1);
  }
}

// ---- through the scaler --------------------------------------------------------

// The scaler with the hooks: nothing set is upstream's nearest to the byte;
// colour then pattern is the table looked up and the class row applied,
// rows cropped by an overscale included.
void test_scaler() {
  const std::vector<u32> img = test_image(3);
  const u32 W = 640, H = 480;
  for (bool crop : {false, true}) {
    const u32 y_lo = crop ? 60 : 0, y_hi = crop ? 420 : H;
    const std::vector<u16> xr = nearest_xrun(W, 0, W);
    sh::ColourTable colour;
    colour.stage(sh::Colour::DsClassic, false);
    colour.finish();
    sh::PatternStore pat;
    CHECK(sh::build_pattern(sh::Pattern::DsClassic, W, H, 0, W, pat));
    for (int hooks = 0; hooks < 2; ++hooks) {
      ds::NDS n;
      std::vector<u32> out(W * (y_hi - y_lo), 0xDEADBEEF), other(W * H);
      Gpu::ScaleTarget t{out.data(), W, H, xr.data()};
      t.y_lo = y_lo; t.y_hi = y_hi;
      if (hooks) { t.colour_lut = colour.lut(); t.colour = sh::colour_line; t.pattern = pat.get(); t.modulate = sh::modulate_row; }
      n.gpu.set_scale_target(0, t);
      n.gpu.set_scale_target(1, Gpu::ScaleTarget{other.data(), W, H, xr.data()});
      n.gpu.scale_image(0, img.data());
      int bad = 0;
      for (u32 y = y_lo; y < y_hi; ++y) {
        const u32 L = y * SCREEN_H / H;
        for (u32 x = 0; x < W; ++x) {
          const u32 s = x * SCREEN_W / W;
          u32 want = img[L * SCREEN_W + s];
          if (hooks) {
            want = colour.lut()[sh::colour_index(want)];
            const size_t at = (static_cast<size_t>(pat.row_class[y]) * W + x) * 4;
            sh::ref::modulate_row(&want, &pat.mul[at], &pat.add[at], 1, &want);
          }
          if (out[(y - y_lo) * W + x] != want && ++bad < 4)
            std::fprintf(stderr, "hooks %d crop %d (%u,%u) = %08x, want %08x\n", hooks, crop, x, y, out[(y - y_lo) * W + x], want);
        }
      }
      CHECK(bad == 0);
    }
  }
}

// Colour comes before everything the scaler does: through bilinear and
// chunky, a coloured line is the line coloured first.
void test_colour_comes_first() {
  const std::vector<u32> img = test_image(5);
  sh::ColourTable colour;
  colour.stage(sh::Colour::Vivid, false);
  colour.finish();
  std::vector<u32> pre(img.size());
  colour.apply(img.data(), pre.data(), img.size());
  const u32 W = 640, H = 480;
  const std::vector<u16> xr = nearest_xrun(W, 0, W);
  std::vector<u16> pairs = xr;   // chunky's: the even pixel's run widened over the odd one's (display.cpp)
  for (u32 x = 1; x < SCREEN_W; x += 2) pairs[x] = pairs[x + 1];
  std::vector<u16> lsx(W);
  std::vector<u8> lwx(W);
  for (u32 x = 0; x < W; ++x) { lsx[x] = static_cast<u16>(std::min<u32>(254, x * 255 / W)); lwx[x] = static_cast<u8>(x * 37); }
  for (int mode = 0; mode < 2; ++mode) {
    std::vector<u32> a(W * H), b(W * H), other(W * H);
    for (int pass = 0; pass < 2; ++pass) {
      ds::NDS n;
      Gpu::ScaleTarget t{pass ? b.data() : a.data(), W, H, mode ? pairs.data() : xr.data()};
      if (mode == 0) { t.bilinear = true; t.lin_sx = lsx.data(); t.lin_wx = lwx.data(); }
      else t.chunky = 2;
      if (pass) { t.colour_lut = colour.lut(); t.colour = sh::colour_line; }
      n.gpu.set_scale_target(0, t);
      n.gpu.set_scale_target(1, Gpu::ScaleTarget{other.data(), W, H, xr.data()});
      n.gpu.scale_image(0, pass ? img.data() : pre.data());
    }
    CHECK(a == b);
  }
}

// Sharp bilinear at 2x through the bilinear path is nearest, row for row.
void test_sharp_scaler() {
  const std::vector<u32> img = test_image(9);
  const u32 W = 512, H = 384;
  std::vector<u16> sx, ystart;
  std::vector<u8> wx, wy;
  sh::build_sharp(W, H, 0, W, sx, wx, ystart, wy);
  const std::vector<u16> xr = nearest_xrun(W, 0, W);
  ds::NDS n;
  std::vector<u32> out(W * H, 0xDEADBEEF), other(W * H);
  Gpu::ScaleTarget t{out.data(), W, H, xr.data()};
  t.bilinear = true; t.lin_sx = sx.data(); t.lin_wx = wx.data(); t.lin_ystart = ystart.data(); t.lin_wy = wy.data();
  n.gpu.set_scale_target(0, t);
  n.gpu.set_scale_target(1, Gpu::ScaleTarget{other.data(), W, H, xr.data()});
  n.gpu.scale_image(0, img.data());
  int bad = 0;
  for (u32 y = 0; y < H; ++y)
    for (u32 x = 0; x < W; ++x) {
      const u32 got = out[y * W + x], want = img[(y / 2) * SCREEN_W + x / 2];
      if (x / 2 < SCREEN_W - 1) { bad += got != want; continue; }
      for (u32 sh = 0; sh < 32; sh += 8) bad += std::abs(static_cast<int>(got >> sh & 255) - static_cast<int>(want >> sh & 255)) > 1;
    }
  CHECK(bad == 0);
}

// ---- stepping ------------------------------------------------------------------

// Enough of a host for step_value: the display engine's rule for PATTERN.
struct StepHost final : ds::sdl::SettingsHost {
  bool disp = false;
  std::string get(const char*) const override { return ""; }
  void set(const char*, const std::string&) override {}
  bool enabled(const ds::sdl::Setting&) const override { return true; }
  const char* disabled_reason(const ds::sdl::Setting&) const override { return ""; }
  bool value_allowed(const ds::sdl::Setting& s, const char* v) const override {
    if (!disp || std::strcmp(s.key, "video.pattern") != 0) return true;
    sh::Pattern p;
    return sh::parse_pattern(v, p) && (p == sh::Pattern::Off || sh::pattern_is_mask(p));
  }
  void commit() override {}
  bool save_per_game() const override { return false; }
  void set_save_per_game(bool) override {}
  bool has_game() const override { return false; }
  int binding_count(bool) const override { return 0; }
  Binding binding(bool, int) const override { return {}; }
  bool has_pad() const override { return false; }
  void begin_capture(bool) override {}
  void cancel_capture() override {}
  bool capturing() const override { return false; }
  std::string take_capture() override { return ""; }
  void bind(const std::string&, const std::string&) override {}
  void reset_bindings(bool) override {}
  std::vector<std::string> collisions() const override { return {}; }
  const char* user_settings_note() const override { return nullptr; }
};

// Forward goes round every value and back to off; back goes the other way.
// On the display engine only the masks come round.
void test_stepping() {
  StepHost h;
  for (const char* key : {"video.colour", "video.pattern"}) {
    const ds::sdl::Setting* row = video_row(key);
    CHECK(row != nullptr);
    std::string v = "off";
    for (int i = 1; i < row->nchoices; ++i) { v = ds::sdl::step_value(*row, v, 1, h); CHECK(v == row->choices[i].value); }
    CHECK(ds::sdl::step_value(*row, v, 1, h) == "off");
    CHECK(ds::sdl::step_value(*row, "off", -1, h) == row->choices[row->nchoices - 1].value);
  }
  h.disp = true;
  const ds::sdl::Setting* row = video_row("video.pattern");
  std::string v = "off";
  std::set<std::string> seen;
  for (int i = 0; i < 2 * row->nchoices; ++i) {
    v = ds::sdl::step_value(*row, v, 1, h);
    sh::Pattern p;
    CHECK(sh::parse_pattern(v, p) && (p == sh::Pattern::Off || sh::pattern_is_mask(p)));
    seen.insert(v);
  }
  CHECK(seen.size() == 5);
}

// ---- timing --------------------------------------------------------------------

// What each costs per frame here. Not a device: for comparing presets only.
void report_timing() {
  using clk = std::chrono::steady_clock;
  const std::vector<u32> img = test_image(11);
  std::vector<u32> line(SCREEN_W);
  for (u8 i = 1; i < sh::kColourCount; ++i) {
    sh::ColourTable t;
    const auto b0 = clk::now();
    t.stage(static_cast<sh::Colour>(i), false);
    t.finish();
    const auto b1 = clk::now();
    for (int rep = 0; rep < 10; ++rep)
      for (u32 y = 0; y < 2 * SCREEN_H; ++y) sh::colour_line(t.lut(), &img[(y % SCREEN_H) * SCREEN_W], line.data());
    const auto b2 = clk::now();
    std::printf("minui_shader: colour %-18s build %6.2f ms, %6.1f us/frame\n", sh::colour_name(static_cast<sh::Colour>(i)),
                std::chrono::duration<double, std::milli>(b1 - b0).count(), std::chrono::duration<double, std::micro>(b2 - b1).count() / 10);
  }
  for (const Size& s : kPanels) {
    std::vector<u32> row(s.w), out(s.w);
    for (u8 i = 2; i < sh::kPatternCount; ++i) {
      sh::PatternStore st;
      sh::build_pattern(static_cast<sh::Pattern>(i), s.w, s.h, 0, s.w, st);
      const auto b0 = clk::now();
      for (int rep = 0; rep < 10; ++rep)
        for (u32 y = 0; y < s.h; ++y) {
          const u32 c = st.row_class[y];
          if (!st.plain[c]) sh::modulate_row(row.data(), &st.mul[c * s.w * 4], &st.add[c * s.w * 4], s.w, out.data());
        }
      const auto b1 = clk::now();
      std::printf("minui_shader: pattern %-14s %4ux%-4u %u classes, %6.1f us/frame (one screen, every row)\n",
                  sh::pattern_name(static_cast<sh::Pattern>(i)), s.w, s.h, st.classes,
                  std::chrono::duration<double, std::micro>(b1 - b0).count() / 10);
    }
  }
}

} // namespace

int main() {
  test_presets();
  test_colour_against_glsl();
  test_colour_table();
  test_colour_double_buffer();
  test_modulate_neon();
  test_pattern_tables();
  test_mask_patterns();
  test_sharp();
  test_scaler();
  test_colour_comes_first();
  test_sharp_scaler();
  test_stepping();
  report_timing();
  std::puts("minui_shader: ok");
  return 0;
}
