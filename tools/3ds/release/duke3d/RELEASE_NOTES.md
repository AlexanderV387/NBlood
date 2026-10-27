Duke Nukem 3D for the New Nintendo 3DS, on EDuke32 (from the [NBlood](https://github.com/NBlood/NBlood) repository), with the status bar and the map on the bottom screen and an optional GPU renderer.

**New 3DS and New 2DS XL only.** Developed and tested on a New 3DS. It was neither designed nor tested for the Old 3DS or 2DS, where it may run very slowly, run out of memory, or not start.

## Downloads

| File | Use |
|---|---|
| `duke3d.cia` | Install with FBI; it appears on the HOME Menu |
| `eduke32.3dsx` | Copy to `/3ds/` and open it from the Homebrew Launcher |

Copy `DUKE3D.GRP` to `/3ds/eduke32/` (game data is not included: use your own copy, or the shareware episode's `DUKE3D.GRP`, which is free).

## Features

- **Two renderers**, in Options > Nintendo 3DS:
  - **Software** (the default): the original look.
  - **GPU**: EDuke32's Polymost (OpenGL) on the 3DS's GPU, through a layer that implements OpenGL on citro3d. At 800x240, the top screen's wide mode (not on the original 2DS), or 400x240.
- **Bottom screen:** the status bar (above or below the map), the map of what was explored with zoom buttons, the on-screen keyboard, and optional touch buttons for the weapons and the usable items. "Classic HUD" keeps the status bar on the top screen.
- **Controls:** 3DS defaults (R/ZR fire, A open, B run, L jump, ZL crouch, X/Y weapons, SELECT map, inventory on the D-pad), touch turning, three run modes, and a 3DS Buttons page to change them.
- **Cheats** typed on the on-screen keyboard; the Cheats menu lists them all.
- Settings, saves and the log are kept in `/3ds/eduke32/`.

## Known issues

- In GPU mode, Duke's reflection does not appear in mirrors.
- GPU mode can run below 60 FPS in large areas.

Port for [n3ds-ports](https://github.com/AlexanderV387/n3ds-ports): direction, design and hardware testing by AlexanderV387; code written with Claude (AI). Original engine: EDuke32 (Richard "TerminX" Gobeille, Evan "Hendricks266" Ramos and contributors) and NBlood (Nuke.YKT and contributors), based on Ken Silverman's Build engine and the Duke Nukem 3D source code released by 3D Realms; GPL-2.0, with Build's own license for the engine.
