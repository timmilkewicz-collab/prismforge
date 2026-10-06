[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$NativeBuildDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$nativeBuild = (Resolve-Path -LiteralPath $NativeBuildDirectory).Path
$nativeBin = Join-Path $nativeBuild 'bin\Release'
$engine = Join-Path $nativeBin 'PrismForge.Engine.exe'
$launcher = Join-Path $nativeBin 'PrismForge.Launcher.exe'
$assets = Join-Path $nativeBin 'assets'
foreach ($item in @($engine, $launcher, $assets)) {
    if (-not (Test-Path -LiteralPath $item)) {
        throw "Release build item is missing: $item"
    }
}
$frontend = Join-Path $repository 'control\ui\dist\index.html'
if (-not (Test-Path -LiteralPath $frontend -PathType Leaf)) {
    throw 'Control frontend is not built. Run control\build.ps1 first.'
}
$teaserGuide = Join-Path $repository 'docs\SHOW_TEASER.md'
if (-not (Test-Path -LiteralPath $teaserGuide -PathType Leaf)) {
    throw "Show teaser guide is missing: $teaserGuide"
}
$musicalMonitor = Join-Path $repository 'tools\watch-musical-state.ps1'
if (-not (Test-Path -LiteralPath $musicalMonitor -PathType Leaf)) {
    throw "Musical-state monitor is missing: $musicalMonitor"
}

if (-not $OutputDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path $repository "dist\PrismForge-alpha-$stamp"
}
$destination = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) {
    throw "Refusing to overwrite an existing portable package: $destination"
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null

$project = Join-Path $repository 'control\PrismForge.Control\PrismForge.Control.csproj'
dotnet publish $project -c Release -r win-x64 --self-contained false -o $destination
if ($LASTEXITCODE -ne 0) { throw 'Control publish failed' }
Copy-Item -LiteralPath $engine -Destination $destination
Copy-Item -LiteralPath $launcher -Destination $destination
Copy-Item -LiteralPath $assets -Destination $destination -Recurse
Copy-Item -LiteralPath (Join-Path $repository 'README.md') -Destination $destination
$packageDocs = Join-Path $destination 'docs'
New-Item -ItemType Directory -Path $packageDocs | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repository 'docs') -Filter '*.md' -File |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $packageDocs }
Copy-Item -LiteralPath (Join-Path $repository 'docs\STATUS.md') `
    -Destination (Join-Path $destination 'STATUS.md')
Copy-Item -LiteralPath $teaserGuide -Destination (Join-Path $destination 'SHOW_TEASER.md')
$packageTools = Join-Path $destination 'tools'
New-Item -ItemType Directory -Path $packageTools | Out-Null
Copy-Item -LiteralPath (Join-Path $repository 'tests\audio-signal-smoke.ps1') `
    -Destination $packageTools
Copy-Item -LiteralPath $musicalMonitor -Destination $packageTools

$hashes = Get-ChildItem -LiteralPath $destination -Recurse -File |
    Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($destination.Length).TrimStart('\')
        $digest = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$digest  $relative"
    }
$hashes | Set-Content -LiteralPath (Join-Path $destination 'SHA256SUMS.txt') `
    -Encoding Ascii
Write-Output "Portable PrismForge alpha: $destination"
Write-Output 'No PrismBurst files, desktop shortcuts, or installed programs were changed.'
