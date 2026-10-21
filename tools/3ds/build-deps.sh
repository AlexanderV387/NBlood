#!/bin/sh
# Builds SDL2 for the 3DS into deps-3ds/ (devkitPro has no SDL2 packages
# for the 3DS; SDL supports it officially since 2.24). The patches in
# tools/3ds/patches are applied to SDL first: two are Crispy Doom's; the
# audio ones give the audio thread a bigger stack (the game mixes and
# emulates its music chip there) and make closing the device safe.
#
# Run from the repository root inside the devkitpro/devkitarm image (the
# source can be cloned beforehand outside it: deps-3ds/src/SDL).
set -eu

SDL_VERSION=2.30.9
PREFIX="$PWD/deps-3ds"
WORK="$PWD/deps-3ds/src"
PATCHES="$PWD/tools/3ds/patches"

# libctru's ndsp-channel.c with a guard (platform/3DS/ndsp), linked
# before libctru so that it replaces the library's copy.
mkdir -p "$PREFIX/lib" "$WORK"
"$DEVKITARM/bin/arm-none-eabi-gcc" -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
    -O2 -D__3DS__ -I"$DEVKITPRO/libctru/include" \
    -c "$PWD/platform/3DS/ndsp/ndsp-channel.c" -o "$WORK/ndsp-channel.o"
rm -f "$PREFIX/lib/libndspfix.a"
"$DEVKITARM/bin/arm-none-eabi-ar" rcs "$PREFIX/lib/libndspfix.a" "$WORK/ndsp-channel.o"

[ -f "$PREFIX/lib/libSDL2.a" ] && { echo "deps-3ds: already built"; exit 0; }

mkdir -p "$WORK"
[ -d "$WORK/SDL" ] || git clone -q --depth 1 --branch "release-$SDL_VERSION" \
    https://github.com/libsdl-org/SDL.git "$WORK/SDL"

for patch in "$PATCHES"/sdl-*.patch; do
    git -C "$WORK/SDL" apply --check "$patch" 2>/dev/null \
        && git -C "$WORK/SDL" apply "$patch" \
        || echo "$(basename "$patch"): already applied"
done

cmake -S "$WORK/SDL" -B "$WORK/SDL/build" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF
cmake --build "$WORK/SDL/build" -j"$(nproc)"
cmake --install "$WORK/SDL/build"
