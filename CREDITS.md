# Credits

- **The ScummVM team**: [ScummVM](https://www.scummvm.org/), GPL-2.0-or-later. This release runs upstream ScummVM, unmodified, built from
  [commit `ec80f5e`](https://github.com/scummvm/scummvm/tree/ec80f5e5ca334ea845b1e823aa3115245d64d279).
  All the games it can run belong to their publishers; none are included here.
- **bananaOS**: the operating system this is made for, and its two-panel display set-up.
- **[PolluxPT/ScummVM-RGDSPlus](https://github.com/PolluxPT/ScummVM-RGDSPlus)** (GPL-3.0): a ScummVM for the RG DS Plus on the stock firmware.
  Its approach of hooking the SDL calls with a preloaded library to mirror the picture on the second display, and of reading the
  touch screen directly, was the model for the mirror and the touch handling here. The code in this repository is written from scratch
  for bananaOS (a different display set-up); no code was copied.
- **glennjaume**'s [dual-screen Monkey Island pull request](https://github.com/glennjaume/scummvm/pull/1) (closed, not merged): the idea
  of keeping only the room on the main screen and moving the sentence line, verbs and inventory to the second screen. No code was used.
- **The ScummVM DS and 3DS ports**: the zoom mode (a magnified part of the picture on one screen, an overview on the other) and touch as
  "tap to click" follow what those ports do.
- **[PortMaster](https://portmaster.games/)**: `gptokeyb`, which turns the gamepad into keyboard and mouse, and the launcher conventions used here.
- The bottom-panel drawing through DRM is the technique first used in the Lighthouse companion screen for bananaOS
  (`BananaPanel.cpp`, from the same author as this repository).
