param([switch]$Update)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zgramRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$palettePath = Join-Path $zgramRoot 'Telegram/Resources/themes/zgram/colors.tdesktop-theme'
$themePath = Join-Path $zgramRoot 'Telegram/Resources/night.tdesktop-theme'
$paletteText = [IO.File]::ReadAllText($palettePath)
$colors = @{}
foreach ($line in ($paletteText -split '\r?\n')) {
    if ($line -match '^([A-Za-z0-9_]+):\s*([^;]+);') {
        $colors[$Matches[1]] = $Matches[2].Trim()
    }
}

function Resolve-Color([string]$key) {
    for ($i = 0; $i -lt 20; $i++) {
        if (-not $colors.ContainsKey($key)) { throw "Missing color: $key" }
        $value = $colors[$key]
        if ($value -match '^#[0-9a-fA-F]{6}$') { return $value }
        $key = $value
    }
    throw "Unresolved color: $key"
}

function Get-Luminance([string]$hex) {
    $rgb = @(1, 3, 5 | ForEach-Object {
        $channel = [Convert]::ToInt32($hex.Substring($_, 2), 16) / 255.0
        if ($channel -le 0.04045) { $channel / 12.92 }
        else { [Math]::Pow(($channel + 0.055) / 1.055, 2.4) }
    })
    return 0.2126 * $rgb[0] + 0.7152 * $rgb[1] + 0.0722 * $rgb[2]
}

$checks = @(
    @('boxTextFg', 'boxBg', 4.5),
    @('windowFg', 'boxBg', 4.5),
    @('boxTitleFg', 'boxBg', 4.5),
    @('boxTextFgError', 'boxBg', 4.5),
    @('attentionButtonFg', 'boxBg', 4.5),
    @('lightButtonFg', 'boxBg', 4.5),
    @('windowSubTextFg', 'boxBg', 4.5),
    @('menuFgDisabled', 'menuBg', 4.5),
    @('placeholderFg', 'windowBg', 4.5),
    @('activeButtonSecondaryFg', 'activeButtonBg', 4.5),
    @('membersAboutLimitFg', 'boxBg', 4.5),
    @('profileStatusFgOver', 'windowBgOver', 4.5),
    @('checkboxFg', 'boxBg', 3.0)
)
foreach ($check in $checks) {
    $fg = Get-Luminance (Resolve-Color $check[0])
    $bg = Get-Luminance (Resolve-Color $check[1])
    $ratio = ([Math]::Max($fg, $bg) + 0.05) / ([Math]::Min($fg, $bg) + 0.05)
    if ($ratio -lt $check[2]) { throw "Low contrast for $($check[0]): $ratio" }
    Write-Output ('{0}: {1:N2}:1' -f $check[0], $ratio)
}

$mode = if ($Update) { [IO.Compression.ZipArchiveMode]::Update } else { [IO.Compression.ZipArchiveMode]::Read }
$archive = [IO.Compression.ZipFile]::Open($themePath, $mode)
try {
    $entry = $archive.GetEntry('colors.tdesktop-theme')
    if ($null -eq $entry) { throw 'The bundled theme has no color palette.' }
    if ($Update) {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($paletteText)
        $stream = $entry.Open()
        try {
            $stream.SetLength(0)
            $stream.Write($bytes, 0, $bytes.Length)
        } finally { $stream.Dispose() }
    } else {
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $embeddedText = $reader.ReadToEnd() } finally { $reader.Dispose() }
        if (($embeddedText -replace '\r\n', "`n") -cne ($paletteText -replace '\r\n', "`n")) {
            throw 'Bundled palette is stale. Run this script with -Update.'
        }
    }
} finally { $archive.Dispose() }
Write-Output 'Zgram theme checks passed.'
