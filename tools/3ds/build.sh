#!/bin/sh
# Builds EDuke32 (Duke Nukem 3D) for the 3DS: eduke32.elf and eduke32.3dsx.
# Run inside the devkitpro/devkitarm image, from the repository root, after
# tools/3ds/build-deps.sh.
set -eu

# The build's revision comes from git: in a container whose user does not
# own the checkout (GitHub Actions), git refuses it and its error became
# the revision, which broke the build.
git config --global --add safe.directory "$PWD" 2>/dev/null || true

rm -f eduke32.elf
make -j"$(nproc)" duke3d PLATFORM=3DS RELEASE=1
smdhtool --create "Duke Nukem 3D" "EDuke32 for New 3DS" "AlexanderV387, EDuke32, NBlood" \
    platform/3DS/duke3d/icon.png eduke32.smdh
3dsxtool eduke32.elf eduke32.3dsx --smdh=eduke32.smdh
