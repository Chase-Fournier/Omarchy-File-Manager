# CLAUDE.md

Working notes for omafile. The spec is [omafile-plan.md](omafile-plan.md) — read it first;
this file records what is actually built, what was decided along the way, and what still
needs checking.

## Commands

```bash
./bin/build                        # qmake6 && make -> build/omafile
./bin/test                         # unit tests + the §12 startup budget check
./build/omafile                    # run
./build/omafile --dump-theme       # print the resolved palette, headless (key<TAB>value)
./build/omafile --dump-theme --file <colors.toml>   # resolve a specific theme file

OMAFILE_TRACE_STARTUP=1    ./build/omafile   # print ms to first painted frame
OMAFILE_TRACE_STARTUP=quit ./build/omafile   # print it and exit — how bin/test measures
```

## Layout

```
src/main.cpp      entry point, --dump-theme, startup tracing
src/theme.h/.cpp  Omarchy theme parsing + hot reload
src/qml/Main.qml  the window (embedded via src/resources.qrc)
tests/            Qt Test suite
```

---

## Progress

### M0 — Skeleton ✅

Repo laid out per §2, qmake6 build with `qtquickcompiler`, `Theme` class, themed empty
window, test suite, startup budget enforced by `bin/test`.

**Startup: median 109 ms over 7 runs (budget 120 ms).** Range across sessions 74–155 ms;
the spread is compositor contention, not our code, which is why `bin/test` asserts the
median rather than the max.

---

## Decisions

**Omarchy Quattro theme format — resolves open decision §16.1.**
Checked `origin/quattro` (v4.0.0.alpha, actively committed to). Quattro deleted the
per-app theme files (`alacritty.toml`, `waybar.css`, `hyprland.conf`, …). A theme is now:

```
colors.toml  shell.lock.toml  icons.theme  neovim.lua  vscode.json  keyboard.rgb  backgrounds/
```

`colors.toml` is the structured source the plan hoped for — no more scraping terminal
configs. Semantic keys: `mode`, `accent`, `selection`, `muted`, `background` (+`dark_`/
`darker_`/`lighter_` variants), `foreground` (+`dark_`/`light_`/`bright_`), and the ANSI
color names with `bright_` variants.

**The current-theme path moved.** Quattro reads
`~/.local/state/omarchy/current/theme/`; Omarchy 3.x used `~/.config/omarchy/current/theme/`.
`Theme` checks both, Quattro first, so one binary works on either.

**`Theme` is a line-by-line port of omarchy's `bin/omarchy-theme-color`.**
That script is the shared resolver every themed app goes through, and it carries a real
alias/fallback cascade for themes that only define a partial key set. Reimplementing it
loosely would have made omafile the one app whose colors are subtly wrong. Verified by
diffing `omafile --dump-theme` against the script itself: **byte-identical across all 22
Quattro themes and the legacy 3.x ANSI format.** If that script changes upstream, re-run
the sweep and re-port.

**Hot reload watches the theme directory's *parent*.** `omarchy-theme-set` does
`rm -rf current/theme && mv current/next-theme current/theme`, which destroys any
`QFileSystemWatcher` registered on the theme dir or on `colors.toml` itself. Watching
`current/` survives the swap and also catches the `theme.name` write that marks it
finished. Covered by `reloadsAcrossAtomicThemeSwap`.

**Theme I/O happens on the GUI thread — a deliberate exception to §3's "GUI thread never
touches the filesystem".** It is one ~500-byte read that must complete before first paint.
The rule stands for everything else.

**QML singleton via `qmlRegisterSingletonInstance`, not a context property.** Context
properties can't be resolved at compile time and would defeat `qmlcachegen`.

**Startup measurement is real, not estimated.** `main.cpp` times from process entry to the
first `QQuickWindow::frameSwapped`. The handler is queued onto the GUI thread — a
`DirectConnection` runs on the render thread and `app.quit()` from there does nothing
(cost me one hung process).

**Material style kept but currently free.** Measured with and without
`QQuickStyle::setStyle("Material")` + linking `quickcontrols2`: 86 ms vs 85.5 ms median —
no difference, because `Main.qml` imports only `QtQuick`, so the style plugin never loads.
Re-measure the moment QML actually imports `QtQuick.Controls`.

---

## To verify

**Startup headroom is thin.** Median 109 ms against a 120 ms budget, and M1 adds a
ListView, a delegate, and possibly `QtQuick.Controls`. Re-measure after every M1 commit;
if it goes over, the first things to try are dropping Controls entirely (the §4 design has
no toolbar, no menus, and custom rows — it may never need them) and deferring the font
lookup.

**Quattro paths are only tested against synthetic fixtures.** This machine runs Omarchy
3.8.3, so `~/.local/state/omarchy/current/theme/` never exists here in practice. The
precedence logic is unit-tested, but confirm against a real Quattro install before
shipping.

**Quattro is alpha and moving.** Last commit on `origin/quattro` was the same day this was
written. Re-check `bin/omarchy-theme-color` and the theme file list before M6.

**`shell.lock.toml` vs `shell.toml`.** Themes ship `shell.lock.toml`, but
`omarchy-theme-set` base64s `$CURRENT_THEME_PATH/shell.toml` into the shell IPC payload.
Unclear whether one is generated from the other. Irrelevant to colors, but worth
understanding if omafile ever needs to match shell chrome (borders, radii).

**`accent` has no entry in omarchy's cascade.** Every Quattro theme defines it explicitly,
and the script's callers pass a per-call-site fallback. `Theme::accent()` falls back to
`blue`; confirm that matches what the shell does for a theme that omits it.

**Nerd Font glyphs at 32 px on HiDPI — open decision §16.7, not yet evaluated.**
`CaskaydiaMono Nerd Font` is installed and renders, but no file-type glyphs exist yet to
judge. Decide during M1.

**The window tiles.** §13 wants a float rule at ~1200×800. That is Hyprland config, so it
lands in M6 with packaging, but it means the current window fills the screen when tested.

---

## Conventions

- C++17, Qt 6, qmake6. No CMake.
- Comments explain *why*, and mostly appear where behavior is non-obvious or mirrors an
  external contract (the theme cascade, the atomic-swap watch). Don't narrate the code.
- Shell scripts in `bin/` follow Omarchy's own style: `#!/bin/bash`, two-space indent,
  `[[ ]]` for strings, `(( ))` for numbers.
- Tests run headless (`QT_QPA_PLATFORM=offscreen`) and use a fake `$HOME` so they never
  read the developer's real Omarchy install.
