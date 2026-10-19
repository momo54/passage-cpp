#!/usr/bin/env bash
#
# What QLever needs on Ubuntu, taken from its own CI
# (`qlever/.github/workflows/install-dependencies-ubuntu`) rather than guessed.
#
# Differences from the Passage fork's container, and why:
#
#   * No JDK. Nothing here runs on a JVM.
#   * `libjemalloc-dev` IS installed. The Passage fork leaves it out because
#     jemalloc's per-thread cleanup crashes a JVM that has its own allocator;
#     there is no JVM here, so the allocator QLever prefers is the right one.
#
# Runs as `onCreateCommand`, so it has to stay idempotent.
set -euo pipefail

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO=sudo
export DEBIAN_FRONTEND=noninteractive

# shellcheck disable=SC1091
. /etc/os-release 2>/dev/null || true
echo "== ${PRETTY_NAME:-unknown OS}, $(uname -m), user $(id -un)"

if ! command -v apt-get >/dev/null 2>&1; then
    cat >&2 <<'MSG'
!! There is no apt-get here, so this is not the Ubuntu container that
!! .devcontainer/devcontainer.json asks for. In a Codespace that means the
!! container failed to build and you are in the recovery container: the
!! workspace is mounted but the image is not the one requested. The reason is
!! in /workspaces/.codespaces/.persistedshare/creation.log; fix it, then
!! "Codespaces: Rebuild Container".
MSG
    exit 1
fi

echo "== apt-get update"
$SUDO apt-get update -qq

echo "== git, before anything that can fail"
$SUDO apt-get install -y --no-install-recommends git ca-certificates

echo "== build tools"
$SUDO apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config mold

# Ubuntu 24.04 ships Boost 1.83, and QLever needs >= 1.81 for `Boost::url`, so
# the PPA its CI uses for older runners is unnecessary. Checked below rather
# than assumed, because an older base image passes apt and then fails the
# CMake configure with a message about `Boost::url`.
echo "== third-party libraries"
$SUDO apt-get install -y --no-install-recommends \
    libicu-dev libzstd-dev libssl-dev tzdata libboost-all-dev libjemalloc-dev

BOOST=$(dpkg-query -W -f='${Version}' libboost-dev 2>/dev/null || echo none)
case "$BOOST" in
    none)       echo "!! no libboost-dev" >&2; exit 1 ;;
    1.7*|1.80*) echo "!! Boost $BOOST has no Boost::url; QLever needs >= 1.81" >&2; exit 1 ;;
esac

echo "== boost $BOOST, cmake $(cmake --version | head -1 | cut -d' ' -f3), $(c++ --version | head -1)"
echo "== done; now run .devcontainer/build-and-test.sh"
