// SPDX-License-Identifier: GPL-3.0-or-later
// Renders every page of the MinUI-style pause menu (overlay/minui_menu.h) to
// PNGs, in both looks and at the panel size of every device the pak runs on,
// so a change to the menu can be seen without a device. Built and run inside
// the toolchain container by `make screenshots`; see TECHNICAL.md.
//
//   render-menu <out dir> <MinUI font or ""> <NextUI font or "">
//
// Everything behind the menu is a stand-in: a generated picture for the
// game, fake settings, bindings, cheats and achievements, and a battery at
// three quarters. The menu itself is the code that ships.
#include "frontend/sdl/minui_menu.h"
#include "frontend/sdl/minui_gfx.h"
#include "core/cart/miniz/miniz_tdef.h"
#include "core/cheat/database.h"
#include "core/io/io.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

using ds::u32;
using ds::u8;
using ds::sdl::Menu;
using ds::sdl::MinUiMenu;
using B = ds::io::Io::Button;

namespace {

struct Host final : ds::sdl::SettingsHost {
  std::string get(const char* key) const override {
    const auto it = kHot.find(key);
    return it == kHot.end() ? "" : it->second;
  }
  void set(const char*, const std::string&) override {}
  bool enabled(const ds::sdl::Setting&) const override { return true; }
  const char* disabled_reason(const ds::sdl::Setting&) const override { return ""; }
  bool value_allowed(const ds::sdl::Setting&, const char*) const override { return true; }
  void commit() override {}
  bool save_per_game() const override { return false; }
  void set_save_per_game(bool) override {}
  bool has_game() const override { return true; }
  int binding_count(bool) const override { return static_cast<int>(kBinds.size()); }
  Binding binding(bool, int i) const override {
    const auto& b = kBinds[static_cast<size_t>(i)];
    return Binding{std::string("pad.") + b.first, b.first, b.second};
  }
  bool has_pad() const override { return true; }
  void begin_capture(bool) override {}
  void cancel_capture() override {}
  bool capturing() const override { return false; }
  std::string take_capture() override { return ""; }
  void bind(const std::string&, const std::string&) override {}
  void reset_bindings(bool) override {}
  std::vector<std::string> collisions() const override { return {}; }
  const char* user_settings_note() const override { return nullptr; }

  // As upstream hands them over: Input::pad_label's spellings.
  const std::vector<std::pair<const char*, const char*>> kBinds = {
    {"A", "\x02 EAST"}, {"B", "\x01 SOUTH"}, {"X", "\x04 NORTH"}, {"Y", "\x03 WEST"}, {"L", "L1"}, {"R", "R1"},
    {"START", "START"}, {"SELECT", "SELECT"}, {"UP", "UP"}, {"DOWN", "DOWN"}, {"LEFT", "LEFT"},
    {"RIGHT", "RIGHT"}, {"QUIT", "NONE"}, {"PAUSE", "NONE"},
    {"PAUSE (2)", "MOD"}, {"FAST_FORWARD", "R2"}, {"SCREEN_SWAP", "MOD+L2"},
  };
  // The shipped profiles' [padhotkeys], for the Shortcuts page.
  const std::map<std::string, std::string> kHot = {
    {"padhotkeys.quit", "none"}, {"padhotkeys.pause", "none"}, {"padhotkeys.pause.alt", "mod"},
    {"padhotkeys.fast_forward_toggle", "none"}, {"padhotkeys.layout_next", "none"},
    {"padhotkeys.layout_prev", "none"}, {"padhotkeys.screen_swap", "mod++lefttrigger"},
    {"padhotkeys.screenshot", "none"}, {"padhotkeys.fps", "none"}, {"padhotkeys.game_switcher", "mod+back"},
    {"padhotkeys.mic", "none"}, {"padhotkeys.lid", "none"},
    {"padhotkeys.pip_corner_next", "mod+start"}, {"padhotkeys.save_state", "none"},
    {"padhotkeys.load_state", "none"}, {"padhotkeys.slot_next", "none"}, {"padhotkeys.slot_prev", "none"},
  };
};

struct Cheevos final : ds::sdl::CheevosHost {
  std::string status() const override { return "SIGNED IN AS PLAYER ONE"; }
  std::string progress() const override { return "2/5 EARNED  15/50 POINTS"; }
  bool signed_in() const override { return true; }
  bool has_set() const override { return true; }
  int row_count() const override { return static_cast<int>(kRows.size()); }
  Row row(int i) const override { return kRows[static_cast<size_t>(i)]; }
  void sign_in(const std::string&, const std::string&) override {}
  void sign_out() override {}
  bool option(Option o) const override { return o == Option::Toasts; }
  void set_option(Option, bool) override {}

