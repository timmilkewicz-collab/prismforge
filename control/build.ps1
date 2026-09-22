[CmdletBinding()]
param(
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-NativeSuccess {
    param([Parameter(Mandatory)][string]$Step)
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

$controlRoot = $PSScriptRoot
$uiRoot = Join-Path $controlRoot 'ui'
$hostProject = Join-Path $controlRoot 'PrismForge.Control\PrismForge.Control.csproj'
$testProject = Join-Path $controlRoot 'PrismForge.Control.Tests\PrismForge.Control.Tests.csproj'

Push-Location $uiRoot
try {
    npm ci
    Assert-NativeSuccess 'npm ci'
    if (-not $SkipTests) {
        npm run test
        Assert-NativeSuccess 'frontend tests'
    }
    npm run build
    Assert-NativeSuccess 'frontend build'
}
finally {
    Pop-Location
}

dotnet restore $hostProject
Assert-NativeSuccess 'dotnet restore'
dotnet build $hostProject --configuration Release --no-restore
Assert-NativeSuccess 'Control host build'

if (-not $SkipTests) {
    dotnet test $testProject --configuration Release
    Assert-NativeSuccess 'Control protocol tests'
}

$outputPath = Join-Path $controlRoot 'PrismForge.Control\bin\Release\net10.0-windows\PrismForge.Control.exe'
Write-Host "PrismForge Control built: $outputPath"
