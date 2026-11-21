# Guide de compilation

StudyLab se compile avec **CMake ≥ 3.25**, un compilateur **C++20** et **Qt 6** (6.8 LTS recommandé, 6.4 minimum).
Les mêmes commandes sont exécutées par l'intégration continue à chaque modification (`.github/workflows/`).

## Debian 13 (trixie)

```sh
./scripts/install-deps-debian.sh      # paquets Qt 6, CMake, Ninja, compilateur
./scripts/build.sh                    # configure + compile + tests (preset debian-release)
./build/debian-release/src/app/studylab
```

Autres presets : `debian-debug`, `debian-asan` (AddressSanitizer + UBSan).

## Windows 10/11 x64

Prérequis : Visual Studio 2022 (charge « Développement Desktop en C++ ») et Python 3.

```powershell
.\scripts\install-deps-windows.ps1    # CMake, Ninja (winget) et Qt 6.8.3 MSVC 2022 x64 (aqtinstall)
# Ouvrir « Developer PowerShell for VS 2022 », puis :
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.3\msvc2022_64"
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
.\scripts\build.ps1                   # preset windows-msvc-release
.\build\windows-msvc-release\src\app\studylab.exe
```

## Paquets

```sh
# Debian 13 : paquet .deb (dépendances calculées par dpkg-shlibdeps)
cmake --preset debian-release && cmake --build --preset debian-release
cpack --config build/debian-release/CPackConfig.cmake -B dist
sudo apt install ./dist/studylab_*_amd64.deb
```

```powershell
# Windows : dossier autonome puis installateur Inno Setup 6
cmake --install build/windows-msvc-release --prefix stage
windeployqt --release --no-system-d3d-compiler --no-opengl-sw stage/studylab.exe
iscc /DAppVersion=1.1.0 /DSourceDir=$PWD\stage /O$PWD\dist packaging\windows\studylab.iss
```

Les deux paquets enregistrent le schéma `studylab://` (liens des exports Markdown). `studylab --version` affiche la
version et quitte : la CI s'en sert pour vérifier qu'un paquet installé se lance.

## Tests

```sh
ctest --preset debian-release         # ou windows-msvc-release
```

Les tests d'interface tournent sans écran (`QT_QPA_PLATFORM=offscreen`). Pour enregistrer des captures d'écran
pendant les tests : `STUDYLAB_SCREENSHOT_DIR=/tmp/captures ctest --preset debian-release -R mainwindow`.

## Données de l'application

| Système | Dossier |
|---|---|
| Debian | `~/.local/share/StudyLab/` |
| Windows | `%LOCALAPPDATA%\StudyLab\` |

La variable `STUDYLAB_DATA_DIR` permet d'utiliser un autre dossier (tests, installation portable).
