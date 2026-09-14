# Technical notes

Developer-facing detail for the build pipeline. For installation and usage see [README.md](README.md).

## Why this pak builds from source

Upstream publishes prebuilt aarch64 tarballs, and neither can load on any device this pak targets.

| Asset | libc floor | Notes |
| --- | --- | --- |
| `dsperate-vX-linux-aarch64-static.tar.gz` | `GLIBC_2.38` | Only libstdc++/libgcc are static; glibc is still dynamic |
| `dsperate-vX-linux-aarch64.tar.gz` | `GLIBC_2.38` | Also needs `GLIBCXX_3.4.30`, i.e. a GCC 12 libstdc++ |

Upstream CI builds on `ubuntu-24.04-arm`, glibc 2.39. The devices are at 2.33 (TrimUI SDK) and 2.35 (H700 stock), and NextUI's own cross toolchain targets 2.28. So the pak cross-compiles a pinned upstream tag in the LoveRetro toolchain images instead.

## Toolchains

| Platform | Image | Compiler | Sysroot glibc | CPU |
| --- | --- | --- | --- | --- |
| `tg5040` | `ghcr.io/loveretro/tg5040-toolchain` | GCC 8.3.0 | 2.28 | cortex-a53 |
| `tg5050` | `ghcr.io/loveretro/tg5050-toolchain` | GCC 10.3.0 | 2.33 | cortex-a55 |
| `h700` | `ghcr.io/loveretro/h700-toolchain` | GCC 8.3.0 | 2.28 | cortex-a53 |

The h700 image is the tg5040 image plus a patched mali-fbdev SDL2 (2.28.5) under `$PREFIX_LOCAL`. That is the SDL2 NextUI installs on those devices, and it is the one the h700 build must link.

Each image ships its own CMake toolchain file at `$CROSS_ROOT/Toolchain.cmake` and exports `CMAKE_TOOLCHAIN_FILE`, so the pak writes none. One gotcha: that file sets `CMAKE_CXX_FLAGS` as a plain variable, which shadows anything passed with `-DCMAKE_CXX_FLAGS`, so `-I$CROSS_ROOT/include/` has to be carried forward by hand in every configure line.

Do not use upstream's `cmake/aarch64-linux-gnu.cmake` or the `aarch64-cross` preset: both hardcode the Debian `aarch64-linux-gnu-` prefix and `/usr/aarch64-linux-gnu`, and the preset's single shared `build/aarch64` directory would have the three platforms clobber each other.

## Measured ABI

`scripts/check-abi.sh` runs after `--strip-unneeded`, on the exact bytes that ship. Measured at v1.15.1:

| Platform | Ceiling | Measured | NEEDED |
| --- | --- | --- | --- |
| `tg5040` | 2.28 | `GLIBC_2.18` | libSDL2, libdl, libpthread, libm, libc |
| `tg5050` | 2.33 | `GLIBC_2.33` | libSDL2, libdl, libpthread, libm, libc |
| `h700` | 2.28 | `GLIBC_2.18` | libSDL2, libdl, libpthread, libm, libc |

The ceiling is per platform because the toolchains genuinely differ, and each ceiling is its toolchain's own floor rather than the device's. That way a build reaching outside its sysroot fails loudly even though the devices would still have tolerated it.

`libpthread` in that list is the point of the `CMAKE_CXX_STANDARD_LIBRARIES` trick below — its absence would be the bug.

## Three silent failures the build asserts on

Each produces a binary that builds, links, boots and plays, just wrongly.

**JIT or NEON silently downgraded.** Upstream disables both with a plain `set(DSPERATE_JIT OFF)`, not `set(... CACHE ... FORCE)`, so `CMakeCache.txt` still reads `ON` afterwards and the NEON path prints no message at all. `scripts/assert-build.sh` therefore checks the generated `build.ninja` for the `-DDSPERATE_JIT=1` / `-DDSPERATE_NEON=1` compile definitions, which `src/core/CMakeLists.txt` emits only when the feature really survived. An interpreter-only build is far too slow to play and nothing in a device log would say why.

**The SDL frontend dropped.** If pkg-config cannot find sdl2, upstream prints `DSperate: SDL2 not found; the SDL frontend is not built` and carries on to a successful build with no `dsperate` target. Building `--target dsperate` rather than `all` turns that into a hard ninja error for free.

**The wrong SDL2 linked on h700.** `src/frontend/sdl/CMakeLists.txt` links `${SDL2_LIBRARIES}` and never references `${SDL2_LIBRARY_DIRS}`, and there is no `link_directories()` anywhere. `pkg_check_modules` puts bare names in `SDL2_LIBRARIES`, so the `-L` is discarded and `-lSDL2` resolves through the default search path into the sysroot. Fixing `PKG_CONFIG_PATH` alone would give patched headers over an unpatched library, and it would link without complaint. `scripts/docker-env.sh` therefore exports both a narrowed `PKG_CONFIG_PATH` and a `DSP_SDL_LDFLAGS` carrying the `-L`, and `assert-build.sh` greps the recorded link command to confirm it took. A wrong-SDL2 link is invisible to `readelf`, since the SONAME is the same either way.

## pthread

DSperate uses `std::thread` but never links `Threads::Threads`. Upstream never notices because glibc 2.34 folded libpthread into libc; below that it is a real library.

It is passed as `CMAKE_CXX_STANDARD_LIBRARIES`, **not** `CMAKE_EXE_LINKER_FLAGS`. Linker flags land ahead of the objects that reference `pthread_create`, where `--as-needed` drops the library again; standard libraries are appended at the very end of the link line, the one place it survives. `--push-state`/`--pop-state` keeps `--no-as-needed` from applying to everything else on the line. `tests/makefile.bats` asserts this, because simplifying it into the linker flags regresses silently and only on glibc < 2.34.

