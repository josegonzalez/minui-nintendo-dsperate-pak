// SPDX-License-Identifier: GPL-3.0-or-later
// write_bmp() from the vendored MinUI module: the launcher reads these files,
// so the header has to be right and the rows have to come out the way BMP
// stores them, which is bottom-up and B,G,R,A where the composer wrote R,G,B,A.
#include "check.h"
#include "frontend/sdl/minui.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using ds::u32;
using ds::u8;

namespace {

u32 le32(const std::vector<u8>& b, size_t at) {
  return static_cast<u32>(b[at]) | (static_cast<u32>(b[at + 1]) << 8) |
         (static_cast<u32>(b[at + 2]) << 16) | (static_cast<u32>(b[at + 3]) << 24);
}
u32 le16(const std::vector<u8>& b, size_t at) {
  return static_cast<u32>(b[at]) | (static_cast<u32>(b[at + 1]) << 8);
}

std::vector<u8> read_all(const std::string& path) {
  std::vector<u8> out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return out;
  u8 buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
  std::fclose(f);
  return out;
}

} // namespace

int main() {
  const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/ds_bmp_test.bmp";
  std::remove(path.c_str());

  // 2x2, RGBA bytes. Top row red then green, bottom row blue then white.
  const u8 rgba[] = {
    0xFF, 0x00, 0x00, 0xFF,   0x00, 0xFF, 0x00, 0xFF,
    0x00, 0x00, 0xFF, 0xFF,   0xFF, 0xFF, 0xFF, 0xFF,
  };
  CHECK(ds::sdl::write_bmp(rgba, 2, 2, path));

  const std::vector<u8> b = read_all(path);
  CHECK(b.size() == 14 + 40 + 2 * 2 * 4);
  CHECK(b[0] == 'B' && b[1] == 'M');
  CHECK(le32(b, 2) == b.size());
  CHECK(le32(b, 10) == 54);          // pixel offset
  CHECK(le32(b, 14) == 40);          // BITMAPINFOHEADER
  CHECK(le32(b, 18) == 2);           // width
  CHECK(le32(b, 22) == 2);           // height, positive: bottom-up
  CHECK(le16(b, 26) == 1);           // planes
  CHECK(le16(b, 28) == 32);          // bpp
  CHECK(le32(b, 30) == 0);           // BI_RGB

  // First stored row is the source's LAST row, channel-swapped to B,G,R,A.
  const u8* px = b.data() + 54;
  CHECK(px[0] == 0xFF && px[1] == 0x00 && px[2] == 0x00 && px[3] == 0xFF);   // blue
  CHECK(px[4] == 0xFF && px[5] == 0xFF && px[6] == 0xFF && px[7] == 0xFF);   // white
  // Second stored row is the source's first.
  CHECK(px[8] == 0x00 && px[9] == 0x00 && px[10] == 0xFF && px[11] == 0xFF); // red
  CHECK(px[12] == 0x00 && px[13] == 0xFF && px[14] == 0x00 && px[15] == 0xFF); // green

  // Refuses what it cannot write rather than producing a truncated file.
  CHECK(!ds::sdl::write_bmp(rgba, 0, 2, path));
  CHECK(!ds::sdl::write_bmp(nullptr, 2, 2, path));
  CHECK(!ds::sdl::write_bmp(rgba, 2, 2, ""));

  std::remove(path.c_str());
  std::printf("bmp: ok\n");
  return 0;
}
