# PrismForge Launcher

`PrismForge.Launcher.exe` starts the engine once per Windows logon session and
opens or re-activates the local control surface. It is a short-lived process:
closing Control or Launcher does not stop a healthy Engine or its Spout output.
The Engine independently owns `Local\PrismForge.Engine`, so direct Engine
launches cannot create a second output process either.

For a portable release, place `PrismForge.Launcher.exe`,
`PrismForge.Engine.exe`, and `PrismForge.Control.exe` in the same directory.
Engine shader/scene assets belong in its sibling `assets` directory. The
launcher deliberately does not search `Program Files`, the PATH, or PrismBurst
installations: it must not accidentally launch an older/different build.

Development builds can use explicit paths:

```powershell
PrismForge.Launcher.exe --engine "C:\path\to\PrismForge.Engine.exe" --control "C:\path\to\PrismForge.Control.exe"
```

`--check-layout` validates those paths without starting either process.
`--engine-only` starts/reuses the Engine without opening Control. Engine
stdout/stderr from launcher starts append to
`%LOCALAPPDATA%\PrismForge\Logs\engine.log`. A missing Control binary is an
error unless `--engine-only` is chosen. The launcher serializes concurrent
starts and never terminates an existing process.

It does not install shortcuts, alter desktop routing, or touch PrismBurst.
