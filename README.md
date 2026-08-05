# omafile

**Open a folder in a blink, find anything by typing, act on it without touching the mouse — and drag it out when you want to.**

A dead-simple file manager for Omarchy, in the shape of omacut and omawrite.

![omafile](docs-screenshot.png)

## Why

Nautilus is the odd one out in Omarchy. It ignores the universal `Super+C/X/V` clipboard
convention, it can't be rebound, it's slow to open, and it doesn't look like the rest of
Quattro. omafile is the file manager that behaves like the rest of the desktop.

It reads Omarchy's theme and re-colours itself the instant you run `omarchy theme set`. It
opens in about 110 ms. It uses the same Nerd Font glyphs as your terminal. Every letter
key you press filters the directory, exactly like the launcher.

## Install

```bash
./bin/install
```

Then make it the default for folders, so every other application opens them here:

```bash
xdg-mime default omafile.desktop inode/directory
```

Hyprland users may want a float rule and `Super+F`:

```
bind = SUPER, F, exec, omafile
windowrulev2 = float, class:^(omafile)$
windowrulev2 = size 1200 800, class:^(omafile)$
windowrulev2 = center, class:^(omafile)$
```

## Use

```
omafile [path]           open a directory (default: $PWD)
omafile --select <file>  open its parent, with it selected
omafile --sidebar        start with the sidebar open   (--no-sidebar to force it closed)
omafile --preview        start with the preview open   (--no-preview likewise)
```

Without a flag it reopens the way you left it.

## Keys

Bare letters filter. That is the whole idea: the fastest path from "window open" to "file
selected" is to start typing its name. Everything else is modified or an arrow key.

| Key | Action |
|---|---|
| *any letter* | Filter this directory, fuzzily |
| `↑` `↓` · `Ctrl+K` `Ctrl+J` | Move |
| `←` `Backspace` | Parent directory (Backspace edits the filter first) |
| `→` `Enter` | Open |
| `Shift+Enter` | Open in a new window |
| `Ctrl+Enter` | Open with… |
| `Escape` | Clear filter → clear selection → close |
| `Home` `End` · `PgUp` `PgDn` | Jump |
| `Ctrl+F` | Find recursively from here |
| `Ctrl+Alt+F` | Search inside files |
| `Ctrl+L` | Edit the path |
| `Ctrl+B` | Sidebar |
| `Ctrl+P` | Preview pane |
| `Ctrl+H` | Hidden files |
| `Super+C` `Super+X` `Super+V` | Copy / cut / paste |
| `Ctrl+C` `Ctrl+X` `Ctrl+V` | The same, for muscle memory |
| `Ctrl+Shift+C` | Copy absolute path |
| `Space` | Add to selection (`Shift+↑↓` or `Shift+click` for a range) |
| `Ctrl+A` | Select all |
| `F2` `Ctrl+R` | Rename in place |
| `Ctrl+Shift+R` | Bulk rename in `$EDITOR` |
| `Delete` | Move to trash |
| `Shift+Delete` | Delete permanently |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+N` | New folder |
| `Ctrl+T` | Terminal here |
| `Ctrl+N` | New window |
| `Ctrl+D` | Bookmark here |
| `Ctrl+S` | Connect to… |
| `Ctrl+E` | Eject / unmount |
| `Ctrl+1` `Ctrl+2` `Ctrl+3` | Sort by name / size / time (again to reverse) |
| `F5` | Refresh |
| `Ctrl+?` | This table, in the app |

That table is generated from the same list that binds the keys, so it cannot drift.

## Mouse

The keyboard is the point, but the mouse is not second-class.

**Right-click** a file, the empty space below the list, the space beside the breadcrumb, or
a place in the sidebar. Entries that do not apply are greyed out rather than hidden, so the
menu never moves under you. Most entries are the same verbs the keys above run; a few —
"New file", and the ones that open a config — exist only here, because they have no
natural key. A sidebar entry offers the file that governs it: `~/.ssh/config` for a host,
`rclone config` for a remote, `config.toml` for anything else.

Clicking away closes the menu **and** does whatever that click would have done — a stray
right-click costs nothing.

**Drag** a file onto a folder to move it there, out to another application, or in from one.
Hold over a folder for a moment and it opens, so you can drag into a tree you cannot see;
hold over a **breadcrumb** and it goes back up, which is how you get out again — or drop
straight onto a crumb to send something several levels up without letting go. `Ctrl`
forces a copy, `Shift` forces a move; otherwise it moves within a filesystem and copies
across one.

## Remote

SSH hosts come from `~/.ssh/config` — omafile keeps no host list of its own, so keys,
agents, `ProxyJump` and tunnels all work because `ssh` already makes them work. Add a
host there and it appears in the sidebar:

```
Host box
    HostName box.example.com
    User chase
    IdentityFile ~/.ssh/id_ed25519
