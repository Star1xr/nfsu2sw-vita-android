# Android port

Status: **first port, untested on a device.** The platform layer, both
renderers, input, ISO installer and build are in place; nothing here has been
run on hardware yet (see *Known risks*).

The Android build is the Linux host (SDL2) packaged as an APK: a launcher
screen extracts the ISO, then SDL's `SDLActivity` loads `libmain.so`, which is
this project (the lifted C, the vendored xboxrecomp runtime,
`src/android_host.c`).

Renderer: **OpenGL ES 3.x** by default (the existing GL renderer, ported: see
below). The Vulkan renderer is available with `-Pnfsu2.vulkan=true`.

## What you need

| | |
|---|---|
| Device | arm64, Android 10+ (API 29), OpenGL ES 3.0 (3.2 preferred). Vulkan build: Vulkan 1.3 with `VK_KHR_push_descriptor` and `VK_EXT_vertex_input_dynamic_state`. 6 GB RAM or more. Free storage: about twice the ISO (ISO + extracted game) while extracting. |
| Build host | Android Studio / Gradle 8.9 (wrapper included), Android NDK r27+, CMake 3.22+ (SDK Manager), git |
| Game | your own game **ISO** (original Xbox, NTSC-U). It is extracted on the phone. |
| Lifted C | `tools/regen.sh` output (`gen/`). It is host independent: the one you built for Linux/Switch works. |

## Build

```sh
export ANDROID_NDK_HOME=/path/to/ndk
android/tools/fetch_deps.sh                 # SDL2 sources -> android/deps/SDL
# optional, for the movies (otherwise the slow lifted VP6 decoder plays them):
tools/build_ffmpeg_vp6.sh android           # PREFIX=android/deps/ffmpeg-vp6-android

cd android
./gradlew assembleRelease -Pnfsu2.genDir=/path/to/gen
# -> app/build/outputs/apk/release/app-release.apk  (signed with the debug key)
adb install -r app/build/outputs/apk/release/app-release.apk
```

Vulkan instead of OpenGL ES: run `android/tools/build_glslang.sh` once and add
`-Pnfsu2.vulkan=true`.

Useful CMake switches (add to `arguments` in `app/build.gradle`):
`-DNFSU2_GEN_OPT=-O1` if clang runs out of memory on the lifted chunks,
`-DNFSU2_LTO=ON` for ThinLTO, `-DNFSU2_ANDROID_TLS_MODEL=initial-exec` to try
cheaper thread-local guest registers (see below).

## Game data: put the ISO in, it does the rest

On first start the app creates `Android/data/com.nfsu2x.nfsu2/files/iso/` and
shows its path. Copy the ISO there:

```sh
android/tools/push_game.sh /path/to/game.iso      # adb; Android 11+ hides Android/data from file managers
```

or tap **Choose ISO file…** and pick it anywhere (it is read in place, no copy).
The app then:

1. waits until the ISO has stopped growing (a copy still in progress is not read),
2. extracts it into `files/game/` with a progress bar (resumes if interrupted),
3. **deletes the ISO** (a file picked through the picker is deleted too when its
   storage provider allows it; otherwise you are told to delete it yourself),
4. starts the game. From then on it starts straight into the game.

The ISO reader understands trimmed XISOs and raw dumps (partition offsets 0,
`0x18300000`, `0xFD90000`, `0x2090000`). It has been tested against images
built by `tools/make_test_xiso.py` at those offsets (`tools/XisoSelfTest.java`),
not against a real disc yet.

```
files/
├── iso/             drop the ISO here (also accepted: directly in files/)
├── game/            the extracted disc (default.xbe, NFSUNDER/, ... and UDATA/ saves)
├── save/            runtime saves
├── nfsu2x_env.txt   optional KEY=VALUE settings, same keys as the main README
├── nfsu2x_log.txt   log of the last run (also in logcat, tag "nfsu2")
└── progcache.bin, ...   shader caches
```

