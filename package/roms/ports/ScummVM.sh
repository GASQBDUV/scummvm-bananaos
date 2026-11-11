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

# register the games found under /roms/scummvm (already known ones are skipped). Looking through every game folder takes
# a while, so it only runs when something in there is newer than the last scan
if [ -d /roms/scummvm ] && { [ ! -f "$GAMEDIR/.scanned" ] || [ -n "$(find /roms/scummvm -newer "$GAMEDIR/.scanned" 2>/dev/null | head -n 1)" ]; }; then
    ./scummvm --config="$GAMEDIR/scummvm.ini" --joystick=-1 --add --recursive --path=/roms/scummvm >> "$GAMEDIR/log.txt" 2>&1
    touch "$GAMEDIR/.scanned"
fi

# ScummVM draws the launcher at 960x720 (scale factor 3), so the GUI is scaled up to a readable size, and the theme is
# forced because ScummVM rewrites these two keys when it exits
[ -f "$GAMEDIR/scummvm.ini" ] || echo '[scummvm]' >"$GAMEDIR/scummvm.ini"
for kv in gui_scale=150 gui_theme=scummbanana; do
    if grep -q "^${kv%%=*}=" "$GAMEDIR/scummvm.ini"; then sed -i "s/^${kv%%=*}=.*/$kv/" "$GAMEDIR/scummvm.ini"
    else sed -i "/^\[scummvm\]/a $kv" "$GAMEDIR/scummvm.ini"; fi
done
# gptokeyb: the pointer map, switched to the keyboard map (scummvm-keys.gptk) when a game that needs the arrow keys starts
start_keys() { kill -9 $(pidof gptokeyb) 2>/dev/null; $GPTOKEYB "scummvm" -c "$GAMEDIR/$1" & }
start_keys scummvm.gptk
(tail -n 0 -F "$GAMEDIR/log.txt" 2>/dev/null | while read -r line; do
    case "$line" in
    *"engine ID 'agi'"*|*"engine ID 'adl'"*|*"engine ID 'hugo'"*) start_keys scummvm-keys.gptk ;;
    *"engine ID '"*) start_keys scummvm.gptk ;;
    esac
done) &
WATCHER=$!
./scummvm --config="$GAMEDIR/scummvm.ini" --joystick=-1 --filtering --scaler=normal --scale-factor=3 --extrapath="$GAMEDIR/extra" --themepath="$GAMEDIR/extra" --path=/roms/scummvm >> "$GAMEDIR/log.txt" 2>&1
kill $WATCHER 2>/dev/null; kill $(pidof tail) 2>/dev/null
kill -9 $(pidof gptokeyb) 2>/dev/null
sync
printf "\033c" >> /dev/tty1
