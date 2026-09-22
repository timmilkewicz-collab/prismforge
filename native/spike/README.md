# Direct3D 11 to Spout feasibility spike

This standalone program renders an animated 1920x1080 BGRA8 Direct3D 11 texture
on the RTX 4070 and publishes it as the `PrismForge` Spout sender. Its probe mode
receives the same sender through the official Spout2 DirectX API and checks
dimensions, format, fresh frames, and changing pixel content. It does not depend
on the future engine.

Build with the installed Visual Studio 2026 C++ tools:

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S native/spike -B build/spout-spike -G 'Visual Studio 18 2026' -A x64
& $cmake --build build/spout-spike --config Release --target PrismForge.SpoutSpike --parallel 8
```

The build fetches Spout2 at tag `2.007.017`, pinned to commit
`f49e2f469f8cb25f559a6eaa61a3f5b8173fc100`. For an offline build, add
`-DFETCHCONTENT_SOURCE_DIR_SPOUT2=<absolute local Spout2 checkout>` to the
configure command.

Run the sender in one terminal and the receiver probe in a second:

```powershell
& build/spout-spike/bin/Release/PrismForge.SpoutSpike.exe --seconds 30
& build/spout-spike/bin/Release/PrismForge.SpoutSpike.exe --probe --seconds 10
```

For the planned 30-minute soak, run the sender with `--seconds 1800` and add a
Spout input in Resolume named `PrismForge`. Confirm live animation, fixed
1920x1080 dimensions, and the reported average rate. Receiver probe success
establishes Spout texture transfer; it does not establish Resolume reception.

Build `PrismForge.EffectProbe` from the same CMake tree to check the four deck
effect slots against actual received GPU pixels. The probe renders the same
scene at a fixed timestamp with and without each effect, and fails if a sampled
output hash is unchanged. It also checks all four internal quality sizes while
the received Spout texture remains 1920x1080 BGRA8, including the 30 fps
safety-tier gate. The same probe verifies panic-dim lowers full-frame average
luminance, blackout emits zero RGB, and a deliberately invalid shader reload
keeps the previous valid output. It restores the shader and verifies a valid
reload afterward. The reload test uses a unique temporary copy of all twelve
shaders; source assets are not changed:

```powershell
& $cmake --build build/spout-spike --config Release --target PrismForge.EffectProbe
& build/spout-spike/bin/Release/PrismForge.EffectProbe.exe assets/shaders
```
