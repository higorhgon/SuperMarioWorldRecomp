#!/bin/bash
# PortMaster launcher for SuperMarioWorldRecomp.
#
# Target hardware: Anbernic H700-chip handhelds (RG34XX / RG34XX H /
# RG34XXSP), 720x480 physical panel, running muOS. See the "PortMaster /
# Anbernic H700 (muOS)" section of the project README for how this port is
# built and packaged, and for what has and has not been verified on real
# hardware.
#
# This script is UNTESTED ON REAL HARDWARE. It follows the standard
# PortMaster port-script convention (GRANDPARENTDIR/control.txt/get_controls)
# used across other PortMaster ports, but nobody has run it on an actual
# RG34XX yet. Please report back what works and what doesn't.

GRANDPARENTDIR="/$(dirname "$(dirname "$(readlink -f "$0")")")"
controlfolder="$GRANDPARENTDIR/PortMaster"
[ -f "${controlfolder}/control.txt" ] && source "$controlfolder/control.txt" "$0"
get_controls

GAMEDIR="/$(dirname "$(readlink -f "$0")")/SuperMarioWorldRecomp"
cd "$GAMEDIR" || exit 1
echo "$GAMEDIR" > "$controlfolder/lastgame.txt"
mkdir -p "$GAMEDIR/saves"

# --- Video backend ---------------------------------------------------------
#
# The H700's Mali-G31 runs on Mesa's mainline Panfrost driver under muOS (not
# a Batocera-style vendor "mali" blob), so SDL's own driver probing should
# land on the correct backend (kmsdrm on a bare console session, or x11/
# wayland if muOS's PortMaster session happens to run one) without help.
# Real PortMaster ports disagree in practice - some hardcode
# SDL_VIDEODRIVER=x11, others hardcode kmsdrm, others only override for a
# detected vendor "mali" driver - and which is right depends on exactly how
# muOS starts a port on this specific device, which cannot be checked from
# this sandbox. So: leave it unset (SDL default) unless you hit a black
# screen or "no available video device" on real hardware, in which case
# uncomment ONE of the lines below.
# export SDL_VIDEODRIVER=kmsdrm
# export SDL_VIDEODRIVER=x11

# config.ini (Fullscreen/OutputMethod/WindowSize/...) and mods/state.toml
# (the SMW Adaptive Widescreen "Fit to screen" mode) are otherwise resolved
# next to the executable (snesrecomp_exe_dir_path() anchors there when
# $APPIMAGE is unset, which is the case for this plain extracted binary), but
# `cd "$GAMEDIR"` above and the explicit --config flag below make that
# unambiguous no matter how muOS invokes this script (symlink, wrapper, or a
# working directory PortMaster changed).

# --- ROM discovery ----------------------------------------------------------
#
# Mirrors the official Linux AppImage's own ROM auto-detection (tools/
# build-linux.sh's AppRun): look for a ROM the user dropped into the port's
# folder and cache its path in rom.cfg, which the game's own launcher UI
# reads on startup. This is the same code path the upstream Linux build
# already exercises, rather than passing the ROM on the command line (a
# different, less-tested code path in src/main.c that also skips the GUI
# launcher entirely).
ROM=""
for ext in sfc smc; do
  for file in "$GAMEDIR"/*."$ext"; do
    [ -e "$file" ] && ROM="$file" && break 2
  done
done
if [ -n "$ROM" ]; then
  cached=""
  [ -f "$GAMEDIR/rom.cfg" ] && cached="$(head -n1 "$GAMEDIR/rom.cfg" 2>/dev/null | tr -d '\r\n')"
  if [ -z "$cached" ] || [ ! -f "$cached" ]; then
    printf '%s\n' "$ROM" > "$GAMEDIR/rom.cfg" 2>/dev/null || true
  fi
fi

./SuperMarioWorldSNESRecomp --config "$GAMEDIR/config.ini" > "$GAMEDIR/log.txt" 2>&1
printf "\033c" > /dev/tty0 2>/dev/null || true
