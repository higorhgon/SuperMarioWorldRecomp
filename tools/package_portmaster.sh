#!/usr/bin/env bash
# Assemble a PortMaster-ready zip for SuperMarioWorldRecomp, for the Anbernic
# H700-chip handhelds (RG34XX / RG34XX H / RG34XXSP) running muOS.
#
# This reuses tools/build-linux.sh for the actual compile (it already knows
# how to configure/build the game, stage the launcher's assets/, and stage
# the release-owned mod catalog) rather than duplicating that logic. It does
# NOT ship the .AppImage tools/build-linux.sh produces: AppImages need FUSE
# (or squashfuse) to mount at runtime, and minimal handheld CFW images like
# muOS often don't ship it. Instead this script runs
# `tools/build-linux.sh --no-package` to get a plain built tree (ELF +
# assets/ + mods/), then lays that out the way PortMaster expects - a zip
# root containing port.json, SuperMarioWorldRecomp.sh, and a
# SuperMarioWorldRecomp/ folder holding the binary and its files - confirmed
# against PortsMaster/PortMaster-New's own published ports.
#
# You still need your own legally dumped Super Mario World (USA) ROM and an
# aarch64 build environment (the RG34XX family is Cortex-A55, so an aarch64
# host or a cross-compiling toolchain both work) - this script does not
# obtain either. See the "PortMaster / Anbernic H700 (muOS)" section of
# README.md for the full local workflow:
#
#   1. git submodule update --init --recursive
#   2. put your verified ROM at smw.sfc, then:
#      bash tools/build-linux.sh --regen --no-package --out build-portmaster
#      (on an aarch64 host, or cross-compiling for aarch64 - see README)
#   3. bash tools/package_portmaster.sh --build build-linux-prod
set -euo pipefail

APP_NAME="SuperMarioWorldRecomp"
CMAKE_TARGET="SuperMarioWorldSNESRecomp"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$REPO/build-linux-prod"
OUT="$REPO/release-portmaster"
VERSION="dev"
SKIP_ARCH_CHECK=0

while [ $# -gt 0 ]; do
  case "$1" in
    --build) BUILD="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    --version) VERSION="$2"; shift 2;;
    --skip-arch-check) SKIP_ARCH_CHECK=1; shift;;
    -h|--help)
      sed -n '1,25p' "$0"
      exit 0
      ;;
    *) echo "unknown arg: $1" >&2; exit 2;;
  esac
done

command -v zip >/dev/null 2>&1 || { echo "package_portmaster.sh: 'zip' is required" >&2; exit 1; }

echo "[1/5] locate the built binary"
BIN=""
while IFS= read -r file; do
  if [ "$(basename "$file")" = "$CMAKE_TARGET" ] && file -b "$file" 2>/dev/null | grep -q "ELF.*executable"; then
    BIN="$file"
    break
  fi
done < <(find "$BUILD" -maxdepth 4 -type f 2>/dev/null)
[ -n "$BIN" ] || {
  echo "package_portmaster.sh: no Linux ELF named $CMAKE_TARGET found under $BUILD" >&2
  echo "  Build it first, e.g.:" >&2
  echo "    bash tools/build-linux.sh --regen --no-package --out $BUILD" >&2
  echo "  (see README's PortMaster / Anbernic H700 section)" >&2
  exit 1
}
echo "  found: $BIN"

if [ "$SKIP_ARCH_CHECK" = "0" ]; then
  ARCH_INFO="$(file -b "$BIN" 2>/dev/null || true)"
  case "$ARCH_INFO" in
    *aarch64*|*ARM\ aarch64*) echo "  arch: aarch64 (matches the H700's Cortex-A55 cores)";;
    *)
      echo "package_portmaster.sh: $BIN does not look like an aarch64 binary ($ARCH_INFO)" >&2
      echo "  The H700 (RG34XX family) needs an aarch64 build. Pass --skip-arch-check to" >&2
      echo "  package anyway (e.g. while testing the zip layout on a desktop build)." >&2
      exit 1
      ;;
  esac
fi

BIN_DIR="$(dirname "$BIN")"

echo "[2/5] stage the port layout"
WORK="$(mktemp -d)"
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT
STAGE="$WORK/stage"
GAMEDIR="$STAGE/$APP_NAME"
mkdir -p "$GAMEDIR/saves"

cp "$REPO/portmaster/port.json" "$STAGE/port.json"
cp "$REPO/portmaster/$APP_NAME.sh" "$STAGE/$APP_NAME.sh"
chmod +x "$STAGE/$APP_NAME.sh"

cp "$BIN" "$GAMEDIR/$CMAKE_TARGET"
chmod +x "$GAMEDIR/$CMAKE_TARGET"

echo "[3/5] stage assets, mod catalog and default config"
if [ -d "$BIN_DIR/assets" ]; then
  cp -r "$BIN_DIR/assets" "$GAMEDIR/assets"
else
  echo "package_portmaster.sh: warning: no assets/ directory beside the binary" >&2
  echo "  (the recomp-ui launcher will not render without it)" >&2
fi

# The release-owned mod catalog (widescreen, MSU-1, etc.) that build-linux.sh
# stages beside the built ELF - carry it into the port so the packaged
# default mods/state.toml below actually has something to select.
if [ -d "$BIN_DIR/mods/packages" ]; then
  mkdir -p "$GAMEDIR/mods"
  cp -r "$BIN_DIR/mods/packages" "$GAMEDIR/mods/packages"
else
  echo "package_portmaster.sh: warning: no mods/packages/ beside the binary" >&2
  echo "  falling back to the tracked portmaster/default-config copy" >&2
  mkdir -p "$GAMEDIR/mods"
  cp -r "$REPO/portmaster/default-config/mods/packages" "$GAMEDIR/mods/packages"
fi

# Ships mods/state.toml with the SMW Adaptive Widescreen mod already enabled
# and set to "Fit to screen" (mode/spawn = adaptive), and config.ini with
# Fullscreen=1 - so a first launch on the H700's 720x480 panel is already
# fullscreen and correctly 3:2 without visiting the launcher's Mods page
# first. See docs/adaptive-renderer.md and portmaster/default-config/.
cp "$REPO/portmaster/default-config/mods/state.toml" "$GAMEDIR/mods/state.toml"
cp "$REPO/portmaster/default-config/config.ini" "$GAMEDIR/config.ini"

echo "[4/5] build the zip"
mkdir -p "$OUT"
ZIP="$OUT/SuperMarioWorldRecomp-portmaster-$VERSION.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -r -X "$ZIP" . >/dev/null)

echo "[5/5] done"
echo "  $ZIP"
echo
echo "This zip has NOT been tested on real hardware. Install it through muOS's"
echo "PortMaster app (or by extracting it into the SD card's PortMaster ports"
echo "folder), drop your own Super Mario World (USA) .sfc/.smc ROM into the"
echo "SuperMarioWorldRecomp folder it creates, and please report back what does"
echo "and doesn't work."
