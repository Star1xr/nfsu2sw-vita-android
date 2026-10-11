#!/usr/bin/env bash
# Fetch what the Android build needs besides the NDK:
#   android/deps/SDL   SDL2 (zlib licence): its Java glue (SDLActivity...) and
#                      the native library the Gradle build compiles in-tree.
# glslang (runtime GLSL -> SPIR-V) is built by build_glslang.sh.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDL_TAG="${SDL_TAG:-release-2.30.9}"
mkdir -p "$HERE/deps"
if [ ! -d "$HERE/deps/SDL/.git" ]; then
    git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL "$HERE/deps/SDL"
fi
echo "SDL2 $SDL_TAG in $HERE/deps/SDL"
