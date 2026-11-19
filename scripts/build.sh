#!/bin/sh
# Compile StudyLab et lance les tests (Debian).
# Usage : ./scripts/build.sh [debian-debug|debian-release|debian-asan]
set -eu
PRESET="${1:-debian-release}"
cd "$(dirname "$0")/.."
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
ctest --preset "$PRESET"
echo "Exécutable : build/$PRESET/src/app/studylab"
