# Compile StudyLab et lance les tests (Windows, Developer PowerShell for VS 2022).
# Usage : .\scripts\build.ps1 [-Preset windows-msvc-release]
param([string]$Preset = "windows-msvc-release")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Exécutable : build\$Preset\src\app\studylab.exe"
