// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak. See minui_shader.h, which also
// credits the shaders each preset comes from.
#include "frontend/sdl/minui_shader.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>

#if DSPERATE_NEON
#include <arm_neon.h>
#endif

// The core's pow, pinned to the glibc symbol the ABI gate allows (pow_compat.c).
extern "C" double ds_pow_compat(double x, double y);

namespace ds::sdl::shader {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v; }
double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
double mix(double a, double b, double t) { return a + (b - a) * t; }
double smoothstep(double e0, double e1, double x) {
  const double t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3.0 - 2.0 * t);
}
double powp(double x, double e) { return x <= 0.0 ? 0.0 : ds_pow_compat(x, e); }

// Row-major 3x3, the GLSL mat3(...) constants already transposed.
void mat3(const double m[9], double c[3]) {
  const double r = m[0] * c[0] + m[1] * c[1] + m[2] * c[2];
  const double g = m[3] * c[0] + m[4] * c[1] + m[5] * c[2];
  const double b = m[6] * c[0] + m[7] * c[1] + m[8] * c[2];
  c[0] = r; c[1] = g; c[2] = b;
}

// nds-color (hunterk, Pokefan531): gamma 1.91 in and out, lum 0.89.
void nds(double c[3]) {
  static const double m[9] = {0.87, 0.255, -0.125,
                              0.10, 0.645, 0.255,
                              0.10, 0.17, 0.73};
  for (int k = 0; k < 3; ++k) c[k] = clamp01(powp(c[k], 1.91) * 0.89);
  mat3(m, c);
  for (int k = 0; k < 3; ++k) c[k] = clamp01(powp(c[k], 1.0 / 1.91));
}

// Natural Vision (ShadX): gamma 2.2 in, YIQ with Y raised to 1.1 and I and Q
// scaled by 1.1, clamped to YIQ's range, gamma 2.2 out. The NTSC matrices.
void natural_vision(double c[3]) {
  static const double to_yiq[9] = {0.299, 0.587, 0.114,
                                   0.595716, -0.274453, -0.321263,
                                   0.211456, -0.522591, 0.311135};
  static const double to_rgb[9] = {1.0, 0.95629572, 0.62102442,
                                   1.0, -0.27212210, -0.64738060,
                                   1.0, -1.10698902, 1.70461500};
  for (int k = 0; k < 3; ++k) c[k] = powp(c[k], 2.2);
  mat3(to_yiq, c);
  c[0] = clampd(powp(c[0], 1.1), 0.0, 1.0);
  c[1] = clampd(c[1] * 1.1, -0.595716, 0.595716);
  c[2] = clampd(c[2] * 1.1, -0.522591, 0.522591);
  mat3(to_rgb, c);
  for (int k = 0; k < 3; ++k) c[k] = clamp01(powp(c[k], 1.0 / 2.2));
}

// The RG DS panel tones: gamma 2.2 in, a channel-bleed matrix, gamma out,
// then black and white lifted to the panel's.
void panel(double c[3], const double m[9], double gout, const double lo[3], const double hi[3]) {
  for (int k = 0; k < 3; ++k) c[k] = powp(c[k], 2.2);
  mat3(m, c);
  for (int k = 0; k < 3; ++k) c[k] = clamp01(mix(lo[k], hi[k], powp(c[k], 1.0 / gout)));
}

// vivid()/the beads' and tiles' tone: saturation that favours dull colours,
// then part of the way to a smoothstep contrast curve.
void vivid(double c[3], double k0, double k1, double curve) {
  const double l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  const double chroma = std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
  const double k = k0 + k1 * (1.0 - chroma);
  for (int i = 0; i < 3; ++i) {
    const double v = clamp01(mix(l, c[i], k));
    c[i] = mix(v, v * v * (3.0 - 2.0 * v), curve);
  }
}

// Pokefan531's GBA model, as rgds-core's gba_color has it.
void gba(double c[3]) {
  static const double m[9] = {0.82, 0.24, -0.06,
                              0.125, 0.665, 0.21,
                              0.195, 0.075, 0.73};
  for (int k = 0; k < 3; ++k) c[k] = clamp01(powp(c[k], 2.7) * 0.94);
  mat3(m, c);
  for (int k = 0; k < 3; ++k) c[k] = clamp01(powp(c[k], 1.0 / 2.2));
}

