#!/usr/bin/env bats

load test_helper

setup() {
    setup_launch tg5040 brick
    dsp_platform_profile "$PLATFORM" "$DEVICE"
    dsp_init_env
    mkdir -p "$DSP_USERDATA_DIR" "$DSP_CONFIG_DIR"
}

@test "tg3040 with no device is normalized to a tg5040 Brick" {
    PLATFORM="tg3040"
    DEVICE=""
    dsp_normalize_platform
    [ "$PLATFORM" = "tg5040" ]
    [ "$DEVICE" = "brick" ]
}

@test "tg3040 with a device set is left alone" {
    PLATFORM="tg3040"
    DEVICE="smartpro"
    dsp_normalize_platform
    [ "$PLATFORM" = "tg3040" ]
}

@test "platform_in_list matches whole words only" {
    run platform_in_list tg5040 "tg5040 tg5050 h700"
    [ "$status" -eq 0 ]
    run platform_in_list h700 "tg5040 tg5050 h700"
    [ "$status" -eq 0 ]
    # The substring trap the helper exists to avoid.
    run platform_in_list tg50 "tg5040 tg5050 h700"
    [ "$status" -ne 0 ]
    run platform_in_list rg35xxplus "tg5040 tg5050 h700"
    [ "$status" -ne 0 ]
}

@test "the config is seeded from the profile the device maps to" {
    dsp_seed_config
    [ -f "$DSP_CONFIG" ]
    [ "$(cat "$DSP_DEVICE_FILE")" = "no-sticks" ]
    grep -q "stylus_dpad" "$DSP_CONFIG"
}

@test "seeding substitutes every path token" {
    dsp_seed_config
    run grep -c "__" "$DSP_CONFIG"
    [ "$output" = "0" ]
    grep -q "saves = $SDCARD_PATH/Saves/DSP" "$DSP_CONFIG"
    grep -q "states = $SHARED_USERDATA_PATH/DSP-dsperate" "$DSP_CONFIG"
    grep -q "bios9 = $BIOS_PATH/DSP/bios9.bin" "$DSP_CONFIG"
    grep -q "cheats = $SDCARD_PATH/Cheats/DSP/usrcheat.dat" "$DSP_CONFIG"
    grep -q "games = $SDCARD_PATH/Roms/Nintendo DS (DSP)" "$DSP_CONFIG"
}

@test "seeding is idempotent and does not clobber user edits" {
    dsp_seed_config
    echo "# edited by the user" >>"$DSP_CONFIG"
    dsp_seed_config
    grep -q "edited by the user" "$DSP_CONFIG"
}

@test "a changed profile re-seeds and keeps a backup" {
    dsp_seed_config
    echo "# original" >>"$DSP_CONFIG"
    # Pretend the card moved to a device with two sticks.
    dsp_platform_profile tg5040 smartpro
    dsp_seed_config
    [ "$(cat "$DSP_DEVICE_FILE")" = "two-sticks" ]
    grep -q "original" "$DSP_USERDATA_DIR/dsperate.ini.bak"
    ! grep -q "original" "$DSP_CONFIG"
}

@test "deleting the marker re-seeds" {
    dsp_seed_config
    echo "# stale" >>"$DSP_CONFIG"
    rm -f "$DSP_DEVICE_FILE"
    dsp_seed_config
    ! grep -q "stale" "$DSP_CONFIG"
}

@test "a missing template leaves the config alone" {
    PROFILE_INI="does-not-exist"
    run dsp_seed_config
    [ "$status" -eq 0 ]
    [ ! -f "$DSP_CONFIG" ]
}

@test "the pad mapping is assembled from the device GUID and the profile body" {
    dsp_export_pad_mapping
    [ -n "$SDL_GAMECONTROLLERCONFIG" ]
    # guid,name,body,platform:Linux
    case "$SDL_GAMECONTROLLERCONFIG" in
    "030000004c050000c405000011010000,Fake Pad,"*",platform:Linux") ;;
    *)
        echo "unexpected: $SDL_GAMECONTROLLERCONFIG"
        return 1
        ;;
    esac
    [ "${SDL_GAMECONTROLLERCONFIG#*guide:b}" != "$SDL_GAMECONTROLLERCONFIG" ]
}

@test "no pad means no mapping rather than a broken one" {
    printf '#!/bin/sh\nexit 1\n' >"$PAK_DIR/$PLATFORM/dsp-pad-guid"
    chmod +x "$PAK_DIR/$PLATFORM/dsp-pad-guid"
    unset SDL_GAMECONTROLLERCONFIG
    run dsp_export_pad_mapping
    [ "$status" -eq 0 ]
    [ -z "${SDL_GAMECONTROLLERCONFIG:-}" ]
}

@test "an unsupported platform yields no mapping" {
    dsp_platform_profile miyoomini ""
    unset SDL_GAMECONTROLLERCONFIG
    run dsp_export_pad_mapping
    [ "$status" -eq 0 ]
    [ -z "${SDL_GAMECONTROLLERCONFIG:-}" ]
}

@test "bundled binaries come first on LD_LIBRARY_PATH, then NextUI, then the system" {
    case "$LD_LIBRARY_PATH" in
    "$PAK_DIR/$PLATFORM:$SDCARD_PATH/.system/$PLATFORM/lib:/usr/trimui/lib:"*) ;;
    *)
        echo "unexpected: $LD_LIBRARY_PATH"
        return 1
        ;;
    esac
}

@test "h700 gets no TrimUI library directory" {
    setup_launch h700 rg35xxplus
    # setup() already ran dsp_init_env for tg5040, and dsp_init_env appends the
    # inherited value, so the earlier /usr/trimui/lib would ride along. A real
    # launch inherits MinUI's environment, not a previous run's.
    unset LD_LIBRARY_PATH
    dsp_platform_profile h700 rg35xxplus
    dsp_init_env
    [ "${LD_LIBRARY_PATH#*trimui}" = "$LD_LIBRARY_PATH" ]
    case "$LD_LIBRARY_PATH" in
    "$PAK_DIR/h700:$SDCARD_PATH/.system/h700/lib:"*) ;;
    *)
        echo "unexpected: $LD_LIBRARY_PATH"
        return 1
        ;;
    esac
}

@test "DSperate is pointed at its config through XDG_CONFIG_HOME" {
    # Config::dir() appends /dsperate to XDG_CONFIG_HOME, so the seeded file has
    # to land at that exact path or DSperate silently writes its own defaults.
    [ "$XDG_CONFIG_HOME" = "$DSP_USERDATA_DIR" ]
    [ "$DSP_CONFIG" = "$DSP_USERDATA_DIR/dsperate/dsperate.ini" ]
}
