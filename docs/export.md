# Exporting a game

`interi_export` cooks a project into a self-contained **package** folder; `interi_player` is its runtime (the same engine
without the editor).

```sh
interi_export <project dir> <out dir> [--lib <dir>]... [--runtime <file or dir>]...
```

```
<out>/package.json            format, game name, engine version, toolchain fingerprint, every file's size + XXH3 hash
<out>/<game>.sh, <game>.bat   launchers (run bin/interi_player --package <out>)
<out>/bin/                    --runtime files: interi_player and the engine's shared libraries
<out>/game/project.json       the project; content folders; the project data file
<out>/game/modules/<id>/      each module of the resolved graph: module.json, its content, its library
<out>/game/cooked.ival        resolved Values, archetype layouts and table rows (values/ProjectData.h, "IVAL")
```

Export is all or nothing: the project must resolve (dependencies, versions, conflicts, Physics Engine), its data must fit
the modules' Values, the startup scene must parse and every library must be found (next to its `module.json`, else in a
`--lib` folder). A failure leaves the output as it was. Modules whose libraries were built with another toolchain than the
player's are caught when the player loads the package, not at export.

## Running

```sh
./coin_run.sh                                  # Linux; coin_run.bat on Windows
bin/interi_player --package . --verify-only    # check hashes and toolchain only
bin/interi_player --package . --steps 600 --press moveRight@0 --print com.interi.starter:coins
```

The player verifies the package (every file's size and hash, the toolchain), opens `game/` as a `GameSession` and plays
the startup scene. `--steps` runs as fast as possible (headless smoke runs, CI); `--realtime` paces to the fixed step;
`--press action@step` raises an input action; `--print id` prints a Value at the end; `--user-dir` is where save slots go.
Exit codes: 0 played, 1 the package didn't verify or the game didn't open, 2 usage.

`--window` (with `--video-driver` and `--frames`) opens an SDL window and plays in real time: input events go through the
game's `content/input.json` bindings into the chains until the window closes. Drawing the scene and HUD
(`hud::BuildFrame`, the forward renderer) is not in the player yet: a renderer plugs into the same loop.

## Installing the tools

`cmake --install` installs `interi_export` and `interi_player` next to the engine libraries (`bin/`, `lib/`). Copy the
player and the libraries it links into a package with `--runtime`.
