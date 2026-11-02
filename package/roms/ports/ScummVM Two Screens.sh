#!/bin/bash
# PORTMASTER: scummvm.zip, ScummVM Two Screens.sh
# ScummVM with a second screen on the bottom panel (banana-second.so): mirror, verb screen, zoom. See ScummVM/README.txt.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
controlfolder="/roms/ports/PortMaster"
[ -d "$XDG_DATA_HOME/PortMaster/" ] && controlfolder="$XDG_DATA_HOME/PortMaster"

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/ScummVM"
cd "$GAMEDIR"
chmod +x ./scummvm 2>/dev/null

export LD_LIBRARY_PATH="$GAMEDIR/lib:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export HOME="$GAMEDIR"

# run-port switched the bottom panel's backlight off for a one-panel port: this one uses both
. /usr/libexec/banana/one-panel
one_panel_dark top
one_panel_restore

> "$GAMEDIR/log.txt"

# register the games found under /roms/scummvm (already known ones are skipped)
if [ -d /roms/scummvm ]; then
    ./scummvm --config="$GAMEDIR/scummvm.ini" --joystick=-1 --add --recursive --path=/roms/scummvm >> "$GAMEDIR/log.txt" 2>&1
fi

# --joystick=-1: ScummVM's own game-controller handling is off, gptokeyb (scummvm.gptk) does all the input
$GPTOKEYB "scummvm" -c "$GAMEDIR/scummvm.gptk" &
BANANA_SPLIT_FILE="$GAMEDIR/banana-split.txt" LD_PRELOAD="$GAMEDIR/lib/banana-second.so" \
    ./scummvm --config="$GAMEDIR/scummvm.ini" --joystick=-1 --extrapath="$GAMEDIR/extra" --path=/roms/scummvm >> "$GAMEDIR/log.txt" 2>&1
kill -9 $(pidof gptokeyb) 2>/dev/null
sync
printf "\033c" >> /dev/tty1