## Patches

`patches/` is applied to every build by the `clone` rule. A patch that no longer applies is fatal, so a drifted patch cannot produce a green build with the workaround silently missing.

Both current patches are **GCC 8 compiler bugs, not upstream bugs**. Drop them when the tg5040 and h700 toolchains move past GCC 8, not before.

**`0001-gcc8-constexpr-in-nested-lambda.patch`** — GCC 8 treats a `constexpr` local of an enclosing lambda, read in an `if constexpr` inside a nested lambda, as a capture, and rejects it: `lambda capture of 'NH' is not a constant expression`, 97 times in `render3d.cpp`. Fixed in GCC 9. Naming the type directly is the same value with no odr-use. Reported upstream as [beebono/DSperate#1](https://github.com/beebono/DSperate/issues/1).

**`0002-gcc8-neon-scale-row-grid-miscompile.patch`** — GCC 8 miscompiles the NEON `scale_row_grid` kernel. Bisected with `-mcpu` held constant so the compiler was the only variable:

| Compiler | `-mcpu` | `gpu` / `kernels` |
| --- | --- | --- |
| GCC 8.3.0 | cortex-a53 | FAIL |
| GCC 10.3.0 | cortex-a53 | pass |
| GCC 10.3.0 | cortex-a55 | pass |

Not an optimiser level: it reproduces at `-O3`, `-O2` and `-O1`, with and without LTO, and with `-fno-strict-aliasing`. The patch falls back to the portable reference on GCC 8 only; GCC 9 and later keep the NEON path, so tg5050 is unaffected. It costs performance only in the LCD-grid and chunky filters, which are off in the shipped default configuration.

This is exactly what `ctest` is in the pipeline to catch, and it caught it on the first run.

## Tests

`make test-native` builds the upstream suite in its own `build/ctest` directory with `-DDSPERATE_TESTS=ON`, so the three shipping builds keep `-DDSPERATE_TESTS=OFF` and never carry test binaries.

It runs once, with the tg5040 flag set. All three platforms build identical source with identical options and only `-mcpu`/`-mtune` differ, which changes scheduling and not semantics; using the cortex-a53 build keeps the tested binary a strict ARMv8-A baseline. CI runs on `ubuntu-24.04-arm`, so the container is native arm64 and the cross-built binaries execute directly — no `qemu-aarch64-static`.

`input` is excluded. It links SDL2, the only libSDL2 in the container is the sysroot's, and putting that on `LD_LIBRARY_PATH` drags its glibc in front of the container's loader: `undefined symbol: __libc_vfork, version GLIBC_PRIVATE`. Nothing to do with the code. `RUN_CTEST=0` skips the suite entirely, `CTEST_EXCLUDE` adjusts the filter.

`make test` is the host-side bats suite, which needs no toolchain: `abi.bats` drives `check-abi.sh` through a stubbed `readelf`, `makefile.bats` reads the build wiring through `print-%`, and `platform.bats`/`launch.bats` source the launcher with `DSP_PAK_TEST=1`.

## PGO is off and cannot be otherwise

`pgo/aarch64/MANIFEST` records `compiler 13.3.0 (aarch64-linux-gnu)` and a fingerprint over the compiler and flags, and upstream's configure calls `message(FATAL_ERROR)` on a mismatch. Our compilers differ and each platform adds a different `-mcpu`, so `use` could not match for more than one of them even in principle. `DSPERATE_PGO_DIR` also defaults inside the cloned tree that `make clean` deletes, and the hottest code on the device is JIT output that ahead-of-time profiling cannot touch. `DSPERATE_PGO` stays an overridable variable so the experiment is one flag away.

## Bumping the upstream version

Dependabot only covers Actions; `DSPERATE_TAG` is manual, and upstream ships tags often.

1. Set `DSPERATE_TAG` in the `Makefile`. `tests/makefile.bats` asserts it is a tag rather than a branch, and `make verify-pin` asserts the checkout matches.
2. Read the upstream release notes for CMake option changes. A renamed switch fails open, because CMake ignores unknown `-D` cache args, and `assert-build.sh` is the only thing that would catch the consequence.
3. `make clean build`. Both patches must still apply; if one does not, check whether upstream fixed it before rebasing.
4. Re-measure the ABI table above if the toolchain images have moved.

## Known rough edges

- **rg28xx rotation is unverified.** Its panel is mounted portrait and NextUI exports `SDL_ROTATION=1` so applications see 640x480 landscape, but DSperate's fbdev tier writes `/dev/fb0` directly and never goes through SDL, so that rotation does not reach it. `config/platform.sh` sets `DS_ROTATE=270` for that device on that reasoning; it needs confirming on hardware.
- **The display tier on TrimUI is unverified.** The A133P is Allwinner, so `/dev/disp` may answer and the display-engine tier — written and measured for the Miyoo A30 — could open first. `--no-disp` is the escape hatch and the `video:` log line says which tier actually opened.
- **`:latest` toolchain images.** House convention, but a green build today can be red tomorrow with no repo change, and a toolchain glibc bump would trip the ABI gate. The gate firing is the desired behaviour; the per-platform `*_GLIBC_MAX` variables are the knob.
- **Root-owned build output.** Everything under `src/`, `build/` and `dist/` is written as root by the containers, so `make clean` from an unprivileged account can fail. `make clean-docker` does the delete from inside a container.
