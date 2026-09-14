# minui-nintendo-dsperate-pak

A MinUI Emu Pak for Nintendo DS, wrapping the standalone [DSperate](https://github.com/beebono/DSperate) emulator (version 1.15.1).

DSperate reimplements DraStic's JIT and NEON rendering techniques with an ARM to AArch64 recompiler, a threaded software rasteriser, save states, Action Replay cheats and RetroAchievements. Unlike DraStic it needs no BIOS dump to play games.

> [!NOTE]
> This pak installs alongside [minui-nintendo-ds-pak](https://github.com/josegonzalez/minui-nintendo-ds-pak), which wraps DraStic. They use separate rom folders, so both can be installed at once.

## Requirements

This pak is designed and tested on the following MinUI Platforms and devices:

- `tg5040`: Trimui Brick (formerly `tg3040`), Trimui Brick Pro and Trimui Smart Pro
- `tg5050`: Trimui Smart Pro S
- `h700`: Anbernic RG28XX, RG34XX, RG34XX SP, RG35XX Plus, RG35XX 2024, RG35XX H, RG35XX Pro, RG35XX SP, RG40XX H, RG40XX V, RG Cube XX and RG SP

Use the correct platform for your device. The `h700` and `tg5050` platforms are provided by NextUI, so those devices need NextUI rather than stock MinUI.

## Installation

1. Mount your MinUI SD card.
2. Download the latest release from Github. It will be named `DSP.pak.zip`.
3. Copy the zip file to `/Emus/$PLATFORM/DSP.pak.zip`.
4. Extract the zip in place, then delete the zip file.
5. Confirm that there is a `/Emus/$PLATFORM/DSP.pak/launch.sh` file on your SD card.
6. Create a folder at `/Roms/Nintendo DS (DSP)` and place your roms in this directory.
7. Unmount your SD Card and insert it into your MinUI device.

Both `.nds` and `.zip` roms work. DSperate reads zips itself, detected by content rather than extension, so there is no need to extract them. `.7z` is not supported.

## BIOS

No BIOS is required. Without dumps, DSperate boots games with a built-in replacement BIOS (FreeBIOS) and a generated firmware.

Supplying your own dumps unlocks the DS firmware menu and cycle-exact SWI timing. If you have them, place them at:

- `/Bios/DSP/bios9.bin`
- `/Bios/DSP/bios7.bin`
- `/Bios/DSP/firmware.bin`

None are included here, and none can be.

## Device Configuration

DSperate is configured through an INI file. On the first launch the pak picks the profile matching your device, writes it to `/.userdata/$PLATFORM/DSP-dsperate/dsperate/dsperate.ini`, and records the choice in `device.txt` beside it. After that the file is yours: anything you change there, or in DSperate's own pause menu, is kept on later launches.

The profiles differ only in how the stylus is driven, which depends on how many analog sticks the device has:

| Profile | Stylus | Devices |
| --- | --- | --- |
| `two-sticks` | Right stick moves the pen | Smart Pro, Smart Pro S, Brick Pro, RG35XX H, RG35XX Pro, RG40XX H, RG Cube XX, RG34XX SP |
| `one-stick` | Left stick moves the pen | RG40XX V |
| `no-sticks` | Hold R2 and use the d-pad | Brick, RG28XX, RG34XX, RG35XX Plus, RG35XX 2024, RG35XX SP, RG SP |

Delete `device.txt` to have the profile applied again. Your previous config is kept as `dsperate.ini.bak`.

`configs/reference.ini` in the pak is upstream's fully commented defaults, listing every key and what it does. It is documentation only and is never applied.

## Key Controls

MENU acts as the modifier for every chord.

- MENU + A: Quit
- MENU + Y: Pause menu (save/load state, cheats, options, achievements)
- MENU + Up / Down: Cycle screen layout
- MENU + B: Screenshot
- MENU + X: Toggle the FPS counter
- MENU + L2: Swap which screen is the large one
- MENU + R2: Toggle fast forward
- R2 (two-stick and one-stick devices): Hold for fast forward
- L2: Tap the stylus at the pen's position
- R2 (no-stick devices): Hold to move the pen with the d-pad

## Saves & States

- Game saves are stored in `/Saves/DSP/`.
- Save states are stored in `/.userdata/shared/DSP-dsperate/`.
- Screenshots are written to `/Screenshots/`.
- Cheats are read from `/Cheats/DSP/usrcheat.dat` if present.

The pak turns on DSperate's autosave and autoload, so quitting through the launcher writes a state and the next launch resumes from it.

## Deep Sleep & Shutdown

Deep sleep is supported on `tg5040` and `tg5050`. Click the power button to enter deep sleep, click again to resume. To shut down, hold the power button for 2 seconds. For more information, see [MinUI Power Control](https://github.com/ben16w/minui-power-control).

MinUI Power Control does not support `h700`, so the power button keeps its default behaviour on those devices.

## Debug Logging

Debug logs are written to the `/.userdata/$PLATFORM/logs/` folder, as `DSP.txt`.

Three lines in that log are worth knowing:

- `controller: <name>` confirms the pad mapping was accepted. DSperate has no joystick fallback, so if this line is missing there will be no input at all.
- `video: ...` names the display path that opened. Expect `fbdev scanout` on `h700`. Anything mentioning the SDL renderer means it fell back and will be slow.
- The recompiler banner. An interpreter note instead means the JIT is off, and the emulator will be far too slow.

## Development

- `make build` cross-compiles DSperate for all three platforms and runs the upstream unit tests.
- `make test` runs the [bats](https://github.com/bats-core/bats-core) suite in `tests/`.
- `make lint` runs shellcheck and shfmt.
- `make release` produces `dist/DSP.pak.zip`.

See [TECHNICAL.md](TECHNICAL.md) for the build pipeline, the ABI floors, the toolchain workarounds and how to bump the upstream version.

## Credits

- @beebono for DSperate
- anyone else I'm missing

## License

This pak is MIT licensed. The bundled DSperate binaries are GPLv3 — see `LICENSE.DSperate` in the release, and `dsperate.build-info` for the exact upstream commit and build flags they were produced from.