void mono(double c[3], const double ink[3], const double paper[3]) {
  const double l = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
  for (int k = 0; k < 3; ++k) c[k] = mix(ink[k], paper[k], l);
}

// The 8-bit value the output stage gives a 6-bit DS colour (expand_colours).
u32 expand6(u32 c6) { return (c6 << 2) | (c6 >> 4); }

u8 to_byte(double v) { return static_cast<u8>(std::lround(clamp01(v) * 255.0)); }

// ---- timing ----

std::atomic<u64> g_colour_ns{0}, g_pattern_ns{0};
u64 now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<u64>(ts.tv_sec) * 1000000000ull + static_cast<u64>(ts.tv_nsec);
}

} // namespace

bool parse_colour(const std::string& v, Colour& out) {
  for (u8 i = 0; i < kColourCount; ++i) if (v == kColours[i].value) { out = static_cast<Colour>(i); return true; }
  out = Colour::Off;
  return false;
}
bool parse_pattern(const std::string& v, Pattern& out) {
  for (u8 i = 0; i < kPatternCount; ++i) if (v == kPatterns[i].value) { out = static_cast<Pattern>(i); return true; }
  out = Pattern::Off;
  return false;
}
const char* colour_name(Colour c) { return c < Colour::Count ? kColours[static_cast<u8>(c)].value : "off"; }
const char* pattern_name(Pattern p) { return p < Pattern::Count ? kPatterns[static_cast<u8>(p)].value : "off"; }

bool pattern_is_table(Pattern p) { return p != Pattern::Off && p != Pattern::SharpBilinear && p < Pattern::Count; }
bool pattern_is_mask(Pattern p) {
  return p == Pattern::Lcd || p == Pattern::ZfastLcd || p == Pattern::Scanlines || p == Pattern::SoftGrid;
}

// ---- colour ----------------------------------------------------------------

void colour_of(Colour k, bool gba_gamma, const double in[3], double out[3]) {
  double c[3] = {in[0], in[1], in[2]};
  switch (k) {
  case Colour::Off: case Colour::Count: break;
  case Colour::Nds: nds(c); break;
  case Colour::NaturalVision: natural_vision(c); break;
  case Colour::NdsNaturalVision: {
    // Two passes in rgds-core, through an 8-bit framebuffer between them.
    nds(c);
    for (double& v : c) v = to_byte(v) / 255.0;
    natural_vision(c);
    break;
  }
  case Colour::DsClassic: {
    static const double m[9] = {0.86, 0.11, 0.03, 0.07, 0.84, 0.09, 0.04, 0.10, 0.86};
    static const double lo[3] = {0.045, 0.05, 0.06}, hi[3] = {0.92, 0.94, 0.96};
    panel(c, m, 2.45, lo, hi);
    break;
  }
  case Colour::DsLite: {
    static const double m[9] = {0.90, 0.08, 0.02, 0.05, 0.89, 0.06, 0.03, 0.07, 0.90};
    static const double lo[3] = {0.025, 0.03, 0.04}, hi[3] = {0.97, 0.98, 1.0};
    panel(c, m, 2.3, lo, hi);
    break;
  }
  case Colour::Natural: {
    static const double m[9] = {0.88, 0.09, 0.03, 0.07, 0.86, 0.07, 0.04, 0.08, 0.88};
    static const double lo[3] = {0.03, 0.03, 0.04}, hi[3] = {0.97, 0.97, 0.98};
    panel(c, m, 2.3, lo, hi);
    break;
  }
  case Colour::Vivid: vivid(c, 1.15, 0.30, 0.45); break;
  case Colour::Bold: vivid(c, 1.2, 0.35, 0.35); break;
  case Colour::Gba: gba(c); break;
  case Colour::WarmPaper: {
    static const double k0[3] = {0.055, 0.04, 0.025}, t[3] = {1.0, 0.95, 0.86};
    for (int i = 0; i < 3; ++i) c[i] = mix(k0[i], c[i], 0.94) * t[i];
    break;
  }
  case Colour::CoolCrisp: {
    static const double t[3] = {0.93, 0.98, 1.0};
    for (int i = 0; i < 3; ++i) c[i] = clamp01((c[i] - 0.5) * 1.06 + 0.5) * t[i];
    break;
  }
  case Colour::PocketGreen: {
    static const double ink[3] = {0.10, 0.19, 0.15}, paper[3] = {0.77, 0.84, 0.57};
    mono(c, ink, paper);
    break;
  }
  case Colour::Amber: {
    static const double ink[3] = {0.12, 0.055, 0.018}, paper[3] = {1.0, 0.79, 0.43};
    mono(c, ink, paper);
    break;
  }
  }
  for (int i = 0; i < 3; ++i) {
    c[i] = clamp01(c[i]);
    if (gba_gamma) c[i] *= 0.6 + 0.4 * c[i];
    out[i] = c[i];
  }
}

