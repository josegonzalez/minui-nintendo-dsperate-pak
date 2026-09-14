#!/usr/bin/env bats

load test_helper

@test "the pak metadata drives the artifact and install paths" {
    mk PAK_NAME
    [ "$output" = "DSP" ]
    mk PAK_FOLDER
    [ "$output" = "Emus" ]
}

@test "the Makefile, pak.json and launch.sh agree on the platform list" {
    mk PLATFORMS
    make_platforms="$output"
    [ "$make_platforms" = "$(pak_platforms)" ]

    # SUPPORTED_PLATFORMS is what launch.sh actually gates on.
    launch_platforms="$(sed -n 's/^SUPPORTED_PLATFORMS="\(.*\)"$/\1/p' "$REPO_ROOT/launch.sh")"
    [ "$launch_platforms" = "$make_platforms" ]
}

@test "every platform has an image, CPU flags, a docker runner and a glibc ceiling" {
    for p in $(pak_platforms); do
        u="$(echo "$p" | tr '[:lower:]' '[:upper:]')"
        mk "${u}_IMAGE"
        [ -n "$output" ] || return 1
        mk "${u}_CPUFLAGS"
        [ -n "$output" ] || return 1
        mk "${u}_GLIBC_MAX"
        [ -n "$output" ] || return 1
        mk "DOCKER_RUN_${p}"
        [ -n "$output" ] || return 1
    done
}

@test "every platform has build, stage and dist targets" {
    for p in $(pak_platforms); do
        grep -q "^build-$p:" "$REPO_ROOT/Makefile" || return 1
        grep -q "^stage-$p:" "$REPO_ROOT/Makefile" || return 1
        grep -q "^dist-$p:" "$REPO_ROOT/Makefile" || return 1
    done
}

@test "build and dist cover every platform" {
    for p in $(pak_platforms); do
        grep -q "MAKE) stage-$p\$" "$REPO_ROOT/Makefile" || return 1
        grep -q "MAKE) dist-$p\$" "$REPO_ROOT/Makefile" || return 1
    done
}

