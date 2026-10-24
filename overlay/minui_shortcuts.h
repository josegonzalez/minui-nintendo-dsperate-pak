// SPDX-License-Identifier: GPL-3.0-or-later
// Vendored by minui-nintendo-dsperate-pak. Compiled into DSperate (GPLv3) by
// patches/0003-minui-integration.patch.
//
// The pak's modifier chords, which the pause menu's Shortcuts page turns on
// and off. A shortcut that is on holds `chord` under [padhotkeys]; one that
// is off holds "none". Which are on out of the box is the device profile's
// business (config/dsperate/*.ini), not this table's: the table only says
// what "on" means, so a shortcut a profile ships off can still be turned on.
// A chord on a control the pad mapping does not name (L3 and R3 on a device
// without sticks) cannot be pressed there, and the page says so.
//
// One entry per line, in this exact shape: tests/makefile.bats reads the
// chords out of this file to check every profile against them.
#pragma once

namespace ds::sdl::minui_shortcuts {

struct Shortcut {
  const char* key;        // under [padhotkeys]
  const char* label;      // the Shortcuts page's row
  const char* chord;      // the binding when on
  const char* button;     // what the chord adds to the modifier, as the page names it; "" for the tap
  const char* upstream;   // what an absent key means: upstream's default (src/frontend/sdl/input.cpp)
};

inline constexpr Shortcut kShortcuts[] = {
  {"pause.alt", "Tap: pause menu", "mod", "", "none"},
  {"pause", "Pause menu", "mod+y", "Y", "mod+start"},
  {"quit", "Quit", "mod+a", "A", "mod+start+back"},
  {"layout_next", "Next layout", "mod+dpup", "UP", "mod+back"},
  {"layout_prev", "Previous layout", "mod+dpdown", "DOWN", "mod+x"},
  {"screen_swap", "Swap screens", "mod++lefttrigger", "L2", "mod+y"},
  {"pip_corner_next", "Move PiP screen", "mod+start", "START", "none"},
  {"screenshot", "Screenshot", "mod+b", "B", "none"},
  {"fps", "FPS counter", "mod+x", "X", "none"},
  {"fast_forward_toggle", "Fast forward", "mod++righttrigger", "R2", "none"},
  {"game_switcher", "Game switcher", "mod+back", "SELECT", "none"},
  {"mic", "Blow into the mic (hold)", "mod+leftstick", "L3", "leftstick"},
  {"lid", "Close the lid", "mod+rightstick", "R3", "none"},
  {"save_state", "Save state", "mod+rightshoulder", "R1", "mod+rightshoulder"},
  {"load_state", "Load state", "mod+leftshoulder", "L1", "mod+leftshoulder"},
  {"slot_next", "Next state slot", "mod+dpright", "RIGHT", "mod+dpright"},
  {"slot_prev", "Previous state slot", "mod+dpleft", "LEFT", "mod+dpleft"},
};
inline constexpr int kShortcutCount = static_cast<int>(sizeof kShortcuts / sizeof kShortcuts[0]);

// The buttons that may be the modifier, in the order the page steps through
// them, and only those the exported pad mapping names (MINUI_PAD_BUTTONS).
// SELECT and START are DS buttons as well: as the modifier, the game sees
// them only as a tap on release, and not at all while a tap opens the menu.
struct Modifier {
  const char* name;       // SDL's, as [padhotkeys] modifier takes it
  const char* label;
  bool ds_button;
};

inline constexpr Modifier kModifiers[] = {
  {"guide", "MENU", false},
  {"back", "SELECT", true},
  {"start", "START", true},
  {"leftstick", "L3", false},
  {"rightstick", "R3", false},
  {"misc1", "MISC", false},
  {"paddle1", "PADDLE 1", false},
  {"paddle2", "PADDLE 2", false},
  {"paddle3", "PADDLE 3", false},
  {"paddle4", "PADDLE 4", false},
};
inline constexpr int kModifierCount = static_cast<int>(sizeof kModifiers / sizeof kModifiers[0]);

// Upstream's default modifier on a pad (padhotkeys.modifier).
inline constexpr const char* kDefaultModifier = "guide";

} // namespace ds::sdl::minui_shortcuts