```

`Ctrl+S` also takes `ssh://`, `sftp://`, `smb://`, `davs://`, `mtp://`,
`rclone:remote:path`, or a bare host name. SMB, WebDAV and MTP go through `gio`, so
authentication is the desktop's own dialog and omafile never stores a credential. Cloud
storage comes from `rclone listremotes`; omafile does not configure rclone — that is what
`rclone config` is for.

Once mounted, a remote is an ordinary path: browsing, drag and drop, rename, copy and
preview all work unchanged. Searching a host omafile mounted runs on the far end, so it
takes about as long as a local search rather than about as long as a walk over the wire.

## Bulk rename

`Ctrl+Shift+R` writes the selected names to a file, opens `$EDITOR`, and applies the diff
when you exit. Regex rename, sequential numbering, case changes and sorting come free,
with no UI at all. If the line count changed, nothing is renamed. Swaps and rotations are
ordered so no file is ever overwritten. The whole edit is one `Ctrl+Z`.

## Configuration

There is no settings UI, and there will not be one. There is a small file:

```toml
# ~/.config/omafile/config.toml
sidebar = "remember"   # "on", "off", or "remember" (the default)
preview = "remember"
```

omafile never writes to it. What it remembers between sessions lives in
`~/.local/state/omafile/state.toml`.

## Non-goals

- No dual-pane, no miller columns, no tabs. One window, one location; `Ctrl+N` for another.
- No settings UI.
- No plugins, no scripting API, no extensions.
- No trash browser beyond undo and "open the trash folder".
- No protocol implementations beyond what a mount helper already provides.
- No credential storage, no OAuth flows, no "add account" wizards.
- No file-type-associations editor — that is `xdg-mime`'s job.

## Dependencies

**Required:** `qt6-base`, `qt6-declarative`, `qt6-svg`, `fd`, `xdg-utils`, a
`xdg-desktop-portal` backend.

**Optional**, detected at startup and hidden rather than broken when absent: `ripgrep`
(content search), `sshfs`/`gvfs` (SSH and network shares), `rclone` (cloud), `rsync`
(faster remote transfers), `udisks2` (removable media), `plocate` (whole-filesystem
search).

## Development

```bash
./bin/build     # qmake6 && make -> build/omafile
./bin/test      # both suites, plus the performance budgets
```

Two test binaries: `tst_omafile` is headless C++ under `QCoreApplication`, and
`tst_omafile_qml` drives the QML components with real clicks and keystrokes — offscreen
and software-rendered, so it needs neither a display nor a GPU. `bin/test` builds and runs
both.

The budgets are tests, not aspirations — `bin/test` fails if they regress.

| Metric | Budget | Actual |
|---|---|---|
| Cold start to first paint | < 140 ms | ~110 ms |
| 10k-entry directory | < 150 ms | ~7 ms |
| Keystroke → filtered list | < 5 ms | ~1 ms |
| First search result | < 30 ms | ~3 ms |
| 100k-file tree walked | < 400 ms | ~60 ms |

## License

MIT.
