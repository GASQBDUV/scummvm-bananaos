#!/bin/bash
# PORTMASTER: scummvm.zip, ScummVM.sh
# ScummVM on one panel (the top one), without the second screen. See ScummVM/README.txt.

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

> "$GAMEDIR/log.txt"

# register the games found under /roms/scummvm (already known ones are skipped)
if [ -d /roms/scummvm ]; then
    ./scummvm --config="$GAMEDIR/scummvm.ini" --joystick=-1 --add --recursive --path=/roms/scummvm >> "$GAMEDIR/log.txt" 2>&1
fi

$GPTOKEYB "scummvm" -c "$GAMEDIR/scummvm.gptk" &
./scummvm --config="$GAMEDIR/scummvm.ini" --joystick=-1 --extrapath="$GAMEDIR/extra" --path=/roms/scummvm >> "$GAMEDIR/log.txt" 2>&1
kill -9 $(pidof gptokeyb) 2>/dev/null
sync
printf "\033c" >> /dev/tty1
