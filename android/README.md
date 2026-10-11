# Android port

**Status: work in progress.** Everything below is written, but none of it has been compiled
with the NDK or run on a device yet. Expect build errors and crashes on the first runs. See
[Known risks](#known-risks). **PS Vita is only a plan** and has no code.

## How it works

The Android build is the Linux host (SDL2) packaged as an APK.

1. A launcher screen finds the game ISO and extracts it, then deletes the ISO.
2. SDL's `SDLActivity` loads `libmain.so`. That library is this project: the lifted C, the
   vendored xboxrecomp runtime and `src/android_host.c`.
3. The renderer is **OpenGL ES 3.x** by default (the existing OpenGL renderer, ported; see
   [the table below](#opengl-es-port)). The Vulkan renderer is available with
   `-Pnfsu2.vulkan=true`.

## Requirements

| | |
|---|---|
| Device | arm64, Android 10 (API 29) or newer, OpenGL ES 3.0 (3.2 preferred), 6 GB RAM or more. Free storage of about twice the ISO size while extracting. Vulkan build: Vulkan 1.3 with `VK_KHR_push_descriptor` and `VK_EXT_vertex_input_dynamic_state`. |
| Build host | Gradle 8.9 (wrapper included), Android NDK r27 or newer, CMake 3.22 or newer, git |
| Game | Your own original Xbox NTSC-U game **ISO**. It is extracted on the phone. |
| Lifted C | The `gen/` directory produced by `tools/regen.sh` from your `default.xbe`. It is host independent, so the one built for Linux or Switch works. |

## Building

### Locally

```sh
export ANDROID_NDK_HOME=/path/to/ndk
android/tools/fetch_deps.sh                 # SDL2 sources -> android/deps/SDL
# Optional, for the movies (otherwise the slow lifted VP6 decoder plays them):
tools/build_ffmpeg_vp6.sh android           # PREFIX=android/deps/ffmpeg-vp6-android

cd android
./gradlew assembleRelease -Pnfsu2.genDir=/path/to/gen
adb install -r app/build/outputs/apk/release/app-release.apk   # signed with the debug key
```

For Vulkan instead of OpenGL ES, run `android/tools/build_glslang.sh` once and add
`-Pnfsu2.vulkan=true`.

CMake switches worth knowing (add them to `arguments` in `app/build.gradle`):

- `-DNFSU2_GEN_OPT=-O1` if clang runs out of memory on the lifted chunks.
- `-DNFSU2_LTO=ON` for ThinLTO.
- `-DNFSU2_ANDROID_TLS_MODEL=initial-exec` to try cheaper thread-local guest registers.

### With GitHub Actions

`.github/workflows/android.yml` builds the APK on a GitHub runner, which has the NDK
preinstalled. It reads `default.xbe` from the root of a branch named `private-assets`, runs
`tools/regen.sh`, builds, and uploads the APK as a workflow artifact. If that branch does not
exist, the job exits without doing anything, so it is harmless in a public repository.

Use it only in a **private** repository. The branch holds a copy of the game executable, and the
lifted C is derived from it. The tail of each build log is pushed to a `ci-logs` branch.

## Game data

On first start the app creates `Android/data/com.nfsu2x.nfsu2/files/iso/` and shows its path.
Put the ISO there:

```sh
android/tools/push_game.sh /path/to/game.iso    # adb; Android 11 and newer hide Android/data from file managers
```

Or tap **Choose ISO file** and pick it anywhere (it is read in place, without copying). The app
then:

1. waits until the ISO has stopped growing, so a copy in progress is never read;
2. extracts it into `files/game/` with a progress bar, and resumes if interrupted;
3. **deletes the ISO** (for a file chosen with the picker, only when its storage provider allows
   it; otherwise you are told to delete it yourself);
4. starts the game. From then on the app goes straight into the game.

The reader handles trimmed XISOs and raw dumps (partition offsets 0, `0x18300000`,
`0xFD90000`, `0x2090000`). It was tested on images built by `tools/make_test_xiso.py` at those
offsets (`tools/XisoSelfTest.java`), not on a real disc yet. An already extracted disc can be
pushed to `files/game/` instead.

```
files/
  iso/               drop the ISO here (directly in files/ also works)
  game/              the extracted disc: default.xbe, NFSUNDER/, ... and the UDATA/ saves
  save/              runtime saves
  nfsu2x_env.txt     optional KEY=VALUE settings, the same keys as the main README
  nfsu2x_log.txt     log of the last run (also in logcat, tag "nfsu2")
  progcache.bin ...  shader caches
```

## Controls

- A Bluetooth or USB gamepad (anything SDL's game-controller layer knows) is player 1 and hides
  the overlay. It can be connected while the game runs.
- Without one, the on-screen controls appear: floating left stick, A/B/X/Y, LT/RT, LB/RB
  (Black/White), Back and Start. Landscape only.
- Buttons map by label, like the Switch build (`RECOMP_PAD_LAYOUT=position` for the Xbox
  layout). There is no second player on touch.

## OpenGL ES port

`nv2a_gl` was written for desktop GL 3.3 core. On Android the same code runs on an ES 3.x
context. `RECOMP_GLES=1` selects the ES path on a desktop, for testing.

| Desktop GL | OpenGL ES 3 |
|---|---|
| `GL_BGRA` uploads | Bytes go in as RGBA and the texture is swizzled R<->B when sampled. Render targets are plain RGBA. |
| BGRA vertex colours | Fetched as RGBA and swapped in the vertex shader (`u_bgra`). |
| `glClearDepth` / `glDepthRange` (double) | The `...f` variants. |
| `GL_DEPTH_CLAMP` | Not available. The vertex shader already clamps clip z. |
| `GL_CLAMP_TO_BORDER` | ES 3.2 only; edge clamp on 3.0 and 3.1. |
| S3TC (DXT) | Through the extension. If the driver refuses, textures are decoded on the CPU, which was already the fallback. |
| `#version 330 core` | `#version 300 es` with `highp` defaults. |

Checked so far: all generated shaders (120 programs from random vertex microcode and combiner
keys) pass `glslangValidator` as GLSL ES 3.00 exactly when they pass as desktop 3.30. No device
driver has run any of it.

## What changed in the code

| Where | Change |
|---|---|
| `CMakeLists.txt` | Android branch: `libmain.so`, SDL2 built in-tree, GL ES or Vulkan, 16 KB page alignment. |
| `android/.../LauncherActivity.java`, `XisoReader.java`, `IsoInstaller.java` | ISO detection, extraction, deletion, progress UI. |
| `src/android_host.c` | Storage paths, `nfsu2x_env.txt`, logcat and log file, touch-pad state (JNI). |
| `src/main.c` | `SDL_main`, big-stack game thread (as on Switch), unwinder-based crash report (bionic has no `execinfo.h`). |
| `xboxrecomp/.../win32_compat.c` | `sched_setaffinity` (no `pthread_setaffinity_np`), `memfd_create` syscall, `TerminateThread` without `pthread_cancel`. |
| `xboxrecomp/.../nv2a_gl/*` | The ES 3 port above. |
| `xboxrecomp/.../nv2a_vk.c` | Surface and swapchain recreation when the app is backgrounded, identity pre-transform, fullscreen window. |
| `xboxrecomp/.../xinput_device.c` | Touch pad as player 1 when no pad is attached, pad hot-plug rescan. |
| `xboxrecomp/.../CMakeLists.txt` | No OpenSSL, null D3D8 layer, in-tree SDL2 on Android. |
| `tools/regen.sh`, `xboxrecomp/tools/*/output.py` | The toolkit's missing `output.py` modules and the XBE analysis JSON step, needed for a clean regeneration. |
| `.github/workflows/android.yml` | The APK build described above. |

## Known risks

None of these has been checked on a device.

- **Clang on the lifted code.** The generated C has only been built with GCC (Linux, devkitA64).
  The first NDK build will show whether clang accepts all of it.
- **Thread-local registers.** The guest registers are `__thread`. In a shared library that costs
  a call per function, slower than on the Switch's static executable. Try
  `NFSU2_ANDROID_TLS_MODEL=initial-exec`; `PERF_NOTES.md` covers the register-in-locals work that
  already reduces this.
- **OpenGL ES drivers.** Mobile drivers are stricter than Mesa. Watch the log for
  `[GL] ... link failed` and `incomplete` lines. If the swizzle approach to BGRA misbehaves on a
  driver, check `RECOMP_GL_DXT=0` and the log first. The OpenGL renderer has no cube maps (car
  reflections); the Vulkan build does.
- **Context loss.** When the app is backgrounded SDL may take the EGL context away. The present
  path makes it current again, but a driver that really destroys it would need every GL object
  rebuilt, which is not implemented.
- **Guest address space.** As on macOS arm64, the guest cannot sit at its own addresses, so the
  runtime's `g_xbox_mem_offset` path is used plus a 2 GB reservation for the extended VMA. A
  device with a strict `RLIMIT_AS` would fail at boot ("failed to map base view").
- **16 KB-page devices.** The guest's 4 KB page bookkeeping (`guest_vmem.c`) has never run on a
  16 KB host.
- **Backgrounding.** SDL blocks its event pump while the app is paused, which pauses the game
  too. Audio and the guest clock are not paused.
- **Performance.** The Switch build manages about 20 fps in races. A current Snapdragon core is
  faster than a Cortex-A57, but the GPU path is untested.