void colour_line(const u32* lut, const u32* src, u32* dst) {
  const u64 t0 = timing_on() ? now_ns() : 0;
  u32 was = ~src[0], is = 0;
  for (u32 i = 0; i < SCREEN_W; ++i) {
    const u32 c = src[i];
    if (c != was) { was = c; is = lut[colour_index(c)]; }
    dst[i] = is;
  }
  if (t0) g_colour_ns.fetch_add(now_ns() - t0, std::memory_order_relaxed);
}

ColourTable::~ColourTable() { stop(); }

void ColourTable::stop() {
  if (!build_.joinable()) return;
  cancel_ = true;
  build_.join();
  cancel_ = false;
}

void ColourTable::stage(Colour c, bool gba_gamma) {
  stop();   // a change before the last was done: start again
  pending_ = true;
  staged_colour_ = c;
  staged_gamma_ = gba_gamma;
  if ((c == Colour::Off || c >= Colour::Count) && !gba_gamma) { staged_ = -1; built_ = true; return; }
  const int b = live_ == 0 ? 1 : 0;
  staged_ = b;
  built_ = false;
  std::vector<u32>& t = buf_[b];
  t.resize(COLOUR_ENTRIES);
  build_ = std::thread([&t, c, gba_gamma, this] {
    for (u32 i = 0; i < COLOUR_ENTRIES; ++i) {
      if ((i & 4095) == 0 && cancel_.load(std::memory_order_relaxed)) return;
      const double in[3] = {expand6((i >> 12) & 63) / 255.0, expand6((i >> 6) & 63) / 255.0, expand6(i & 63) / 255.0};
      double out[3];
      colour_of(c, gba_gamma, in, out);
      t[i] = 0xFF000000u | static_cast<u32>(to_byte(out[0])) << 16 | static_cast<u32>(to_byte(out[1])) << 8 | to_byte(out[2]);
    }
    built_.store(true, std::memory_order_release);
  });
}

bool ColourTable::commit() {
  if (!pending_ || !built_.load(std::memory_order_acquire)) return false;
  if (build_.joinable()) build_.join();
  pending_ = false;
  live_ = staged_;
  live_colour_ = staged_colour_;
  live_gamma_ = staged_gamma_;
  return true;
}

void ColourTable::finish() {
  if (build_.joinable()) build_.join();
  commit();
}

void ColourTable::apply(const u32* src, u32* dst, size_t n) const {
  const u32* lut = this->lut();
  if (!lut) { if (dst != src) std::memcpy(dst, src, n * sizeof(u32)); return; }
  const u64 t0 = timing_on() ? now_ns() : 0;
  u32 was = n ? ~src[0] : 0, is = 0;
  for (size_t i = 0; i < n; ++i) {
    const u32 c = src[i];
    if (c != was) { was = c; is = lut[colour_index(c)]; }
    dst[i] = is;
  }
  if (t0) g_colour_ns.fetch_add(now_ns() - t0, std::memory_order_relaxed);
}

// ---- pattern ---------------------------------------------------------------

