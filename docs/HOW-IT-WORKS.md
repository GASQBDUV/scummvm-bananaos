# How the second screen works

ScummVM is not modified. `banana-second.so` is loaded into it with `LD_PRELOAD` (see `ScummVM Two Screens.sh`) and replaces a handful
of SDL functions. Everything it needs it takes from the calls ScummVM already makes.

## Why a preloaded library and not a ScummVM backend

bananaOS' SDL has a mode that puts one window over both panels, but only for the software `evdev` driver, not for KMSDRM with
OpenGL. ScummVM's SDL backend (`SurfaceSdlGraphicsManager`) draws the whole picture into a software surface and uploads it as
one streaming texture, so the finished frame is already in RAM. No GPU read-back is needed to copy it; the copy plus the
scaling to the bottom panel takes about 1.5 ms per frame.

## What it hooks

| Function | Used for |
|---|---|
| `SDL_UpdateTexture` | copies the finished frame (RGB565, the whole picture: game, menus, pointer) |
| `SDL_RenderCopy` | learns where the window shows that texture; in verb and zoom mode it changes which part of the texture the **top** screen shows |
| `SDL_RenderPresent` | after the real present, draws the bottom screen (at most one frame per 40 ms) |
| `SDL_SetWindowTitle` | the running game's name; decides if the verb screen is available (`banana-split.txt`) |
| `SDL_PollEvent` | touch rewriting, the mode and zoom keys, the pointer return, the remote-control files |

## The bottom screen

The same method as other bananaOS companion-screen ports: SDL's KMSDRM driver holds the DRM file descriptor (found by looking for
`/dev/dri/card*` in `/proc/self/fd`). With it the connector `DSI-1` gets a CRTC that the top panel does not use, a dumb buffer in
`XBGR8888`, and a mode of its own, and the library scales the frame into that buffer (nearest neighbour, from a 64K-entry
RGB565 lookup table). If anything in that set-up fails, the library switches itself off and ScummVM runs as usual on the top screen.

`run-port` turns the bottom backlight off for one-panel ports; the launcher turns it back on (`one_panel_dark top; one_panel_restore`).

## Modes

- **Mirror**: the bottom screen shows the frame at the same place and size as the window. SDL's touch positions are fractions of the
  window, so a tap lands where it is drawn.
- **Verb screen**: the top screen shows the first *N* of the 200 game lines (`banana-split.txt`), the bottom screen the rest,
  enlarged to the width of the panel. Finger events are rewritten so that a tap on the enlarged part reaches ScummVM as a tap on the
  matching point of the real picture. After a tap the pointer is put back where the stick had left it, and one click is made there
  (only if it was over the room), because ScummVM decides what a click means from the position it reads on its next tick, not
  at the moment of the press.
- **Zoom**: the top screen shows a window of the texture (1.5x, 2x or 3x). The window stays still until the pointer comes within a fifth
  of its edge, then it follows. The bottom screen shows the whole frame with a yellow frame around the visible part. Touch and stick both
  move the window, since touches reach ScummVM as finger events and the library listens to those too.

## Environment variables

| Variable | Default | |
|---|---|---|
| `BANANA_SECOND` | on | `0` switches the library off |
| `BANANA_SECOND_MS` | 40 | least time between two copies to the bottom screen |
| `BANANA_PANEL_ID` | 1 | which `DSI-n` connector is the second panel |
| `BANANA_SPLIT_FILE` | none | the list of games with a verb screen (the launcher sets it) |
| `BANANA_KEEP_POINTER` | on | `0`: a tap in the verb screen leaves the pointer where the tap was |
| `BANANA_AUTOCLICK` | on | `0`: no automatic click after the pointer is put back |
| `BANANA_RETURN_MS` | 250 | how long the pointer stays on the tapped verb before it returns |

## Debugging helpers

The library looks for three files in `/tmp` about every 150 ms (create them over SSH while a game runs):

- `/tmp/banana-dump`: writes the next game frame to `/tmp/banana-frame.ppm`, to see where a game's interface starts.
- `/tmp/banana-mode`: contains `MODE` or `MODE ZOOM` (`MODE` 0 mirror, 1 verb screen, 2 zoom; `ZOOM` 0, 1, 2 for 1.5x, 2x, 3x): switches mode.
- `/tmp/banana-shot`: writes both panels as they are drawn to `/tmp/banana-top.ppm` and `/tmp/banana-bottom.ppm` (this is how the
  screenshots in this repository were made; the pointer is moved one pixel a few times to make ScummVM redraw a still picture).

Every line the library writes to ScummVM's log starts with `BANANA-SECOND:` (in `roms/ports/ScummVM/log.txt`).

## Limits

- It relies on ScummVM's SDL graphics manager uploading an RGB565 streaming texture. Other ScummVM versions, or the OpenGL renderer
  (not built here), would need changes.
- The verb screen cuts the picture at a fixed game line. A game whose interface is not at the bottom (or that changes layout in
  the middle of the game) is not handled; zoom still works.
- While ScummVM's own menu is open in the verb screen only the upper part is on the top screen. Press R2 to go back to mirror.

## On-screen keyboard

ScummVM is built with `--enable-vkeybd`; its keyboard pictures are found through `--extrapath` (`roms/ports/ScummVM/extra/`).
ScummVM opens the keyboard on Ctrl+F7. gptokeyb can only send single keys, so `scummvm.gptk` maps Select (named `back` in gptokeyb;
there is no `select`) to F7 and the shim adds Ctrl to that key press with `SDL_SetModState`, because ScummVM reads the modifiers
from SDL, not from the event.
