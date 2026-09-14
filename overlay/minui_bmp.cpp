// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak. See minui.h.
//
// Its own translation unit, and deliberately free of SDL: the rest of the
// module needs SDL for its timers, and linking that into tests/bmp_test.cpp
// would mean the test could only run somewhere libSDL2 can be loaded, which
// the build containers cannot. Nothing here needs more than libc.
#include "minui.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ds::sdl {

bool write_bmp(const u8* rgba, int w, int h, const std::string& path) {
  if (!rgba || w <= 0 || h <= 0 || path.empty()) return false;

  const u32 stride = static_cast<u32>(w) * 4;
  const u32 pixels = stride * static_cast<u32>(h);
  const u32 offset = 14 + 40;                    // file header + BITMAPINFOHEADER
  const u32 size = offset + pixels;

  std::vector<u8> out(size, 0);
  u8* p = out.data();
  auto put16 = [&](size_t at, u32 v) { p[at] = v & 0xFF; p[at + 1] = (v >> 8) & 0xFF; };
  auto put32 = [&](size_t at, u32 v) {
    p[at] = v & 0xFF; p[at + 1] = (v >> 8) & 0xFF;
    p[at + 2] = (v >> 16) & 0xFF; p[at + 3] = (v >> 24) & 0xFF;
  };

  p[0] = 'B'; p[1] = 'M';
  put32(2, size);
  put32(10, offset);
  put32(14, 40);                                 // header size
  put32(18, static_cast<u32>(w));
  put32(22, static_cast<u32>(h));                // positive: rows stored bottom-up
  put16(26, 1);                                  // planes
  put16(28, 32);                                 // bpp
  put32(30, 0);                                  // BI_RGB
  put32(34, pixels);

  // Two differences from the source buffer, both handled in this one pass:
  // BI_RGB stores B,G,R,A where the composer wrote R,G,B,A, and BMP's first
  // row is the bottom one.
  for (int y = 0; y < h; ++y) {
    const u8* src = rgba + static_cast<size_t>(h - 1 - y) * stride;
    u8* dst = p + offset + static_cast<size_t>(y) * stride;
    for (int x = 0; x < w; ++x) {
      dst[x * 4 + 0] = src[x * 4 + 2];   // B
      dst[x * 4 + 1] = src[x * 4 + 1];   // G
      dst[x * 4 + 2] = src[x * 4 + 0];   // R
      dst[x * 4 + 3] = src[x * 4 + 3];   // A
    }
  }

  const std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  const bool ok = f && std::fwrite(out.data(), 1, out.size(), f) == out.size();
  if (f) std::fclose(f);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

} // namespace ds::sdl
