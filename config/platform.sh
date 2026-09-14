#!/bin/sh
# Per-platform and per-device facts, kept out of launch.sh so tests/platform.bats
# can assert them without a device: dsp_platform_profile does no I/O. It reads
# its two arguments and sets the PROFILE_* variables everything else consumes.
#
# The button indices come from NextUI's own pad enumeration, by way of
# minui-n64-pak's config/shared/platform.sh, which follows
# workspace/h700/platform/platform.c. TrimUI pads start at button 0; NextUI's
# h700 SDL2 enumerates in ascending evdev keycode order and the Anbernic pad
# reports ESC (1) and two volume keys (114/115) before the gamepad codes
# (304-316), so its gamepad buttons start at index 3. Stick-click codes 313 (L3)
# and 316 (R3) exist only where a stick does, which shifts L2/R2 again -- hence
# three h700 classes.

# The mapping bodies below are SDL GameController mappings in SDL's POSITIONAL
# convention: "a" is the South button, "b" East, "x" West, "y" North.
#
# This matters and the wrong choice is not a visible break. DSperate's default
# [pad] block reads the DS A button from SDL's "b" (East), which on a
# Nintendo-layout pad is the button physically marked A. Label-named mappings
# would give a symmetric A/B and X/Y swap instead -- easy to misread as a
# broken mapping rather than the wrong convention. spruceOS makes the same
# choice for the same reason (helperFunctions.sh, export_sdl_gamecontroller_map).
#
# MENU becomes "guide" so DSperate's padhotkeys.modifier lands on it, and
# Select becomes "back".

# TrimUI pads: buttons from 0, analog triggers on axes 2 and 5.
DSP_MAP_TRIMUI='a:b1,b:b0,x:b3,y:b2,back:b6,guide:b8,start:b7,leftshoulder:b4,rightshoulder:b5,lefttrigger:a2,righttrigger:a5,leftstick:b9,rightstick:b10,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a3,righty:a4'

# TrimUI Brick: no sticks at all, so no leftstick/rightstick and no axes.
DSP_MAP_TRIMUI_NOSTICK='a:b1,b:b0,x:b3,y:b2,back:b6,guide:b8,start:b7,leftshoulder:b4,rightshoulder:b5,lefttrigger:a2,righttrigger:a5,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2'

# h700 with two sticks: L3 at 12 shifts L2/R2 to 13/14, R3 takes 15.
DSP_MAP_H700_STICKS='a:b3,b:b4,x:b5,y:b6,back:b9,guide:b11,start:b10,leftshoulder:b7,rightshoulder:b8,lefttrigger:b13,righttrigger:b14,leftstick:b12,rightstick:b15,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3'

# h700 with a left stick only: L3 exists, R3 does not.
DSP_MAP_H700_LSTICK='a:b3,b:b4,x:b5,y:b6,back:b9,guide:b11,start:b10,leftshoulder:b7,rightshoulder:b8,lefttrigger:b13,righttrigger:b14,leftstick:b12,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1'

# h700 with no sticks: no stick clicks, so L2/R2 sit at 12/13.
DSP_MAP_H700_NOSTICK='a:b3,b:b4,x:b5,y:b6,back:b9,guide:b11,start:b10,leftshoulder:b7,rightshoulder:b8,lefttrigger:b12,righttrigger:b13,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2'

# The PROFILE_* variables are read by launch.sh, which sources this file, so
# their use is not visible from here.
# shellcheck disable=SC2034
# dsp_platform_profile <platform> <device>
#
# Sets:
#   PROFILE_INI      which configs/<name>.ini to seed from
#   PROFILE_MAP      the SDL GameController mapping body
#   PROFILE_ROTATE   DS_ROTATE value, empty for none
#   PROFILE_LD_DIRS  extra LD_LIBRARY_PATH entries, space separated
#   PROFILE_POWER    1 when minui-power-control supports this platform
dsp_platform_profile() {
    _platform="$1"
    _device="$2"

    PROFILE_ROTATE=""
    PROFILE_LD_DIRS=""
    PROFILE_POWER=0

    case "$_platform" in
    tg5040)
        # SDL2 lives outside the pak on TrimUI.
        PROFILE_LD_DIRS="/usr/trimui/lib"
        PROFILE_POWER=1
        case "$_device" in
        # The Brick is the one TrimUI device with no sticks at all.
        brick)
            PROFILE_INI="no-sticks"
            PROFILE_MAP="$DSP_MAP_TRIMUI_NOSTICK"
            ;;
        *)
            PROFILE_INI="two-sticks"
            PROFILE_MAP="$DSP_MAP_TRIMUI"
            ;;
        esac
        ;;
    tg5050)
        PROFILE_LD_DIRS="/usr/trimui/lib"
        PROFILE_POWER=1
        PROFILE_INI="two-sticks"
        PROFILE_MAP="$DSP_MAP_TRIMUI"
        ;;
    h700)
        # minui-power-control has no h700 support, so the power button keeps
        # its default behaviour there.
        PROFILE_POWER=0
        case "${_device:-rg35xxplus}" in
        rg35xxh | rg35xxpro | rg40xxh | rgcubexx | rg34xxsp)
            PROFILE_INI="two-sticks"
            PROFILE_MAP="$DSP_MAP_H700_STICKS"
            ;;
        rg40xxv)
            PROFILE_INI="one-stick"
            PROFILE_MAP="$DSP_MAP_H700_LSTICK"
            ;;
        *)
            # rg28xx, rg34xx, rg35xxplus, rg35xxsp, rgsp.
            PROFILE_INI="no-sticks"
            PROFILE_MAP="$DSP_MAP_H700_NOSTICK"
            ;;
        esac
        # The rg28xx panel is mounted portrait. NextUI exports SDL_ROTATION=1 so
        # applications see 640x480 landscape, but DSperate's fbdev tier writes
        # /dev/fb0 directly and never goes through SDL, so that rotation does not
        # reach it. DS_ROTATE is an environment variable, not a flag, and is read
        # only on the disp and fbdev paths.
        if [ "$_device" = "rg28xx" ]; then
            PROFILE_ROTATE=270
        fi
        ;;
    *)
        PROFILE_INI=""
        PROFILE_MAP=""
        ;;
    esac

    unset _platform _device
}
