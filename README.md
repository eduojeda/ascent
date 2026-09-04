# Ascent

A two-player arena game written in 2002 for Classic Mac OS, ported to
modern macOS in 2026. Catch the ball, throw it into your opponent's goal,
and use missiles, rockets, and powerups to make their day worse.

![gameplay](docs/screenshot.png)

## Build and run

```sh
brew install sdl3 sdl3_image pkgconf
make            # builds ./ascent (run it from the repo root)
make bundle     # builds Ascent.app
```

The prebuilt `assets/` are checked in. To rebuild them from the original
art (`brew install netpbm`, plus a Python venv in `tools/.venv` with
pillow): `make assets`.

## Controls

|                    | Left player (blue) | Right player (red) |
|--------------------|--------------------|--------------------|
| Move               | W A S D            | Arrow keys         |
| Shoot / release    | Control            | Space              |
| Powerup            | Tab                | Option             |
| Turn around        | Shift              | Command            |

`P` pauses, `Esc` quits the match, `Cmd+Return` toggles fullscreen.
Keys are rebindable in Settings. (The right player's Command/Option
defaults are the 2002 ones; rebinding them away from the system modifiers
is kinder on a modern Mac.)

The play area defaults to the largest preset that fits your display
(800x600 minimum, the 2002 size) and can be changed in Settings.
Settings and key bindings persist in
`~/Library/Application Support/Ascent/prefs.txt`.

## About the port

The original was built with CodeWarrior against the Mac Toolbox
(QuickDraw, resource forks) and Ingemar Ragnemalm's Sprite Animation
Toolkit. The port keeps the 2002 game code — physics, gameplay, sprite
logic — intact and replaces the platform underneath:

- `src/compat/` reimplements the slice of the SAT API and QuickDraw the
  game uses, on SDL3. Dirty-rect animation became full-frame
  recomposition at the configured play-area size (the 2002 code already
  derived every position from the SAT screen-size globals, so larger
  arenas just work; the starfield is tiled rather than stretched).
- `src/main.c` is new: SDL event loop, menu, settings, pause.
- The assets are the ORIGINALS, recovered from the 2002 release app's
  resource fork. The fork survived inside `Juego PPC.zip` — a Finder-made
  backup of the old dev machine whose `__MACOSX` AppleDouble entries
  carried it through years on a Windows disk. `tools/extract_rsrc.py`
  parses the fork (all 43 `snd ` sounds to WAV, 140 `cicn` sprite faces
  with their masks, all `PICT`s); `tools/install_original_assets.py`
  installs them into `assets/`.
- Before the archive turned up, the assets were reconstructed from the
  art sources in `Development Stuff/` (`tools/build_assets.py`) with
  synthesized placeholder sounds (`tools/gen-sounds.sh`); those tools
  remain as the fallback path — `make assets` runs the full chain,
  originals winning.

One genuine 2002 bug fixed: `SetupBall` dereferenced `g.ball` before it
was assigned — Classic Mac OS silently allowed the nil write; modern
macOS does not.

The untouched originals (source, CodeWarrior project, the PowerPC
binary) live at the repository root and in `Ascent v1.0.1*/`.
