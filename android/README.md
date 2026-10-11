# Android port

Status: **first port, untested on a device.** The platform layer, renderer
lifecycle, input and build are in place; nothing here has been run on hardware
yet (see *Known risks*).

The Android build is the Linux host (SDL2 + Vulkan renderer) packaged as an
APK: SDL's `SDLActivity` loads `libmain.so`, which is this project (the lifted
C, the vendored xboxrecomp runtime, `src/android_host.c`).

## What you need

| | |
|---|---|
| Device | arm64, Android 10+ (API 29), **Vulkan 1.3** with `VK_KHR_push_descriptor` and `VK_EXT_vertex_input_dynamic_state` (recent Adreno 6xx/7xx, Mali-G7x with current drivers). 6 GB RAM or more. |
| Build host | Android Studio / Gradle 8.9 (wrapper included), Android NDK r27+, CMake 3.22+ (SDK Manager), git, Python 3 |
| Game | your own **extracted** disc (`default.xbe`, `NFSUNDER/`, ...), not the ISO |
| Lifted C | `tools/regen.sh` output (`gen/`). It is host independent: the one you built for Linux/Switch works. |

## Build

```sh
export ANDROID_NDK_HOME=/path/to/ndk
android/tools/fetch_deps.sh                 # SDL2 sources -> android/deps/SDL
android/tools/build_glslang.sh              # glslang for arm64 -> android/deps/glslang-android
# optional, for the movies (otherwise the slow lifted VP6 decoder plays them):
tools/build_ffmpeg_vp6.sh android           # PREFIX=android/deps/ffmpeg-vp6-android

cd android
./gradlew assembleRelease -Pnfsu2.genDir=/path/to/gen
# -> app/build/outputs/apk/release/app-release.apk  (signed with the debug key)
adb install -r app/build/outputs/apk/release/app-release.apk
```

Useful CMake switches (add to `arguments` in `app/build.gradle`):
`-DNFSU2_GEN_OPT=-O1` if clang runs out of memory on the lifted chunks,
`-DNFSU2_LTO=ON` for ThinLTO, `-DNFSU2_ANDROID_TLS_MODEL=initial-exec` to try
cheaper thread-local guest registers (see below).

## Game data

The app reads and writes `Android/data/com.nfsu2x.nfsu2/files/`:

```
files/
├── game/            the extracted disc (default.xbe, NFSUNDER/, ...)
├── save/            runtime saves (NFSU2's own go to game/UDATA)
├── nfsu2x_env.txt   optional KEY=VALUE settings, same keys as the README
├── nfsu2x_log.txt   log of the last run (also in logcat, tag "nfsu2")
└── progcache.bin, vkpipecache.bin, ...   shader / pipeline caches
```

```sh
android/tools/push_game.sh /path/to/extracted/disc     # adb push, ~2.6 GB
```

Android 11+ hides `Android/data` from file managers; use adb or a PC over USB.

## Controls

* A Bluetooth/USB gamepad (anything SDL's game-controller layer knows) is
  player 1 and hides the overlay. It can be connected while the game runs.
* Otherwise the on-screen controls: floating left stick, A/B/X/Y, LT/RT,
  LB/RB (Black/White), Back/Start. Landscape only.
* Buttons map by label, like the Switch build (`RECOMP_PAD_LAYOUT=position`
  for the Xbox layout). No second player on touch.

## What changed in the code

| Where | Change |
|---|---|
| `CMakeLists.txt` | Android branch: `libmain.so`, SDL2 built in-tree, Vulkan only, 16 KB page alignment |
| `src/android_host.c` | storage paths, `nfsu2x_env.txt`, logcat + log file, touch-pad state (JNI) |
| `src/main.c` | `SDL_main`, big-stack game thread (as on Switch), unwinder-based crash report (no `execinfo.h` in bionic) |
| `xboxrecomp/.../win32_compat.c` | `sched_setaffinity` (no `pthread_setaffinity_np`), `memfd_create` syscall, `TerminateThread` without `pthread_cancel` |
| `xboxrecomp/.../nv2a_vk.c` | surface/swapchain recreation (app backgrounded), identity pre-transform, fullscreen window |
| `xboxrecomp/.../xinput_device.c` | touch pad as player 1 when no pad is attached, pad hot-plug rescan |
| `xboxrecomp/.../CMakeLists.txt` | no OpenSSL, null D3D8 layer, in-tree SDL2 on Android |

## Known risks (nothing below has been checked on a device)

* **Clang on the lifted code.** The generated C has only been built with GCC
  (Linux, devkitA64). Expect the first NDK build to show whether clang accepts
  all of it.
* **Thread-local registers.** The guest registers are `__thread`. In a shared
  library that is a call per function (TLS descriptors / `__tls_get_addr`),
  slower than on the Switch's static executable. `NFSU2_ANDROID_TLS_MODEL=initial-exec`
  is the thing to try; `PERF_NOTES.md` covers the register-in-locals work that
  already reduces this.
* **Guest address space.** Like macOS arm64 the guest cannot sit at its own
  addresses; the runtime's `g_xbox_mem_offset` path is used, plus a 2 GB
  reservation for the extended VMA. Android allows this, but a device with a
  strict `RLIMIT_AS` would fail at boot ("failed to map base view").
* **16 KB-page devices.** The guest's 4 KB page bookkeeping (`guest_vmem.c`)
  was never run on a 16 KB host.
* **Vulkan features.** `vkCreateDevice` fails on drivers without push
  descriptors or dynamic vertex input; the log says so.
* **Backgrounding.** SDL blocks its event pump while the app is paused, which
  pauses the game with it; the surface is rebuilt on return. Audio and the
  guest clock are not paused.
* **Performance.** The Switch build manages ~20 fps in races; a current
  Snapdragon core is faster than a Cortex-A57, the GPU path is untested.