namespace {

// Box-filtered coverage of the R, G and B thirds of a pixel by a panel pixel
// `w` wide centred on x, as rgds-core's stripes().
double stripe_cover(int ch, double x, double w) {
  const double lo = ch / 3.0, hi = lo + 1.0 / 3.0, a = x - 0.5 * w, b = x + 0.5 * w;
  double c = 0.0;
  for (int k = -1; k <= 1; ++k) c += clampd(std::min(b, hi + k) - std::max(a, lo + k), 0.0, w);
  return c / w;
}

// The RG DS grid: dark between pixels by how far the panel pixel is into the seam.
double edge(double f, double off, double w) { return clamp01((std::min(f, 1.0 - f) - off) / w + 0.5); }

} // namespace

void pattern_at(Pattern p, double fx, double fy, double ax, double ay, double mul[3], double add[3]) {
  for (int i = 0; i < 3; ++i) { mul[i] = 1.0; add[i] = 0.0; }
  auto all = [&](double m) { for (int i = 0; i < 3; ++i) mul[i] = m; };
  const double aa = std::max(ax, ay);
  const double s = clamp01(1.0 / aa - 1.0);   // the RG DS fade-in over 1x..2x
  auto grid = [&](double off, double dark, double gain, double stripe) {
    const double g = mix(dark, 1.0, edge(fx, off, ax) * edge(fy, off, ay));
    for (int i = 0; i < 3; ++i) {
      const double st = stripe > 0.0 ? 1.0 + stripe * (3.0 * stripe_cover(i, fx, ax) - 1.0) : 1.0;
      mul[i] = mix(1.0, g * st * gain, s);
    }
  };
  switch (p) {
  case Pattern::Off: case Pattern::SharpBilinear: case Pattern::Count: return;
  case Pattern::Lcd: {
    // lcd1x with rgds-core's brightening (BRIGHTEN_SCANLINES 24, BRIGHTEN_LCD 6).
    const double yf = (24.0 + std::sin(2.0 * kPi * (fy - 0.25))) / 25.0;
    const double xf = (6.0 + std::sin(2.0 * kPi * (fx - 0.25))) / 7.0;
    all(xf * yf);
    return;
  }
  case Pattern::ZfastLcd: {
    // zfast_lcd with rgds-core's BORDERMULT 16.
    double y = std::max(std::fabs(fx - 0.5), std::fabs(fy - 0.5));
    y *= y;
    const double yy = y * y, yyy = yy * y;
    all(1.0 - 16.0 * (yy - 2.7 * yyy));
    return;
  }
  case Pattern::Scanlines:
    all(mix(0.88, 1.0, smoothstep(0.05, 0.4, fy)));
    return;
  case Pattern::SoftGrid:
    all(0.90 + 0.10 * smoothstep(0.04, 0.23, fx) * smoothstep(0.04, 0.23, fy));
    return;
  case Pattern::DsClassic: grid(0.07, 0.74, 1.04, 0.16); return;
  case Pattern::DsLite: grid(0.05, 0.78, 1.03, 0.0); return;
  case Pattern::Gba: grid(0.05, 0.80, 1.04, 0.0); return;
  case Pattern::RgbStripes: grid(0.08, 0.62, 1.12, 0.22); return;
  case Pattern::Beads: {
    const double qx = fx - 0.5, qy = fy - 0.5;
    const double bead = clamp01((0.56 - std::hypot(qx, qy)) / aa + 0.5);
    const double dome = 1.0 - 0.38 * smoothstep(0.0, 0.62, std::hypot(qx + 0.12, qy + 0.12));
    const double spec = smoothstep(0.16, 0.0, std::hypot(qx + 0.15, qy + 0.16)) * 0.45;
    const double m = (1.0 - s) + s * ((1.0 - bead) * 0.40 + bead * (1.18 * dome + 0.4 * spec));
    const double a = s * ((1.0 - bead) * 0.01 + bead * 0.6 * spec);
    for (int i = 0; i < 3; ++i) { mul[i] = m; add[i] = a; }
    return;
  }
  case Pattern::Tiles: {
    const double qx = fx - 0.5, qy = fy - 0.5;
    const double inside = 0.16 - std::hypot(std::max(std::fabs(qx) - 0.28, 0.0), std::max(std::fabs(qy) - 0.28, 0.0));
    const double tile = clamp01(inside / aa + 0.5);
    const double rim = smoothstep(0.12, 0.0, inside);
    const double nx = qx + 0.0001, ny = qy + 0.0001, len = std::hypot(nx, ny);
    const double facing = (nx * -0.7071 + ny * -0.7071) / len;
    const double d = qx + qy;
    const double gloss = smoothstep(0.05, -0.12, d) * smoothstep(-0.48, -0.30, d) * 0.22 * (1.0 - rim);
    const double m = (1.0 - s) + s * ((1.0 - tile) * 0.48 + tile * (1.06 + 0.32 * rim * facing + 0.3 * gloss));
    const double a = s * ((1.0 - tile) * 0.01 + tile * 0.7 * gloss);
    for (int i = 0; i < 3; ++i) { mul[i] = m; add[i] = a; }
    return;
  }
  }
}

