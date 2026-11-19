# Installe les outils de compilation de StudyLab sur Windows 10/11 x64.
# Prérequis : Python 3 et Visual Studio 2022 (charge de travail « Développement Desktop en C++ »).
# Usage (PowerShell) : .\scripts\install-deps-windows.ps1 [-QtDir C:\Qt]
param(
    [string]$QtDir = "C:\Qt",
    [string]$QtVersion = "6.8.3"
)
$ErrorActionPreference = "Stop"

Write-Host "Installation de CMake et Ninja (winget)…"
winget install --id Kitware.CMake --exact --silent --accept-package-agreements --accept-source-agreements
winget install --id Ninja-build.Ninja --exact --silent --accept-package-agreements --accept-source-agreements

Write-Host "Installation de Qt $QtVersion (MSVC 2022 x64) dans $QtDir via aqtinstall…"
python -m pip install --upgrade aqtinstall
python -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 --outputdir $QtDir

$qtPath = Join-Path $QtDir "$QtVersion\msvc2022_64"
Write-Host ""
Write-Host "Terminé. Avant de compiler, dans une « Developer PowerShell for VS 2022 » :"
Write-Host "  `$env:CMAKE_PREFIX_PATH = '$qtPath'"
Write-Host "  `$env:PATH = '$qtPath\bin;' + `$env:PATH"
Write-Host "  .\scripts\build.ps1"
