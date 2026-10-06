[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$outputDirectory = Join-Path $PSScriptRoot 'musical'
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$columns = [System.Collections.Generic.List[string]]::new()
$columns.AddRange([string[]]@('sampleIndex', 'rms', 'peak'))
for ($band = 0; $band -lt 32; $band++) {
    $columns.Add(('band{0:D2}' -f $band))
}
$columns.AddRange([string[]]@(
    'bass', 'mids', 'highs', 'hit', 'accent', 'hitCount', 'accentCount',
    'bpm', 'beatPhase', 'beatConfidence'
))

function Limit([double]$value) {
    return [Math]::Max(0.0, [Math]::Min(1.0, $value))
}

function Format-Float([double]$value) {
    return $value.ToString('0.000000', $invariant)
}

function Get-Bands([double]$bass, [double]$mids, [double]$highs) {
    $values = [System.Collections.Generic.List[string]]::new()
    for ($band = 0; $band -lt 32; $band++) {
        if ($band -lt 11) {
            $base = $bass
        } elseif ($band -lt 23) {
            $base = $mids
        } else {
            $base = $highs
        }
        $shape = 0.88 + 0.04 * ($band % 4)
        $values.Add((Format-Float (Limit ($base * $shape))))
    }
    return $values
}

function Add-Frame(
    [System.Collections.Generic.List[string]]$lines,
    [int]$frame,
    [double]$rms,
    [double]$peak,
    [double]$bass,
    [double]$mids,
    [double]$highs,
    [bool]$hit,
    [bool]$accent,
    [UInt64]$hitCount,
    [UInt64]$accentCount,
    [double]$bpm,
    [double]$beatPhase,
    [double]$beatConfidence
) {
    $fields = [System.Collections.Generic.List[string]]::new()
    $fields.Add(([UInt64](($frame + 1) * 800)).ToString($invariant))
    $fields.Add((Format-Float $rms))
    $fields.Add((Format-Float $peak))
    $fields.AddRange([string[]](Get-Bands $bass $mids $highs))
    $fields.Add((Format-Float $bass))
    $fields.Add((Format-Float $mids))
    $fields.Add((Format-Float $highs))
    $fields.Add($(if ($hit) { '1' } else { '0' }))
    $fields.Add($(if ($accent) { '1' } else { '0' }))
    $fields.Add($hitCount.ToString($invariant))
    $fields.Add($accentCount.ToString($invariant))
    $fields.Add((Format-Float $bpm))
    $fields.Add((Format-Float $beatPhase))
    $fields.Add((Format-Float $beatConfidence))
    $lines.Add(($fields -join ','))
}

function Write-Fixture([string]$name, [int]$frameCount, [scriptblock]$makeFrame) {
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add(($columns -join ','))
    for ($frame = 0; $frame -lt $frameCount; $frame++) {
        & $makeFrame $lines $frame
    }
    $path = Join-Path $outputDirectory $name
    [System.IO.File]::WriteAllLines($path, $lines, [System.Text.UTF8Encoding]::new($false))
}

Write-Fixture 'a_silence.csv' 240 {
    param($lines, $frame)
    $rms = 0.0006 + 0.0002 * [Math]::Sin($frame * 0.13)
    Add-Frame $lines $frame $rms ($rms * 1.8) ($rms * 0.7) ($rms * 0.5) ($rms * 0.4) `
        $false $false 0 0 0.0 0.0 0.0
}

$script:lowHits = 0
$script:lowAccents = 0
Write-Fixture 'b_steady_low_groove.csv' 360 {
    param($lines, $frame)
    $period = 30
    $withinBeat = $frame % $period
    $hit = $withinBeat -eq 0
    $beatNumber = [Math]::Floor($frame / $period)
    $accent = $hit -and (($beatNumber % 4) -eq 0)
    $pulse = [Math]::Exp(-$withinBeat / 3.0)
    $rms = 0.018 + 0.018 * $pulse + 0.0015 * [Math]::Sin($frame * 0.17)
    $script:lowHits = if ($hit) { $script:lowHits + 1 } else { $script:lowHits }
    $script:lowAccents = if ($accent) { $script:lowAccents + 1 } else { $script:lowAccents }
    Add-Frame $lines $frame $rms ([Math]::Min(1.0, $rms * 2.2)) (0.030 + 0.030 * $pulse) `
        (0.012 + 0.010 * $pulse) (0.007 + 0.004 * $pulse) $hit $accent `
        ([UInt64]$script:lowHits) ([UInt64]$script:lowAccents) 120.0 ($withinBeat / $period) 0.92
}

$script:transientHits = 0
$script:transientAccents = 0
Write-Fixture 'c_repeated_transient_groove.csv' 360 {
    param($lines, $frame)
    $period = 30
    $withinBeat = $frame % $period
    $hit = $withinBeat -eq 0
    $beatNumber = [Math]::Floor($frame / $period)
    $accent = $hit -and (($beatNumber % 2) -eq 0)
    $pulse = [Math]::Exp(-$withinBeat / 1.5)
    $rms = 0.032 + 0.180 * $pulse
    if ($hit) { $script:transientHits++ }
    if ($accent) { $script:transientAccents++ }
    Add-Frame $lines $frame $rms ([Math]::Min(1.0, 0.10 + 0.75 * $pulse)) `
        (0.045 + 0.260 * $pulse) (0.028 + 0.100 * $pulse) (0.018 + 0.090 * $pulse) `
        $hit $accent ([UInt64]$script:transientHits) ([UInt64]$script:transientAccents) `
        120.0 ($withinBeat / $period) 0.97
}

$script:buildHits = 0
$script:buildAccents = 0
Write-Fixture 'd_gradual_buildup.csv' 600 {
    param($lines, $frame)
    $progress = $frame / 599.0
    $period = 30
    $withinBeat = $frame % $period
    $hit = ($frame -ge 90) -and ($withinBeat -eq 0)
    $beatNumber = [Math]::Floor($frame / $period)
    $accent = $hit -and (($beatNumber % 4) -eq 0)
    $pulse = if ($hit) { 1.0 } else { [Math]::Exp(-$withinBeat / 4.0) }
    $rms = 0.008 + 0.205 * [Math]::Pow($progress, 1.35) + 0.025 * $progress * $pulse
    if ($hit) { $script:buildHits++ }
    if ($accent) { $script:buildAccents++ }
    Add-Frame $lines $frame $rms ([Math]::Min(1.0, $rms * 2.3)) `
        (0.012 + 0.255 * $progress + 0.035 * $pulse) `
        (0.006 + 0.170 * $progress) (0.004 + 0.125 * $progress) `
        $hit $accent ([UInt64]$script:buildHits) ([UInt64]$script:buildAccents) `
        120.0 ($withinBeat / $period) (0.25 + 0.72 * $progress)
}

$script:dropHits = 0
$script:dropAccents = 0
Write-Fixture 'e_buildup_to_peak.csv' 600 {
    param($lines, $frame)
    $dropFrame = 420
    $buildProgress = [Math]::Min(1.0, $frame / [double]$dropFrame)
    $afterDrop = $frame -ge $dropFrame
    $period = 30
    $withinBeat = $frame % $period
    $hit = $withinBeat -eq 0
    $beatNumber = [Math]::Floor($frame / $period)
    $accent = $hit -and ((($beatNumber % 4) -eq 0) -or ($frame -eq $dropFrame))
    $pulse = [Math]::Exp(-$withinBeat / 2.0)
    if ($afterDrop) {
        $rms = 0.34 + 0.055 * $pulse
        $bass = 0.48 + 0.16 * $pulse
        $mids = 0.31 + 0.09 * $pulse
        $highs = 0.23 + 0.10 * $pulse
    } else {
        $rms = 0.008 + 0.220 * [Math]::Pow($buildProgress, 1.2) + 0.020 * $pulse
        $bass = 0.012 + 0.280 * $buildProgress + 0.035 * $pulse
        $mids = 0.008 + 0.190 * $buildProgress
        $highs = 0.006 + 0.145 * $buildProgress
    }
    if ($hit) { $script:dropHits++ }
    if ($accent) { $script:dropAccents++ }
    Add-Frame $lines $frame $rms ([Math]::Min(1.0, $rms * 2.35)) $bass $mids $highs `
        $hit $accent ([UInt64]$script:dropHits) ([UInt64]$script:dropAccents) `
        120.0 ($withinBeat / $period) (0.35 + 0.63 * $buildProgress)
}

$script:releaseHits = 0
$script:releaseAccents = 0
Write-Fixture 'f_peak_to_release.csv' 600 {
    param($lines, $frame)
    $peakFrames = 120
    $period = 30
    $withinBeat = $frame % $period
    $activePeak = $frame -lt $peakFrames
    $hit = $activePeak -and ($withinBeat -eq 0)
    $beatNumber = [Math]::Floor($frame / $period)
    $accent = $hit -and (($beatNumber % 2) -eq 0)
    if ($activePeak) {
        $decay = 1.0
    } else {
        $decay = [Math]::Exp(-($frame - $peakFrames) / 95.0)
    }
    $pulse = if ($activePeak) { [Math]::Exp(-$withinBeat / 2.0) } else { 0.0 }
    $rms = 0.002 + 0.360 * $decay + 0.040 * $pulse
    if ($hit) { $script:releaseHits++ }
    if ($accent) { $script:releaseAccents++ }
    Add-Frame $lines $frame $rms ([Math]::Min(1.0, $rms * 2.25)) `
        (0.002 + 0.50 * $decay) (0.002 + 0.34 * $decay) (0.001 + 0.25 * $decay) `
        $hit $accent ([UInt64]$script:releaseHits) ([UInt64]$script:releaseAccents) `
        120.0 ($withinBeat / $period) $(if ($activePeak) { 0.95 } else { 0.95 * $decay })
}

$script:isolatedHits = 0
$script:isolatedAccents = 0
Write-Fixture 'g_isolated_transient.csv' 240 {
    param($lines, $frame)
    $strikeFrame = 90
    $distance = $frame - $strikeFrame
    $pulse = if ($distance -ge 0) { [Math]::Exp(-$distance / 2.0) } else { 0.0 }
    $hit = $frame -eq $strikeFrame
    $accent = $hit
    if ($hit) {
        $script:isolatedHits++
        $script:isolatedAccents++
    }
    $rms = 0.001 + 0.42 * $pulse
    Add-Frame $lines $frame $rms ([Math]::Min(1.0, 0.002 + 0.94 * $pulse)) `
        (0.001 + 0.40 * $pulse) (0.001 + 0.25 * $pulse) (0.001 + 0.48 * $pulse) `
        $hit $accent ([UInt64]$script:isolatedHits) ([UInt64]$script:isolatedAccents) `
        0.0 0.0 0.0
}

$script:loudHits = 0
$script:loudAccents = 0
Write-Fixture 'h_sustained_loud.csv' 600 {
    param($lines, $frame)
    $hit = $frame -eq 0
    $accent = $hit
    if ($hit) {
        $script:loudHits++
        $script:loudAccents++
    }
    $rms = 0.32 + 0.012 * [Math]::Sin($frame * 0.09)
    Add-Frame $lines $frame $rms 0.72 `
        (0.44 + 0.012 * [Math]::Sin($frame * 0.07)) `
        (0.31 + 0.010 * [Math]::Sin($frame * 0.05)) `
        (0.23 + 0.008 * [Math]::Sin($frame * 0.11)) `
        $hit $accent ([UInt64]$script:loudHits) ([UInt64]$script:loudAccents) `
        120.0 (($frame % 30) / 30.0) 0.96
}

Write-Output "Generated eight musical feature-frame fixtures in $outputDirectory"