namespace {

// Where panel pixel x sits in the run of the source pixel it shows, when
// `n` source pixels cover `len` panel pixels in runs [ceil(s*len/n), ceil((s+1)*len/n)).
double run_phase(u32 x, u32 len, u32 n) {
  const u32 s = static_cast<u32>((static_cast<u64>(x) * n) / len);
  const u32 a = static_cast<u32>((static_cast<u64>(s) * len + n - 1) / n);
  const u32 b = static_cast<u32>((static_cast<u64>(s + 1) * len + n - 1) / n);
  return (static_cast<double>(x - a) + 0.5) / static_cast<double>(b - a);
}

u8 q7(double m) { return static_cast<u8>(std::clamp<long>(std::lround(m * 128.0), 0, 255)); }

} // namespace

bool build_pattern(Pattern p, u32 w, u32 h, u32 x0, u32 x1, PatternStore& out) {
  out.clear();
  if (!pattern_is_table(p) || w <= SCREEN_W || h <= SCREEN_H || x0 >= x1 || x1 > w) return false;
  const u32 n = x1 - x0;
  const double ax = static_cast<double>(SCREEN_W) / w, ay = static_cast<double>(SCREEN_H) / h;
  std::vector<double> fx(n);
  for (u32 i = 0; i < n; ++i) fx[i] = run_phase(i + x0, w, SCREEN_W);
  const size_t row_bytes = static_cast<size_t>(n) * 4;
  std::vector<u8> mul_row(row_bytes), add_row(row_bytes);
  std::map<double, u16> by_phase;   // rows at the same phase share a class without recomputing it
  out.row_class.assign(h, 0);
  for (u32 y = 0; y < h; ++y) {
    const double fy = run_phase(y, h, SCREEN_H);
    const auto hit = by_phase.find(fy);
    if (hit != by_phase.end()) { out.row_class[y] = hit->second; continue; }
    bool plain = true;
    for (u32 i = 0; i < n; ++i) {
      double m[3], a[3];
      pattern_at(p, fx[i], fy, ax, ay, m, a);
      // Pixel byte order: B, G, R, A. Alpha passes through at 1.0.
      u8* mb = &mul_row[static_cast<size_t>(i) * 4];
      u8* ab = &add_row[static_cast<size_t>(i) * 4];
      mb[0] = q7(m[2]); mb[1] = q7(m[1]); mb[2] = q7(m[0]); mb[3] = 128;
      ab[0] = to_byte(a[2]); ab[1] = to_byte(a[1]); ab[2] = to_byte(a[0]); ab[3] = 0;
      plain = plain && mb[0] == 128 && mb[1] == 128 && mb[2] == 128 && !ab[0] && !ab[1] && !ab[2];
    }
    // A row the same as one already made is that class.
    u32 c = 0;
    for (; c < out.classes; ++c)
      if (!std::memcmp(&out.mul[c * row_bytes], mul_row.data(), row_bytes) && !std::memcmp(&out.add[c * row_bytes], add_row.data(), row_bytes)) break;
    if (c == out.classes) {
      out.mul.insert(out.mul.end(), mul_row.begin(), mul_row.end());
      out.add.insert(out.add.end(), add_row.begin(), add_row.end());
      out.plain.push_back(plain ? 1 : 0);
      ++out.classes;
    }
    by_phase[fy] = static_cast<u16>(c);
    out.row_class[y] = static_cast<u16>(c);
  }
  out.rows = ds::gpu::Gpu::PatternRows{n, out.mul.data(), out.add.data(), out.row_class.data(), out.plain.data()};
  return true;
}

