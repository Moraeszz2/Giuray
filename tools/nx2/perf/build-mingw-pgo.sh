#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Local copy of the release workflow's windows-x86_64-pgo build (MSYS2 UCRT64 clang with
# eglantine.profdata), for A/B packages on a desktop. Run from an MSYS2 UCRT64 shell:
#   tools/nx2/perf/build-mingw-pgo.sh <build dir> <package dir> [build tag]
# The package dir gets lichii.exe, eden-cli.exe, their DLLs and a CodeView lichii.pdb, which the
# in-process sampler (EDEN_NX2_SAMPLE, the debug server's `sample`) needs to name functions.
# The PDB comes from -g1 line tables only: debug info does not change clang's code generation.
set -euo pipefail

src=$(cd "$(dirname "$0")/../../.." && pwd)
build=${1:?build dir}
package=${2:?package dir}
tag=${3:-local-$(git -C "$src" rev-parse --short HEAD)}
export PATH="/ucrt64/bin:$PATH"

profile="-fprofile-use=$(cygpath -m "$src/eglantine.profdata") -Wno-backend-plugin \
  -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
symbols="-g1 -gcodeview"
# The bundled MinGW FFmpeg has no AV1 decoder (externals/ffmpeg uses the AV1 package for MSVC and
# Linux only): Ocarina of Time's title background stays black and its AV1 movies hang on a white
# screen. FFMPEG=system (default here) links MSYS2's FFmpeg, which has libdav1d
# (pacman -S mingw-w64-ucrt-x86_64-ffmpeg); FFMPEG=bundled matches the release workflow.
case "${FFMPEG:-system}" in
system) bundled_ffmpeg=OFF ;;
bundled) bundled_ffmpeg=ON ;;
*) echo "FFMPEG must be system or bundled" >&2; exit 1 ;;
esac

# Configured every time: the build tag and the git revision in the version string come from the
# configure step, and a package must name the commit it was built from.
cmake -S "$src" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DYUZU_TESTS=OFF \
        -DYUZU_CMD=ON \
        -DYUZU_ROOM=OFF \
        -DYUZU_ROOM_STANDALONE=OFF \
        -DYUZU_USE_BUNDLED_QT=OFF \
        -DYUZU_USE_BUNDLED_SDL3=ON \
        -DYUZU_USE_BUNDLED_FFMPEG=$bundled_ffmpeg \
        -DVulkanHeaders_FORCE_BUNDLED=ON \
        -DVulkanUtilityLibraries_FORCE_BUNDLED=ON \
        -DENABLE_UPDATE_CHECKER=OFF \
        -DLICHII_BUILD_TAG="$tag" \
        -DLICHII_BUILD_PLATFORM=windows-x86_64-pgo \
        -DUSE_DISCORD_PRESENCE=OFF \
        -DUSE_CCACHE=ON \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,--pdb=" \
        "-DCMAKE_C_FLAGS=$profile $symbols" \
        "-DCMAKE_CXX_FLAGS=$profile $symbols"

cmake --build "$build" --target yuzu yuzu-cmd --parallel "${JOBS:-$(nproc)}"

rm -rf "$package"
mkdir -p "$package"
cp "$build"/bin/lichii.exe "$build"/bin/eden-cli.exe "$package"/
cp "$build"/bin/*.pdb "$package"/ 2>/dev/null || true
# DLLs the build puts next to the executables (nxformat), which the PATH lookup below misses.
find "$build"/bin -maxdepth 1 -name '*.dll' ! -name 'Qt6*' -exec cp {} "$package"/ \;
cd "$package"
windeployqt6 --no-translations --no-compiler-runtime --no-opengl-sw \
    --no-system-dxc-compiler --no-system-d3d-compiler lichii.exe > /dev/null
deps() {
    objdump -p "$1" | awk '/DLL Name:/ {print $3}' | while read -r dll; do
        [ -z "$dll" ] && continue
        source=$(command -v "$dll" 2>/dev/null || true)
        [ -z "$source" ] && continue
        case "$source" in
        *System32* | *SysWOW64* | *system32*) continue ;;
        esac
        # MSYS2 FFmpeg's dependencies import the Vulkan loader: the driver's own must load.
        case "$dll" in
        vulkan-1.dll) continue ;;
        esac
        if [ ! -f "$dll" ]; then
            cp "$source" "$dll"
            deps "$source"
        fi
    done
}
for exe in lichii.exe eden-cli.exe; do
    deps "$exe"
done
find . -name '*.dll' | while read -r dll; do
    deps "$dll"
done
echo "Package: $package ($tag)"
