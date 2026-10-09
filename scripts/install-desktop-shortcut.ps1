[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [string]$ShortcutName = 'PrismForge'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$launcher = Join-Path $package 'PrismForge.Launcher.exe'
$icon = Join-Path $package 'PrismForge.ico'
if (-not (Test-Path -LiteralPath $launcher)) {
    throw "Launcher is missing: $launcher"
}
if (-not (Test-Path -LiteralPath $icon)) {
    throw "Icon is missing: $icon"
}

$desktop = [Environment]::GetFolderPath('Desktop')
$shortcutPath = Join-Path $desktop "$ShortcutName.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $launcher
$shortcut.WorkingDirectory = $package
$shortcut.IconLocation = "$icon,0"
$shortcut.Description = 'PrismForge — Engine + Control'
$shortcut.WindowStyle = 1
$shortcut.Save()

Write-Output "Desktop shortcut: $shortcutPath"