void ref::modulate_row(const u32* src, const u8* mul, const u8* add, u32 n, u32* dst) {
  const u8* s = reinterpret_cast<const u8*>(src);
  u8* d = reinterpret_cast<u8*>(dst);
  for (size_t i = 0; i < static_cast<size_t>(n) * 4; ++i) {
    const u32 v = std::min<u32>(255, (static_cast<u32>(s[i]) * mul[i] + 64) >> 7);
    d[i] = static_cast<u8>(std::min<u32>(255, v + add[i]));
  }
}

void modulate_row(const u32* src, const u8* mul, const u8* add, u32 n, u32* dst) {
  const u64 t0 = timing_on() ? now_ns() : 0;
#if DSPERATE_NEON
  const u8* s = reinterpret_cast<const u8*>(src);
  u8* d = reinterpret_cast<u8*>(dst);
  const size_t bytes = static_cast<size_t>(n) * 4;
  size_t i = 0;
  for (; i + 16 <= bytes; i += 16) {
    const uint8x16_t c = vld1q_u8(s + i), m = vld1q_u8(mul + i), a = vld1q_u8(add + i);
    const uint16x8_t lo = vmull_u8(vget_low_u8(c), vget_low_u8(m));
    const uint16x8_t hi = vmull_u8(vget_high_u8(c), vget_high_u8(m));
    vst1q_u8(d + i, vqaddq_u8(vcombine_u8(vqrshrn_n_u16(lo, 7), vqrshrn_n_u16(hi, 7)), a));
  }
  if (i < bytes) ref::modulate_row(src + i / 4, mul + i, add + i, static_cast<u32>((bytes - i) / 4), dst + i / 4);
#else
  ref::modulate_row(src, mul, add, n, dst);
#endif
  if (t0) g_pattern_ns.fetch_add(now_ns() - t0, std::memory_order_relaxed);
}

// ---- sharp bilinear ----------------------------------------------------------

namespace {

// sharp-bilinear (Themaister): where in texel space to sample, for the
// panel pixel centred at texel position t, with a prescale of `scale`.
double sharp_sample(double t, double scale) {
  const double fl = std::floor(t), cd = t - fl - 0.5;
  const double range = 0.5 - 0.5 / scale;
  return fl + (cd - clampd(cd, -range, range)) * scale + 0.5;
}

} // namespace

void build_sharp(u32 w, u32 h, u32 x0, u32 x1, std::vector<u16>& sx, std::vector<u8>& wx,
                 std::vector<u16>& ystart, std::vector<u8>& wy) {
  // libretro's automatic prescale: whole panel rows per DS row.
  const double scale = std::max(1.0, std::floor(static_cast<double>(h) / SCREEN_H + 0.01));
  const u32 n = x1 > x0 ? x1 - x0 : 0;
  sx.assign(n, 0); wx.assign(n, 0);
  for (u32 i = 0; i < n; ++i) {
    // Between texel centres, as the bilinear kernel samples (upstream's edge rules).
    const double u = sharp_sample((i + x0 + 0.5) * SCREEN_W / w, scale) - 0.5;
    if (u <= 0.0) continue;
    u32 s = static_cast<u32>(u), f = std::min<u32>(255, static_cast<u32>((u - s) * 256.0));
    if (s >= SCREEN_W - 1) { s = SCREEN_W - 2; f = 255; }
    sx[i] = static_cast<u16>(s); wx[i] = static_cast<u8>(f);
  }
  ystart.assign(SCREEN_H + 1, static_cast<u16>(h));
  wy.assign(h, 0);
  ystart[0] = 0;
  u32 line = 1;
  for (u32 y = 0; y < h; ++y) {
    const double v = sharp_sample((y + 0.5) * SCREEN_H / h, scale) - 0.5;
    // Line L owns the rows from the first whose sample reaches L - 1.
    while (line <= SCREEN_H && v >= static_cast<double>(line) - 1.0) ystart[line++] = static_cast<u16>(y);
    if (v > 0.0) wy[y] = static_cast<u8>(std::min<u32>(255, static_cast<u32>((v - std::floor(v)) * 256.0)));
  }
}