  const std::vector<Row> kRows = {
    {"First Steps", "Clear the first stage", 5, true, false},
    {"Collector", "Find every hidden coin in world one", 10, true, false},
    {"Speed Runner", "Clear world one in under ten minutes", 10, false, false},
    {"No Damage", "Clear a boss without taking a hit", 25, false, false},
    {"Glitch Hunter", "Trigger the warp in the sewer level", 0, false, true},
  };
};

struct Cheats {
  std::vector<ds::cheat::Code> codes;
  std::vector<ds::cheat::Group> groups;
  Cheats() {
    groups.push_back({"Player", "", false});
    groups.push_back({"Difficulty", "", true});
    const auto add = [&](const char* name, int group, bool on, bool note) {
      ds::cheat::Code c;
      c.name = name;
      c.group = group;
      c.enabled = on;
      if (!note) c.words = {0x02000000, 1};
      codes.push_back(c);
    };
    add("(M) Must be on", 0, false, true);
    add("Infinite Lives", 0, true, false);
    add("Infinite Health", 0, false, false);
    add("Max Coins", 0, false, false);
    add("Easy", 1, false, false);
    add("Normal", 1, true, false);
    add("Hard", 1, false, false);
  }
};

// Two DS screens, stacked and centred, the way the default layout shows them.
void game_frame(std::vector<u32>& px, int w, int h) {
  const int sh = h / 2, sw = std::min(w, sh * 4 / 3), x0 = (w - sw) / 2;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      u32 c = 0xFF000000;
      if (x >= x0 && x < x0 + sw) {
        const int sx = (x - x0) * 256 / sw, sy = (y % sh) * 192 / sh;
        const bool bottom = y >= sh;
        const u32 r = bottom ? 60 + sx / 4 : 70 + sy / 2, g = bottom ? 90 + sy / 3 : 130 + sx / 5, b = bottom ? 150 : 200 - sy / 3;
        c = 0xFF000000 | (r << 16) | (g << 8) | b;
        if (((sx / 16) + (sy / 16)) % 2) c = 0xFF000000 | ((c >> 1) & 0x007F7F7F) + 0x00202020;
      }
      px[static_cast<size_t>(y) * w + x] = c;
    }
}

void dim(std::vector<u32>& px) {
  for (u32& c : px) c = 0xFF000000 | ((c >> 1) & 0x007F7F7F);
}

bool save_png(const std::vector<u32>& px, int w, int h, const std::string& path) {
  std::vector<u8> rgba(px.size() * 4);
  for (size_t i = 0; i < px.size(); ++i) {
    rgba[i * 4] = (px[i] >> 16) & 0xFF;
    rgba[i * 4 + 1] = (px[i] >> 8) & 0xFF;
    rgba[i * 4 + 2] = px[i] & 0xFF;
    rgba[i * 4 + 3] = 0xFF;
  }
  size_t len = 0;
  void* png = tdefl_write_image_to_png_file_in_memory_ex(rgba.data(), w, h, 4, &len, 6, MZ_FALSE);
  if (!png) return false;
  FILE* f = std::fopen(path.c_str(), "wb");
  const bool ok = f && std::fwrite(png, 1, len, f) == len;
  if (f) std::fclose(f);
  std::free(png);
  return ok;
}

void write_text(const std::string& path, const char* s) {
  if (FILE* f = std::fopen(path.c_str(), "w")) { std::fputs(s, f); std::fclose(f); }
}

