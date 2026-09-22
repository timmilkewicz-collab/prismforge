param([string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path)

$ErrorActionPreference = 'Stop'
$sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$fxc = Get-ChildItem -LiteralPath $sdkBin -Recurse -Filter fxc.exe -File |
    Where-Object { $_.FullName -match '\\x64\\fxc\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $fxc) { throw 'Windows SDK x64 fxc.exe was not found' }

$manifestPath = Join-Path $RepositoryRoot 'assets\scenes\scenes-v1.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schema -ne 'SceneManifestV1' -or $manifest.version -ne 1) {
    throw 'Scene manifest schema/version is invalid'
}
$ids = @($manifest.scenes | ForEach-Object { $_.id })
if (($ids | Select-Object -Unique).Count -ne $ids.Count) {
    throw 'Duplicate scene IDs in manifest'
}
$outDir = Join-Path $RepositoryRoot 'build\shader-check'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
foreach ($scene in $manifest.scenes) {
    $source = Join-Path $RepositoryRoot $scene.shader
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Missing shader for $($scene.id): $source"
    }
    foreach ($tier in 0..3) {
        $output = Join-Path $outDir "$($scene.id)-tier$tier.cso"
        & $fxc.FullName /nologo /T ps_5_0 /E main /WX "/DQUALITY_TIER=$tier" `
            "/Fo$output" $source
        if ($LASTEXITCODE -ne 0) {
            throw "Shader compile failed: $($scene.id), tier $tier"
        }
    }
}
Write-Output "Compiled $($manifest.scenes.Count) scene shaders across four quality tiers"