@test "the toolchains come from the LoveRetro registry" {
    for p in $(pak_platforms); do
        u="$(echo "$p" | tr '[:lower:]' '[:upper:]')"
        mk "${u}_IMAGE"
        case "$output" in
        ghcr.io/loveretro/*-toolchain:*) ;;
        *) return 1 ;;
        esac
    done
}

@test "h700 builds for the Cortex-A53 like tg5040, not the A55" {
    mk H700_CPUFLAGS
    h700="$output"
    mk TG5040_CPUFLAGS
    [ "$h700" = "$output" ]
    mk TG5050_CPUFLAGS
    [ "$h700" != "$output" ]
}

@test "the upstream pin is a tag, not a branch" {
    mk DSPERATE_TAG
    [[ "$output" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

@test "the shipping build never compiles the Wayland tier" {
    mk DSP_CMAKE_COMMON
    [[ "$output" == *"-DDSPERATE_WAYLAND=OFF"* ]]
}

@test "the shipping build requires the recompiler and the NEON kernels" {
    mk DSP_CMAKE_COMMON
    [[ "$output" == *"-DDSPERATE_JIT=ON"* ]]
    [[ "$output" == *"-DDSPERATE_NEON=ON"* ]]
    # Asking is not enough: upstream downgrades both with a plain set(), so the
    # build graph is checked after the fact.
    [ -x "$REPO_ROOT/scripts/assert-build.sh" ]
    grep -q -- '-DDSPERATE_JIT=1' "$REPO_ROOT/scripts/assert-build.sh"
    grep -q -- '-DDSPERATE_NEON=1' "$REPO_ROOT/scripts/assert-build.sh"
}

@test "assert-build reads the build graph and not the CMake cache" {
    # CMakeCache.txt still reads ON after upstream's plain set() downgrade, so
    # checking it would pass on an interpreter-only build.
    grep -q "build.ninja" "$REPO_ROOT/scripts/assert-build.sh"
    # Comments name CMakeCache to explain why it is not used, so only code counts.
    ! grep -v '^[[:space:]]*#' "$REPO_ROOT/scripts/assert-build.sh" | grep -q "CMakeCache"
}

@test "test binaries never reach a shipping build" {
    mk DSP_CMAKE_COMMON
    [[ "$output" == *"-DDSPERATE_TESTS=OFF"* ]]
    # ctest builds somewhere no dist target reads from.
    grep -q "build/ctest" "$REPO_ROOT/Makefile"
    ! grep -q "DSP_DIST.*ctest" "$REPO_ROOT/Makefile"
}

@test "the headless harness is neither built nor shipped" {
    mk DSP_CMAKE_COMMON
    [[ "$output" == *"-DDSPERATE_HEADLESS=OFF"* ]]
    # The comment explaining the choice names it, so only recipe lines count.
    ! grep -v '^[[:space:]]*@\?#' "$REPO_ROOT/Makefile" | grep -q "dsperate-headless"
}

@test "PGO is off" {
    # pgo/aarch64/MANIFEST fingerprints GCC 13.3.0 and upstream's configure is a
    # FATAL_ERROR on a mismatch, so `use` cannot configure with our toolchains.
    mk DSPERATE_PGO
    [ "$output" = "OFF" ]
}

@test "no two platforms share a CMake build directory" {
    # The build dir is derived from $(1) in the recipe macros, so each platform
    # gets its own. Upstream's presets all point at one build/aarch64.
    grep -q -- '-B /build/build/\$(1)' "$REPO_ROOT/Makefile"
}

@test "libstdc++ and libgcc are linked statically" {
    grep -q -- "-static-libstdc++" "$REPO_ROOT/Makefile"
    grep -q -- "-static-libgcc" "$REPO_ROOT/Makefile"
}

@test "pthread is forced onto the end of the link line" {
    # CMAKE_EXE_LINKER_FLAGS lands ahead of the objects that reference
    # pthread_create, where --as-needed drops it again. Only
    # CMAKE_CXX_STANDARD_LIBRARIES is appended last. Simplifying this into the
    # linker flags regresses silently, and only on glibc < 2.34.
    mk DSP_PTHREAD_LIBS
    [[ "$output" == *"--no-as-needed"* ]]
    [[ "$output" == *"-lpthread"* ]]
    grep -q 'CMAKE_CXX_STANDARD_LIBRARIES="$(DSP_PTHREAD_LIBS)"' "$REPO_ROOT/Makefile"
    ! grep -q 'CMAKE_EXE_LINKER_FLAGS=.*lpthread' "$REPO_ROOT/Makefile"
}

@test "the ABI ceiling is asserted for every platform" {
    [ -x "$REPO_ROOT/scripts/check-abi.sh" ]
    grep -q "check-abi.sh \$(2)" "$REPO_ROOT/Makefile"
    for p in $(pak_platforms); do
        u="$(echo "$p" | tr '[:lower:]' '[:upper:]')"
        grep -q "stage-$p:.*${u}_GLIBC_MAX" "$REPO_ROOT/Makefile" || return 1
    done
}

@test "the GCC 8 platforms hold a tighter ceiling than the GCC 10 one" {
    mk TG5040_GLIBC_MAX
    [ "$output" = "2.28" ]
    mk H700_GLIBC_MAX
    [ "$output" = "2.28" ]
    # tg5050 ships a GCC 10.3 toolchain over a glibc 2.33 sysroot.
    mk TG5050_GLIBC_MAX
    [ "$output" = "2.33" ]
}

@test "every patch in patches/ is applied by the clone rule" {
    grep -q 'for p in $(PATCHES)/\*.patch' "$REPO_ROOT/Makefile"
    # A drifted patch must be fatal, not silently skipped.
    grep -q 'git apply "$$p"' "$REPO_ROOT/Makefile"
}

@test "the GCC 8 workarounds are still present" {
    # Both are compiler bugs, not upstream ones: drop them when the toolchain
    # moves past GCC 8, not before.
    [ -f "$REPO_ROOT/patches/0001-gcc8-constexpr-in-nested-lambda.patch" ]
    [ -f "$REPO_ROOT/patches/0002-gcc8-neon-scale-row-grid-miscompile.patch" ]
}

@test "bump-version refuses to run without a version" {
    # `make release` calls it, and an unset RELEASE_VERSION would quietly rewrite
    # pak.json's version to the empty string.
    run make -C "$REPO_ROOT" --no-print-directory bump-version
    [ "$status" -ne 0 ]
    [[ "$output" == *"RELEASE_VERSION is not set"* ]]
    [ "$(jq -r .version "$REPO_ROOT/pak.json")" != "" ]
}
