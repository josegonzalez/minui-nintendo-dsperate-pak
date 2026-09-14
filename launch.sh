#!/bin/sh
PAK_DIR="$(dirname "$0")"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

SUPPORTED_PLATFORMS="tg5040 tg5050 h700"

# Whole word match against a space separated list. A plain `grep -q "$PLATFORM"`
# also accepts every substring, so rg35xx and tg50 would both pass.
platform_in_list() {
    case " $2 " in
    *" $1 "*)
        return 0
        ;;
    esac

    return 1
}

# NextUI reports the Trimui Brick as tg5040 with DEVICE=brick. Older MinUI builds
# reported it as its own tg3040 platform with no DEVICE at all.
dsp_normalize_platform() {
    if [ "${PLATFORM:-}" = "tg3040" ] && [ -z "${DEVICE:-}" ]; then
        PLATFORM="tg5040"
        DEVICE="brick"
        export PLATFORM DEVICE
    fi
}

dsp_init_env() {
    BIN_DIR="$PAK_DIR/$PLATFORM"

    DSP_USERDATA_DIR="$USERDATA_PATH/DSP-dsperate"
    DSP_CONFIG_DIR="$DSP_USERDATA_DIR/dsperate"
    DSP_CONFIG="$DSP_CONFIG_DIR/dsperate.ini"
    DSP_DEVICE_FILE="$DSP_USERDATA_DIR/device.txt"

    DSP_SAVES_DIR="$SDCARD_PATH/Saves/DSP"
    DSP_STATES_DIR="$SHARED_USERDATA_PATH/DSP-dsperate"
    DSP_SCREENSHOTS_DIR="$SDCARD_PATH/Screenshots"
    DSP_CHEATS_DIR="$SDCARD_PATH/Cheats/DSP"
    DSP_BIOS_DIR="$BIOS_PATH/DSP"
    DSP_ROMS_DIR="$SDCARD_PATH/Roms/Nintendo DS (DSP)"

    # DSperate reads its ini from $XDG_CONFIG_HOME/dsperate/, creating the
    # directory and a games/ beside it (Config::dir in src/frontend/sdl).
    export HOME="$DSP_USERDATA_DIR"
    export XDG_CONFIG_HOME="$DSP_USERDATA_DIR"

    export PATH="$BIN_DIR:$PAK_DIR:$PATH"

    # What the launcher's game switcher reads. The key everywhere is the ROM's
    # basename *with* its extension, which is what minarch itself keys on.
    MINUI_SHARED_DIR="$SHARED_USERDATA_PATH/.minui"
    MINUI_DIR="$MINUI_SHARED_DIR/DSP"
    export MINUI_SHARED_DIR MINUI_DIR
    MINUI_ROM_FILE="$(basename "$ROM_PATH")"
    export MINUI_ROM_FILE
    # auto_resume.txt holds the path relative to the card, and the frontend
    # rebuilds it with a bare concat. An absolute path here becomes
    # /mnt/SDCARD/mnt/SDCARD/... and the resume silently never happens, so it
    # is better to export nothing than to export something wrong.
    case "$ROM_PATH" in
    "$SDCARD_PATH"/*)
        export MINUI_ROM_PATH="${ROM_PATH#"$SDCARD_PATH"}"
        ;;
    *)
        echo "ROM is outside $SDCARD_PATH, so auto-resume is unavailable: $ROM_PATH"
        ;;
    esac

    # Bundled first, then the SDL2 and friends NextUI installs, then whatever the
    # platform profile adds (/usr/trimui/lib on TrimUI), then the inherited value.
    DSP_LD_PATH="$BIN_DIR:$SDCARD_PATH/.system/$PLATFORM/lib"
    for dir in $PROFILE_LD_DIRS; do
        DSP_LD_PATH="$DSP_LD_PATH:$dir"
    done
    export LD_LIBRARY_PATH="$DSP_LD_PATH:${LD_LIBRARY_PATH:-}"
}

# DSperate opens the pad with SDL_IsGameController / SDL_GameControllerOpen and
# has no joystick fallback, and MinUI ships no GameController mapping, so
# without this there is no input at all. The GUID is read from the device
# because SDL derives it from evdev ids and hashes it differently across SDL
# versions; the mapping body comes from the platform profile.
dsp_export_pad_mapping() {
    if [ -z "$PROFILE_MAP" ]; then
        echo "No pad mapping for $PLATFORM/${DEVICE:-}, leaving SDL_GAMECONTROLLERCONFIG alone"
        return 0
    fi

    _guid_out="$("$BIN_DIR/dsp-pad-guid" 2>&1)" || {
        echo "dsp-pad-guid found no pad; DSperate will start without controller input"
        echo "$_guid_out"
        return 0
    }

    _guid="$(echo "$_guid_out" | sed -n '1p')"
    _name="$(echo "$_guid_out" | sed -n '2p')"
    [ -n "$_guid" ] || return 0
    [ -n "$_name" ] || _name="Controller"

    export SDL_GAMECONTROLLERCONFIG="$_guid,$_name,$PROFILE_MAP,platform:Linux"
    echo "pad mapping: $_guid ($_name)"
}

# Seed once per profile so remapping done in DSperate's pause menu survives later
# launches. The runtime paths cannot live in the shipped template because
# SDCARD_PATH and friends are only known here.
dsp_seed_config() {
    _template="$PAK_DIR/configs/$PROFILE_INI.ini"

    if [ ! -f "$_template" ]; then
        echo "No config template named $PROFILE_INI, leaving the existing config alone"
        return 0
    fi

    if [ -f "$DSP_DEVICE_FILE" ] && [ "$(cat "$DSP_DEVICE_FILE")" = "$PROFILE_INI" ]; then
        return 0
    fi

    echo "Seeding DSperate config from the $PROFILE_INI profile"

    if [ -f "$DSP_CONFIG" ]; then
        cp -f "$DSP_CONFIG" "$DSP_USERDATA_DIR/dsperate.ini.bak"
    fi

    sed \
        -e "s|__BIOS_PATH__|$DSP_BIOS_DIR|g" \
        -e "s|__ROMS_PATH__|$DSP_ROMS_DIR|g" \
        -e "s|__SAVES_PATH__|$DSP_SAVES_DIR|g" \
        -e "s|__STATES_PATH__|$DSP_STATES_DIR|g" \
        -e "s|__SCREENSHOTS_PATH__|$DSP_SCREENSHOTS_DIR|g" \
        -e "s|__CHEATS_PATH__|$DSP_CHEATS_DIR/usrcheat.dat|g" \
        "$_template" >"$DSP_CONFIG"

    echo "$PROFILE_INI" >"$DSP_DEVICE_FILE"
    sync
}

# The launcher asks for a state through /tmp/resume_slot.txt, and the file is
# consumed. Its slots are minarch's: 0-7 are the player's, 8 means "plain
# launch, start fresh", 9 is the state written before a sleep -- which here is
# DSperate's own hidden auto slot, so autoload already resumes it.
#
# This matters because the shipped config has autoload on: without translating
# the marker, picking a game fresh from the launcher would resume it anyway.
dsp_resume_args() {
    DSP_RESUME_ARGS=""
    [ -f /tmp/resume_slot.txt ] || return 0

    _slot="$(cat /tmp/resume_slot.txt 2>/dev/null | tr -dc '0-9')"
    rm -f /tmp/resume_slot.txt

    case "$_slot" in
    "" | 9)
        # Let autoload pick up the auto slot.
        ;;
    8)
        DSP_RESUME_ARGS="--no-autoload"
        ;;
    *)
        _stem="$(basename "$ROM_PATH")"
        _stem="${_stem%.*}"
        DSP_RESUME_ARGS="--load-state $DSP_STATES_DIR/$_stem.$_slot.dss"
        ;;
    esac
}

cleanup() {
    rm -f /tmp/stay_awake

    # DSperate writes the battery save on SIGTERM, so -9 straight away loses the
    # last save. Give it a moment before insisting.
    killall -q -15 dsperate 2>/dev/null || true
    sleep 2
    killall -q -9 dsperate 2>/dev/null || true

    sync
}

main() {
    set -x

    rm -f "$LOGS_PATH/$PAK_NAME.txt"
    exec >>"$LOGS_PATH/$PAK_NAME.txt"
    exec 2>&1

    echo "$0" "$@"

    dsp_normalize_platform

    if ! platform_in_list "$PLATFORM" "$SUPPORTED_PLATFORMS"; then
        echo "$PLATFORM is not a supported platform"
        return 1
    fi

    # Before dsp_init_env, which derives the launcher's file keys from it.
    # Some builds hand the pak a /media/SDCARD0 path for the same card, and a
    # symlinked Roms folder is common, so resolve both: the MinUI artifacts are
    # keyed on this and the launcher will not match a path it did not write.
    ROM_PATH="$(echo "$1" | sed "s|/media/SDCARD0/|$SDCARD_PATH/|g")"
    if [ -e "$ROM_PATH" ]; then
        ROM_PATH="$(readlink -f "$ROM_PATH")"
    fi

    # shellcheck source=/dev/null
    . "$PAK_DIR/platform.sh"
    dsp_platform_profile "$PLATFORM" "${DEVICE:-}"

    dsp_init_env

    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT

    mkdir -p "$DSP_USERDATA_DIR" "$DSP_CONFIG_DIR" "$DSP_SAVES_DIR" \
        "$DSP_STATES_DIR" "$DSP_SCREENSHOTS_DIR" "$DSP_CHEATS_DIR" "$DSP_BIOS_DIR" \
        "$MINUI_SHARED_DIR" "$MINUI_DIR"

    dsp_seed_config
    dsp_export_pad_mapping

    # Only on a panel whose rotation SDL cannot tell DSperate about; the fbdev
    # tier writes /dev/fb0 directly. Empty everywhere else.
    if [ -n "$PROFILE_ROTATE" ]; then
        export DS_ROTATE="$PROFILE_ROTATE"
    fi

    dsp_resume_args

    # No --config: the seeded file is already at the default path DSperate
    # computes from XDG_CONFIG_HOME. No layout or performance flags either --
    # flags override the ini, which would make them uneditable from the pause
    # menu.
    cd "$BIN_DIR" || return 1
    # shellcheck disable=SC2086 # DSP_RESUME_ARGS is a deliberate word list
    ./dsperate "$ROM_PATH" --fullscreen $DSP_RESUME_ARGS
}

if [ "${DSP_PAK_TEST:-}" != "1" ]; then
    main "$@"
fi