An already extracted disc can be pushed to `files/game/` instead.

## Controls

* A Bluetooth/USB gamepad (anything SDL's game-controller layer knows) is
  player 1 and hides the overlay. It can be connected while the game runs.
* Otherwise the on-screen controls: floating left stick, A/B/X/Y, LT/RT,
  LB/RB (Black/White), Back/Start. Landscape only.
* Buttons map by label, like the Switch build (`RECOMP_PAD_LAYOUT=position`
  for the Xbox layout). No second player on touch.

## OpenGL ES port of the renderer

`nv2a_gl` was desktop GL 3.3 core. On Android the same code runs on an ES 3.x
context (`RECOMP_GLES=1` selects it on a desktop for testing):

| Desktop GL | OpenGL ES 3 |
|---|---|
| `GL_BGRA` uploads | bytes go in as RGBA, the texture is swizzled R<->B when sampled; render targets are plain RGBA |
| BGRA vertex colours (`GL_ARB_vertex_array_bgra`) | fetched as RGBA and swapped in the vertex shader (`u_bgra`) |
| `glClearDepth`/`glDepthRange` (double) | `...f` variants |
| `GL_DEPTH_CLAMP` | not available; the vertex shader already clamps clip z |
| `GL_CLAMP_TO_BORDER` | ES 3.2 only; edge clamp on 3.0/3.1 |
| S3TC (DXT) | extension; if the driver refuses, the textures are decoded on the CPU (already the fallback) |
| `#version 330 core` | `#version 300 es` with `highp` defaults |

Checked: all generated shaders (random vertex microcode and combiner keys, 120
programs) pass `glslangValidator` as GLSL ES 3.00 exactly when they pass as
desktop 3.30. No driver has run any of it.

## What changed in the code

| Where | Change |
|---|---|
| `CMakeLists.txt` | Android branch: `libmain.so`, SDL2 built in-tree, GL ES or Vulkan, 16 KB page alignment |
| `android/.../LauncherActivity.java`, `XisoReader.java`, `IsoInstaller.java` | ISO detection, extraction, deletion, progress UI |
| `src/android_host.c` | storage paths, `nfsu2x_env.txt`, logcat + log file, touch-pad state (JNI) |
| `src/main.c` | `SDL_main`, big-stack game thread (as on Switch), unwinder-based crash report (no `execinfo.h` in bionic) |
| `xboxrecomp/.../win32_compat.c` | `sched_setaffinity` (no `pthread_setaffinity_np`), `memfd_create` syscall, `TerminateThread` without `pthread_cancel` |
| `xboxrecomp/.../nv2a_gl/*` | the ES 3 port above |
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
* **GL ES drivers.** Mobile drivers are stricter than Mesa: watch the log for
  `[GL] ... link failed` / `incomplete` lines. If the swizzle approach to BGRA
  misbehaves on a driver, `RECOMP_GL_DXT=0` and the log are the first things
  to check. The game's GL renderer has no cube maps (car reflections); the
  Vulkan build has them.
* **Context loss.** When the app is backgrounded SDL may take the EGL context
  away; the present path re-makes it current, but a driver that really destroys
  it would need a rebuild of every GL object (not implemented).
* **Guest address space.** Like macOS arm64 the guest cannot sit at its own
  addresses; the runtime's `g_xbox_mem_offset` path is used, plus a 2 GB
  reservation for the extended VMA. A device with a strict `RLIMIT_AS` would
  fail at boot ("failed to map base view").
* **16 KB-page devices.** The guest's 4 KB page bookkeeping (`guest_vmem.c`)
  was never run on a 16 KB host.
* **Backgrounding.** SDL blocks its event pump while the app is paused, which
  pauses the game with it. Audio and the guest clock are not paused.
* **Performance.** The Switch build manages ~20 fps in races; a current
  Snapdragon core is faster than a Cortex-A57, the GPU path is untested.
