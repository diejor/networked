# Local threads-off web export for the Discord Activity, mirroring the CI step.
#
# Swaps the main scene to Bomber and exports the "Web" preset into build/web.
# Tolerates the known Godot teardown segfault by gating on artifacts on disk
# (a non-empty index.pck means the export ran to completion).
#
#   pwsh addons/networked_activity/scripts/build_web.ps1 -Godot "C:\path\to\godot.exe"
param(
    [string]$Godot = "godot",
    [string]$MainSceneUid = "uid://3a3852p4fypv",  # examples/bomber
    [string]$OutDir = "build/web"
)

$ErrorActionPreference = "Stop"
$repo = Resolve-Path "$PSScriptRoot/../../.."
Set-Location $repo

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Swap main scene in a copy so the working tree stays clean.
$projectPath = Join-Path $repo "project.godot"
$backup = Get-Content $projectPath -Raw
try {
    (Get-Content $projectPath -Raw) `
        -replace '(?m)^run/main_scene=.*', "run/main_scene=`"$MainSceneUid`"" |
        Set-Content $projectPath -Encoding utf8

    & $Godot --headless --import
    & $Godot --headless --verbose --export-release "Web" (Join-Path $OutDir "index.html")
}
finally {
    Set-Content $projectPath -Value $backup -Encoding utf8 -NoNewline
}

$wasm = Join-Path $OutDir "index.wasm"
$pck = Join-Path $OutDir "index.pck"
if ((Test-Path $wasm) -and (Test-Path $pck) -and ((Get-Item $pck).Length -gt 0)) {
    Write-Host "Web export complete: $OutDir"
} else {
    throw "Web export did not produce its artifacts in $OutDir"
}