// ---- the display engine's mask -------------------------------------------------

void draw_mask(Pattern p, u32* stage, int panel_w, int px0, int px1, int py0, int py1, int nx, int ny, int rot) {
  const int pw = px1 - px0, ph = py1 - py0;
  if (!pattern_is_mask(p) || pw <= 0 || ph <= 0 || nx <= 0 || ny <= 0) return;
  const bool turned = rot == 90 || rot == 270;
  // DS pixels per panel pixel, along the DS picture's own x and y.
  const double ax = turned ? static_cast<double>(ny) / ph : static_cast<double>(nx) / pw;
  const double ay = turned ? static_cast<double>(nx) / pw : static_cast<double>(ny) / ph;
  // Only a few distinct phases on each axis: the alpha for each pair, once.
  std::vector<double> ux, uy;
  std::vector<int> ix(static_cast<size_t>(pw)), iy(static_cast<size_t>(ph));
  auto index_of = [](std::vector<double>& seen, double v) {
    for (size_t k = 0; k < seen.size(); ++k) if (seen[k] == v) return static_cast<int>(k);
    seen.push_back(v);
    return static_cast<int>(seen.size() - 1);
  };
  for (int x = 0; x < pw; ++x) ix[static_cast<size_t>(x)] = index_of(ux, run_phase(static_cast<u32>(x), static_cast<u32>(pw), static_cast<u32>(nx)));
  for (int y = 0; y < ph; ++y) iy[static_cast<size_t>(y)] = index_of(uy, run_phase(static_cast<u32>(y), static_cast<u32>(ph), static_cast<u32>(ny)));
  std::vector<u32> alpha(ux.size() * uy.size());
  for (size_t j = 0; j < uy.size(); ++j)
    for (size_t i = 0; i < ux.size(); ++i) {
      // The composite turns the DS picture by rot (DispOut::comp_rect).
      const double cx = ux[i], cy = uy[j];
      double fu = cx, fv = cy;
      switch (rot) {
        case 90:  fu = cy;       fv = 1.0 - cx; break;
        case 180: fu = 1.0 - cx; fv = 1.0 - cy; break;
        case 270: fu = 1.0 - cy; fv = cx;       break;
        default: break;
      }
      double m[3], a[3];
      pattern_at(p, fu, fv, ax, ay, m, a);
      alpha[j * ux.size() + i] = static_cast<u32>(to_byte(1.0 - m[1])) << 24;
    }
  for (int y = 0; y < ph; ++y) {
    u32* row = stage + static_cast<size_t>(py0 + y) * static_cast<size_t>(panel_w) + px0;
    const u32* a = &alpha[static_cast<size_t>(iy[static_cast<size_t>(y)]) * ux.size()];
    for (int x = 0; x < pw; ++x) row[x] = a[ix[static_cast<size_t>(x)]];
  }
}

// ---- timing ----------------------------------------------------------------

bool timing_on() {
  static const bool on = [] { const char* e = std::getenv("MINUI_SHADER_TIMING"); return e && !std::strcmp(e, "1"); }();
  return on;
}

void timing_frame(u32 now_ms, Colour c, Pattern p) {
  if (!timing_on()) return;
  static u32 frames = 0, mark = 0;
  static bool started = false;
  if (!started) { started = true; mark = now_ms; }
  ++frames;
  if (now_ms - mark < 5000) return;
  const double colour_us = g_colour_ns.exchange(0) / 1000.0 / frames;
  const double pattern_us = g_pattern_ns.exchange(0) / 1000.0 / frames;
  std::fprintf(stderr, "shader timing: colour %s %.0f us/frame, pattern %s %.0f us/frame\n",
               colour_name(c), colour_us, pattern_name(p), pattern_us);
  frames = 0;
  mark = now_ms;
}

} // namespace ds::sdl::shader