u32 press(B b) { return 1u << b; }

} // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <out dir> <MinUI font or \"\"> <NextUI font or \"\">\n", argv[0]);
    return 2;
  }
  const std::string out = argv[1];
  const struct { const char* name; bool nextui; std::string font; } looks[] = {
    {"minui", false, argv[2]}, {"nextui", true, argv[3]},
  };
  // The panels the pak runs on: h700, the RG CubeXX, the Brick, and the
  // Smart Pro family.
  const int sizes[][2] = {{640, 480}, {720, 720}, {1024, 768}, {1280, 720}};

  ::mkdir(out.c_str(), 0755);
  const std::string bat = out + "/.battery";
  ::mkdir(bat.c_str(), 0755);
  ::mkdir((bat + "/battery").c_str(), 0755);
  write_text(bat + "/battery/type", "Battery\n");
  write_text(bat + "/battery/capacity", "76\n");
  write_text(bat + "/battery/status", "Discharging\n");

  int written = 0;
  for (const auto& look : looks) {
    for (const auto& size : sizes) {
      const int w = size[0], h = size[1];
      std::vector<u32> px(static_cast<size_t>(w) * h);
      // The slot thumbnail is the undimmed picture, as write_png saves it.
      game_frame(px, w, h);
      const std::string thumb = out + "/.thumb-" + std::to_string(w) + "x" + std::to_string(h) + ".bmp";
      save_png(px, w, h, thumb);

      Host host;
      Cheevos cheevos;
      Cheats cheats;
      MinUiMenu m(true);
      ds::sdl::minui_gfx::Theme theme;
      theme.nextui = look.nextui;
      theme.font = look.font;
      m.set_theme(theme);
      m.set_battery_root(bat);
      m.set_title("Example Game");
      m.set_settings_host(&host);
      m.set_cheevos_host(&cheevos);
      m.set_cheats(&cheats.codes, &cheats.groups);
      m.set_thumbnails([&](int slot) { return slot == 1 ? thumb : std::string(); });
      for (int s : {1, 4, MinUiMenu::kAutoSlot}) m.set_slot_used(s, true);

      const ds::sdl::Canvas canvas{px.data(), static_cast<u32>(w), w, h};
      const auto shot = [&](const char* scene) {
        game_frame(px, w, h);
        dim(px);   // what the frontend does before it draws the menu
        m.draw(canvas);
        const std::string path = out + "/" + look.name + "-" + std::to_string(w) + "x" + std::to_string(h) + "-" + scene + ".png";
        if (!save_png(px, w, h, path)) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); std::exit(1); }
        ++written;
      };
      // Walks from the root: Continue, Save, Load, Options, Quit; Options is
      // Emulation, Visual FX, Layout, Controls, Shortcuts, DS Options,
      // Cheats, Achievements, Manage Save Slots, Save Changes, Reset Game.
      const auto options_row = [&](int row) {
        m.set_slot(1);
        m.set_open(true);
        for (int i = 0; i < 3; ++i) m.input(press(B::BTN_DOWN));
        m.input(press(B::BTN_A));
        for (int i = 0; i < row; ++i) m.input(press(B::BTN_DOWN));
      };

      m.set_slot(1);
      m.set_open(true);
      shot("root");
      m.input(press(B::BTN_DOWN));
      shot("save");
      m.input(press(B::BTN_DOWN));
      m.input(press(B::BTN_RIGHT));
      shot("load-empty-slot");
      options_row(0);
      shot("options");
      const struct { int row; const char* scene; } pages[] = {
        {0, "emulation"}, {1, "visual-fx"}, {2, "layout"}, {3, "controls"}, {4, "shortcuts"},
        {5, "ds-options"}, {6, "cheats"}, {7, "achievements"}, {8, "manage-save-slots"},
      };
      for (const auto& p : pages) {
        options_row(p.row);
        m.input(press(B::BTN_A));
        shot(p.scene);
      }
      options_row(7);
      m.input(press(B::BTN_A));
      m.input(press(B::BTN_DOWN));
      m.input(press(B::BTN_A));
      shot("achievement-list");
      options_row(5);
      m.input(press(B::BTN_A));
      m.input(press(B::BTN_A));
      shot("name-editor");

      // The notice a layout hotkey raises, over the undimmed game: as it
      // holds, and half way through its fade.
      m.set_open(false);
      m.notify(MinUiMenu::setting_notice("video.layout", "dominant_v"), 0);
      for (const auto& n : {std::make_pair(0u, "notice"),
                            std::make_pair(MinUiMenu::kNoticeHoldMs + MinUiMenu::kNoticeFadeMs / 2, "notice-fading")}) {
        game_frame(px, w, h);
        m.draw_notice(canvas, n.first);
        const std::string path = out + "/" + look.name + "-" + std::to_string(w) + "x" + std::to_string(h) + "-" + n.second + ".png";
        if (!save_png(px, w, h, path)) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); std::exit(1); }
        ++written;
      }
    }
  }
  std::printf("render-menu: %d screenshots in %s\n", written, out.c_str());
  return 0;
}
