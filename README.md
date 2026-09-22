# PrismForge

PrismForge is an original Windows-native VJ instrument for this RTX 4070 rig. The
live engine renders two scene decks with a crossfader into a fixed 1920×1080
BGRA8 Direct3D 11 texture, then publishes a `PrismForge` Spout sender. A local
WebView2 control window talks to the engine over `\\.\pipe\PrismForge.v1`.

This repository is an early development build, not a live-show replacement for
PrismBurst. PrismBurst and its launchers are deliberately untouched. The
[implementation status](docs/STATUS.md) distinguishes proven work from pending
venue tests and planned features. See the dated [validation record](docs/VALIDATION.md)
for exact build, GPU, IPC and device evidence.

## Build on Windows

Requirements: Visual Studio Build Tools with C++ and Windows SDK, CMake,
Ninja or the Visual Studio generator, .NET 10 SDK, Node.js, WebView2 Runtime,
and the RTX 4070 for the current fixed-adapter renderer. Git/network access is
needed for pinned source dependencies on the first native configure. The
`vcpkg.json` baseline is the preferred reproducible dependency path; the build
also has pinned upstream FetchContent fallback for local development.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cd control/ui
npm ci
npm run build
cd ..
dotnet build PrismForge.Control/PrismForge.Control.csproj -c Release
```

From `build/bin/Release`, run `PrismForge.Engine.exe`. The engine keeps sending
when the control window closes. For a bounded smoke run, pass `--seconds 10`
with `--no-audio --no-persist` to avoid touching your show autosave.
`PrismForge.Launcher.exe` is the normal entry point
once built and packaged. Do not replace any existing desktop shortcut yet.
Pass `--launchpad` to the Launcher or Engine only when intentionally assigning
the Launchpad S to PrismForge; normal startup leaves its MIDI ports untouched.
See [the alpha Launchpad profile](docs/LAUNCHPAD_S.md) before using it alongside
PrismBurst.

`scripts/package-portable.ps1 -NativeBuildDirectory <build-dir>` creates a
new, hash-manifested alpha folder under `dist/` after both native and Control
builds pass. It refuses to overwrite an existing package and does not install
or alter PrismBurst.

`tests/ipc-smoke.ps1 -EnginePath <engine-exe>` exercises local commands,
reconnect and OSC without saving show state. `tests/ui-crash-survival.ps1
-EnginePath <engine-exe> -ControlPath <control-exe>` starts only test-owned
processes and verifies output survives a hard Control exit.

## Repository map

- `native/core`: signal analysis, show state, bounded queues and adaptive tiers.
- `native/engine`: WASAPI/miniaudio capture, D3D11 scene/deck rendering, Spout
  sender and versioned named-pipe IPC.
- `native/spike`: independent D3D11→Spout sender/receiver proof.
- `assets/scenes` and `assets/shaders`: twelve original scene manifests and HLSL assets.
- `control`: local React/TypeScript UI and WebView2 host.
- `docs`: contracts, validation and honest feature status.

All shaders and UI assets are original; the Photism demos were used only as a
capability reference. No remote service is required at runtime.
