// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak. Compiled into DSperate (GPLv3) by
// patches/0003-minui-integration.patch.
//
// Native power button and MinUI/NextUI launcher integration.
//
// Two jobs, both of which a MinUI handheld needs and neither of which upstream
// has a reason to carry:
//
//  - The power button. MinUI paks normally delegate this to an external
//    watcher (minui-power-control), which cannot save the game, does not
//    support every platform, and is a second process racing this one for the
//    same key. Handling it in-process means a short press can pause and blank
//    the panel with the emulator still resident, and a long press can go out
//    through the emulator's ordinary clean-exit path with the battery save and
//    the auto state written.
//
//  - The handful of files MinUI's game switcher reads. They are plain text and
//    a bitmap; the formats are in the table in TECHNICAL.md.
//
// The module is deliberately ignorant of NDS, Display and Input so the patch
// that wires it in stays small enough to rebase on an upstream tag bump. The
// emulator passes callbacks; this file passes none of its own types back.
//
// Everything is best effort. With no MINUI_* environment (a desktop build, or
// a launcher that does not set it) every writer is a no-op and poll() simply
// never finds a power key, so behaviour is exactly upstream's.
#pragma once
#include "core/types.h"

#include <functional>
#include <string>

namespace ds::sdl {

class MinUi {
public:
  enum class Event {
    None,
    Sleep,     // short press
    PowerOff,  // held for LONG_PRESS_MS
  };

  // Reads the MINUI_* environment and finds the evdev node that reports
  // KEY_POWER. Harmless where there is neither.
  void open();
  void close();

  // Once a frame, from the top of the loop body so it runs while paused too.
  // Non-blocking.
  Event poll();

  // The blocking half of a short press: runs `before`, takes the panel and the
  // audio down, waits for the next press, brings them back, runs `after`.
  // Returns when the user has woken the device.
  void sleep(const std::function<void()>& before, const std::function<void()>& after);

  // MinUI artifacts. Each is a no-op when the environment did not name a path.
  void write_auto_resume() const;    // survives a battery death; cleared on wake
  void clear_auto_resume() const;
  void write_slot_marker(int slot) const;
  void write_game_switcher() const;
  // Empty when there is no MINUI_DIR, or when the slot is outside 0..MAX_SLOT.
  std::string slot_thumbnail_path(int slot) const;

  bool active() const { return !dir_.empty(); }

  // The highest slot the switcher will look at. minarch's MENU_SLOT_COUNT is 8,
  // and its marker file only ever holds 0..7; 8 is its "hidden default" and 9
  // its sleep autosave, which is DSperate's ".auto" here.
  static constexpr int MAX_SLOT = 7;

private:
  void set_backlight(int level) const;
  int  read_backlight() const;

  int         fd_ = -1;
  bool        down_ = false;
  u32         pressed_at_ = 0;
  std::string rom_path_;      // card-relative, for auto_resume.txt
  std::string rom_file_;      // basename with extension: the artifact key
  std::string dir_;           // .minui/<EMU>
  std::string shared_dir_;    // .minui
};

} // namespace ds::sdl
