#!/bin/sh
# Installe les outils de compilation de StudyLab sur Debian 13 (trixie).
# Usage : ./scripts/install-deps-debian.sh [--yes]
set -eu

PACKAGES="build-essential cmake ninja-build pkg-config git \
qt6-base-dev qt6-base-dev-tools qt6-base-private-dev qt6-svg-dev qt6-pdf-dev \
qt6-webengine-dev qt6-webchannel-dev fonts-dejavu-core libgl-dev"

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
