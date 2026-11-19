#!/bin/sh
# Installe les outils de compilation de StudyLab sur Debian 13 (trixie).
# Usage : ./scripts/install-deps-debian.sh [--yes]
set -eu

PACKAGES="build-essential cmake ninja-build pkg-config git \
qt6-base-dev qt6-base-dev-tools qt6-svg-dev libgl-dev"

YES=""
[ "${1:-}" = "--yes" ] && YES="-y"

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi

$SUDO apt-get update
# shellcheck disable=SC2086
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install --no-install-recommends $YES $PACKAGES
echo "Dépendances installées."
