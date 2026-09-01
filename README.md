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

## About the port

The original was built with CodeWarrior against the Mac Toolbox
(QuickDraw, resource forks) and Ingemar Ragnemalm's Sprite Animation
Toolkit. The port keeps the 2002 game code — physics, gameplay, sprite
logic — intact and replaces the platform underneath:

- `src/compat/` reimplements the slice of the SAT API and QuickDraw the
  game uses, on SDL3. Dirty-rect animation became full-frame
  recomposition at 800x600 (the original resolution).
- `src/main.c` is new: SDL event loop, menu, settings, pause.
- The resource forks (all compiled resources) were lost when the files
  left HFS around 2012. Sprites and pictures were rebuilt from the
  Photoshop/PICT sources in `Development Stuff/` by
  `tools/build_assets.py`; art with no surviving source (explosions,
  smoke, sparks, digits, bullets, countdown text) is regenerated
  procedurally.
- All 43 original sounds are gone — including the Beavis & Butthead and
  Simpsons clips behind the "Dirty Words" toggle. `tools/gen-sounds.sh`
  synthesizes placeholders (macOS `say` stands in for the voices). If a
  copy of the original release archive ever turns up, the real sounds
  can be dropped into `assets/sounds/`.

One genuine 2002 bug fixed: `SetupBall` dereferenced `g.ball` before it
was assigned — Classic Mac OS silently allowed the nil write; modern
macOS does not.

The untouched originals (source, CodeWarrior project, the PowerPC
binary) live at the repository root and in `Ascent v1.0.1*/`.
