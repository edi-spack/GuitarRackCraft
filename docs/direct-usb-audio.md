# Direct USB audio integration

GuitarRackCraft now builds its Oboe factory from the pinned
`3rd_party/directusb` submodule (wrapper 0.2.1 plus explicit context bootstrap).
Initialize it with `git submodule update --init 3rd_party/directusb`.
That project preserves MIT driver/wrapper and Apache-2.0 upstream Oboe licenses;
GuitarRackCraft remains GPL-3.0-or-later. No precompiled binary blobs are vendored.
The old `3rd_party/oboe` submodule is retained for existing prebuild/patch scripts
but is no longer linked into the app.

## Before and after

Previously CMake built Oboe as a static library inside `libguitarrackcraft.so`.
There was therefore no independent `liboboe.so` to replace in the APK. The app
requested AAudio mono float input, stereo float output, exclusive/low-latency
mode and an output-driven callback. Android AudioManager supplies device IDs
for the settings screen; Oboe does not populate that list.

Now `libguitarrackcraft.so` links the shared `liboboe.so` wrapper. CMake also
builds `libdirectusb.so` and `liboboeoriginal.so` from the same pinned source.
The wrapper dynamically loads both optional libraries; the app never directly
links the original factory. Matching Oboe 1.10.0 headers are used throughout,
without changing public class layouts. All libraries share the pinned NDK's
libc++ runtime, and their LOAD segments use 16 KB alignment. Current app builds
remain arm64-v8a because the plugin prebuild supports that ABI.

Supported USB routes use one direct usbfs session for capture and playback,
bypassing AAudio/Android audio processing. Known non-USB routes and positively
unsupported modes delegate to stock Oboe/Android. USB permission, busy, I/O and
disconnect errors do not silently switch devices. Explicit phone routes can
coexist with USB, but independently clocked mixed routes are not drift-compensated.
The application context and JavaVM are passed explicitly through JNI during
EngineInitHelper initialization; no hidden ActivityThread call is needed.
Android still requires this app's USB permission. Approve the dialog and press
Start again; errors appear in the rack screen and logcat. USB access is optional
in the manifest, so phone-only operation remains available.

## Effects processing and lifecycle

The existing signal path is preserved: USB/phone input -> first channel as mono
float -> stereo plugin chain -> first two playback channels. USB multichannel
channels are not additional Android device choices. Output fixed power-of-two
callbacks still support convolution plugins, and the input read remains
nonblocking. Buffer allocation and plugin activation now finish before streams
start. Callback bounds are checked before reading; no scratch resizing occurs
inside the callback. Output is initialized to silence on early exit.

Output stop/close quiesces its callback before input is destroyed or the plugin
chain is deactivated. USB close is synchronous; the existing AAudio destruction
pause is retained only for AAudio. No backend reopen or USB permission work runs
inside the data callback. The settings screen identifies Direct USB separately
from AAudio/OpenSL ES and does not treat absent MMAP as a USB routing failure.

DSP still runs synchronously in the USB worker callback. Effect chains must
finish within the queued transport deadline. Existing plugin locks, logging,
recording and VST subprocess statistics (including /proc reads) remain possible
jitter sources in the host; this integration is not a hard-real-time guarantee.
USB transport latency, host buffer estimates and actual loopback latency are
different quantities. Validate the actual rack at modest buffer sizes before
reducing them, and watch underruns/CPU load. High-rate operation increases DSP
work; neither this build nor synthetic tests measure phone CPU limits.

The existing app UI starts at 48 kHz with Float buffers. The driver and wrapper
can negotiate 96/192 kHz when requested through the engine API and supported
by the device, but this change does not add rate/channel settings. Float32 has
about 24 significant binary digits; a 32-bit USB subslot does not turn the
plugin signal path into full-precision integer PCM32. BADD stays fixed at 48 kHz.

## Build and validation

Use the normal `./build.sh playstore` or `./build.sh full` native/plugin
prebuild before building a usable effects-host APK. The existing missing-input
checks are preserved. On a configured Android SDK/NDK environment:

```sh
./gradlew :app:assemblePlaystoreDebug :app:lintPlaystoreDebug
```

For integration-only work without the plugin prebuild, the existing
`-Pgrc.allowMissingNativeInputs=true` option builds a UI/audio smoke APK with
LV2 stubs. It is not a complete effects host; no plugin operation is established
by that build. Its pre-existing undefined UI URID globals are now explicitly
stubbed only when HAVE_LV2=0; production LV2 uses the DSP's real shared map.

Host regression tests:

```sh
cmake -S tests/directusb -B build/directusb-tests -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build build/directusb-tests --parallel 2
ctest --test-dir build/directusb-tests --output-on-failure
```

The RackCraft contract test covers mono input, stereo effects-style output,
input-first open, output probe/reopen, fixed 64/256/1024-frame callbacks and
output-first teardown at 48/96/192 kHz. Controller and original-Oboe factories
are mocks; the pinned wrapper's existing regressions also run. These tests do
not execute the real plugin rack or establish Android USB permission, sound,
full/Play Store distribution behavior or measured round-trip latency.

On-device checklist: initialize with a wired SSL 2 MKII, grant USB permission,
select matching input/output or defaults, verify Direct USB in settings,
exercise bypass then a small effects chain, stop/restart/change buffers, and
unplug/reconnect. Separately test built-in microphone/speaker fallback. Start
with low headphone volume and disable interface hardware direct monitoring.

Validation in the cloud checkout: all seven host CTest targets pass with
ASan/UBSan; the complete arm64 native library and Play Store debug smoke APK
build with explicit LV2 stubs. The APK contains all three audio libraries plus
the host library with 16 KB LOAD alignment, and its debug signature verifies.
All 789 stock Oboe export names are retained. Android lint reports eight existing
TV manifest/API-level errors and 41 warnings, including MissingLeanbackLauncher,
UnsupportedTvHardware and unguarded API 28 calls. Those unrelated findings have
not been suppressed or fixed here. No actual rack/phone audio validation or
complete LV2/VST/plugin production build was performed in this checkout.
