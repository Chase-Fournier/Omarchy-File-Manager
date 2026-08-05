# Omafile — Build Plan

*A dead-simple file manager for Omarchy Quattro, in the shape of omacut and omawrite.*

---

## 1. Product definition

**One line:** Open a folder in a blink, find anything by typing, act on it without touching the mouse — and drag it out when you want to.

**The pitch against Nautilus:** Nautilus is the odd one out in Omarchy. It ignores the universal `Super+C/X/V` clipboard convention, it can't be rebound, it's slow to open, and it doesn't look like the rest of Quattro. Omafile is the file manager that behaves like the rest of the desktop.

**Non-goals** (write these in the README and defend them):

- No dual-pane / miller columns / tabs. One window, one location. `Ctrl+N` for another.
- No settings UI. Config is a small TOML/JSON file nobody needs to open.
- No plugin system, no scripting API, no extensions.
- No trash browser UI beyond "undo" and "open trash folder".
- No protocol implementations beyond SFTP. Everything else (SMB, WebDAV, MTP, cloud) is reached by delegating to `gio`/`rclone` mounts — omafile never becomes a network client zoo.
- No credential storage, no OAuth flows, no "add account" wizards. The system agent, the portal keyring, and `rclone config` already exist.
- No file type associations editor — that's `xdg-mime`'s job.

**Feature set (v1):** browse, search (local + remote + content), drag and drop both directions, full keyboard control, inline and bulk rename, copy/move/delete with undo, SSH hosts as first-class locations, image/text previews, open-with, terminal-here.

---

## 2. Stack and repo layout

Match omacut/omawrite exactly so the code reads as a sibling project:

- **Qt 6 Quick (QML)** with the Material style — same Qt stack Quickshell builds on.
- **C++17** core, single binary, QML embedded via Qt resources.
- **qmake6** (`omafile.pro`) — no CMake.
- **MIT** license.

```
omafile/
├── bin/
│   ├── build          # qmake6 && make -> build/omafile
│   ├── test           # builds and runs tests/
│   └── install        # ./bin/build && makepkg -fsi from pkgbuild/
├── pkgbuild/
│   └── PKGBUILD       # binary + .desktop + icon + LICENSE
├── src/
│   ├── main.cpp
│   ├── qml/           # embedded via resources.qrc
│   └── *.cpp/h        # core classes below
├── tests/
├── .gitignore
├── LICENSE
├── README.md
└── omafile.pro
```

**Build deps:** `qt6-base`, `qt6-declarative`, `qt6-svg`.
**Runtime deps (hard):** `fd`, `xdg-utils`, a `xdg-desktop-portal` backend.
**Runtime deps (soft, feature-gated at runtime):** `ripgrep` (content search), `openssh` + `sshfs` (SSH mounts), `rsync` (fast remote transfers), `glib2`/`gvfs` for `gio mount` (SMB, WebDAV, MTP), `rclone` (cloud remotes), `udisks2` (removable media), `ffmpegthumbnailer` (video thumbs), `wl-clipboard` (clipboard fallback).

**Linked, not optional, if the native SFTP backend ships in v1:** `libssh2` (or `libssh`) — about 300 KB and the only real dependency in the whole project.

Detect soft deps once at startup with `QStandardPaths::findExecutable` and gray out / hide the feature rather than erroring.

---

## 3. Architecture

### Core objects (C++, exposed to QML)

| Class | Responsibility |
|---|---|
| `Location` | Value type: a URI-ish path (`/home/chase`, `ssh://box/srv`) with backend tag. Everything takes a `Location`, not a `QString`. |
| `DirectoryModel` | `QAbstractListModel` of entries for one `Location`. Sorting, filtering, selection state. |
| `Entry` | POD: name, type, size, mtime, mode, symlink target, lazy-stat flag. |
| `Lister` | Worker: enumerates a directory off the GUI thread, emits batches. |
| `Watcher` | inotify via `QFileSystemWatcher`, coalesces events on a 50 ms timer, applies diffs to the model instead of re-listing. |
| `SearchEngine` | Owns the `fd`/`rg` subprocesses, streams and ranks results, cancellable. |
| `FuzzyScorer` | Pure function, ~150 lines, unit-testable. No fzf dependency. |
| `FileOps` | copy/move/delete/mkdir on a worker thread, with progress + conflict signals. |
| `Journal` | Ring buffer of completed operations for undo. |
| `Trash` | XDG trash spec: `~/.local/share/Trash/{files,info}` + `.trashinfo`. |
| `Hosts` | Parses `~/.ssh/config`, manages sshfs mounts. |
| `Thumbnails` | `QQuickImageProvider` + freedesktop thumbnail cache. |
| `Theme` | Reads Omarchy's current theme, exposes color properties, hot-reloads. |
| `Opener` | `xdg-open`, plus a parsed list of `.desktop` handlers for "open with". |

### Threading rules

- **GUI thread never touches the filesystem.** Not even `QFileInfo`. Every stat happens in a worker.
- One `QThread` for listing/watching, one for file operations, one process-reader for search. Communicate with queued signals carrying batches, never per-item signals.
- Every long operation takes a `std::stop_token`-style cancel flag checked in the inner loop. Navigating away or typing another key cancels immediately and the results are dropped, not merged.

### Windows and process model

One process per window (like omawrite's `Ctrl+N`). No single-instance daemon, no IPC, no state to corrupt. Startup cost is the thing that makes that viable — see §11.

---

## 4. UI design

### Window anatomy

```
┌─────────────────────────────────────────────┐
│  ~ / Projects / omafile                     │  breadcrumb (clickable segments)
├─────────────────────────────────────────────┤
│  ▸ src/                              4 items│
│  ▸ tests/                            2 items│
│    README.md                    3.1 KB  2d  │  <- selection: full-width tinted row
│    omafile.pro                   842 B  2d  │
│                                             │
├─────────────────────────────────────────────┤
│  4 items · 1 selected · 3.1 KB        ~ ⌘?  │  status bar
└─────────────────────────────────────────────┘
```

**Aesthetic rules** (this is the whole game — get these right and it looks like Omarchy without trying):

- No toolbar. No menu bar. No icons-with-labels grid. A list, a breadcrumb, a status bar.
- Nerd Font glyphs for file types, not an icon theme. One color, one weight, no rainbow. (Ships with Omarchy's `CaskaydiaMono Nerd Font`, so zero new dependencies and it matches the terminal.)
- Monospace throughout — it's a list of paths, alignment matters.
- Generous row height (~32 px), generous window padding, no borders between rows.
- Exactly two accent uses: the selected row and the search match highlight.
- Every transition ≤ 120 ms or absent. No spinners under 300 ms of work.
- Sidebar (home, downloads, ssh hosts, bookmarks) is **hidden by default**, toggled with `Ctrl+B`. The launcher-style search makes it mostly unnecessary.

### Theming

Read Omarchy's active theme from `~/.config/omarchy/current/theme/` at startup and re-read on change via `QFileSystemWatcher` — Omarchy theme switches are instant everywhere else and omafile should be no exception.

> Verify the Quattro theme file format before writing this: the shell moved to Quickshell in 4.0, so there is likely a structured theme source to read rather than parsing `alacritty.toml`. Plan for a `Theme` class with one `load(QDir themeDir)` entry point and a hardcoded fallback palette, so the parsing detail is swappable in one file.

Expose colors as QML singleton properties (`Theme.bg`, `Theme.fg`, `Theme.accent`, `Theme.dim`, `Theme.error`) and honor the theme's light/dark flag.

---

## 5. Keyboard map

**The central decision: bare letter keys type into the filter.** No vim `hjkl` navigation. Filenames are text; the fastest possible path from "window open" to "file selected" is to start typing its name, exactly like the Omarchy launcher. Everything else is modified or arrow keys. This is the one place to deviate from vim convention and it's worth it — write it down so it doesn't get relitigated.

| Key | Action |
|---|---|
| *any letter* | Filter current directory, incrementally |
| `↑ / ↓` , `Ctrl+K / Ctrl+J` | Move selection |
| `←` , `Backspace` | Parent directory |
| `→` , `Enter` | Enter directory / open file |
| `Shift+Enter` | Open in new window |
| `Ctrl+Enter` | Open with… |
| `Escape` | Clear filter → clear selection → close window |
| `Home / End` , `PgUp / PgDn` | Jump |
| `Ctrl+F` | Recursive fuzzy find from here |
| `Ctrl+Alt+F` | Content search (ripgrep) |
| `Ctrl+L` | Edit path directly (breadcrumb becomes a text field) |
| `Ctrl+B` | Toggle sidebar |
| `Ctrl+H` | Toggle hidden files |
| `Ctrl+P` | Toggle preview pane |
| `Super+C / X / V` | Copy / cut / paste — **the Omarchy convention Nautilus can't do** |
| `Ctrl+C / X / V` | Same, for muscle memory |
| `Ctrl+Shift+C` | Copy absolute path |
| `F2` , `Ctrl+R` | Inline rename |
| `Ctrl+Shift+R` | Bulk rename in `$EDITOR` |
| `Delete` | Move to trash |
| `Shift+Delete` | Delete permanently (confirm) |
| `Ctrl+Z` | Undo last operation |
| `Ctrl+A` | Select all |
| `Space` | Add to selection / range with `Shift+↑↓` |
| `Ctrl+Shift+N` | New folder |
| `Ctrl+T` | Terminal here |
| `Ctrl+N` | New window |
| `Ctrl+D` | Bookmark here |
| `Ctrl+S` | Connect to… (ssh/sftp/smb/davs/rclone or a config host) |
| `Ctrl+E` | Unmount / eject current location or selected device |
| `F5` | Refresh (remote locations have no watcher) |
| `Super+F` | Fullscreen (Qt maps as Meta+F — same as omawrite) |
| `Ctrl+?` | Shortcut reference overlay |

Keep the shortcut table in **one QML file** (`Shortcuts.qml`) that also generates the `Ctrl+?` overlay from the same data, so the docs can't drift from the bindings.

---

## 6. Search — the headline feature

Three tiers, all cancellable, all streaming.

### Tier 1 — Filter (instant, no I/O)
Typing filters the already-loaded `DirectoryModel` through `FuzzyScorer`. Zero syscalls, sub-millisecond for 10k entries. Match positions are returned so the UI can bold the matched characters.

### Tier 2 — Recursive find (`Ctrl+F`)
Shell out to `fd`:

```
fd --hidden --no-ignore-vcs --color never --absolute-path --strip-cwd-prefix . <cwd>
```

Read stdout incrementally, score each line, keep a bounded top-N heap (N = 500), flush to the model on a 16 ms timer so the list fills visibly while the walk continues. Kill the process on new keystroke, directory change, or `Escape`.

Two refinements worth the effort:

1. **Warm cache per directory.** On the first `Ctrl+F` in a location, keep the full path list in memory (capped at ~200k paths ≈ 20 MB) with a generation counter invalidated by the watcher. Subsequent queries re-rank in memory with no process at all — this is what makes it feel instant on the second search.
2. **`plocate` for whole-filesystem search.** If the query starts with `/`, query `plocate` instead of walking. Optional dependency, degrade silently.

### Tier 3 — Content search (`Ctrl+Alt+F`)
`rg --json --line-number --max-count 5` streamed into a results list of `path:line — preview`. Enter jumps to that line in `$EDITOR`. Feature-gated on ripgrep being present.

### Scoring
Implement a small fzf-style scorer: exact prefix > word-boundary hits > camelCase/`_`/`-` boundaries > consecutive runs > scattered; penalize gaps and path depth; strong bonus for matching the basename rather than a parent directory. Recency-boost the top of the list from a small MRU file of opened paths. Keep it a pure function taking `(needle, haystack) -> (score, positions)` so it can be unit tested to death — this is the highest-value test in the repo.

**Budget:** first result on screen within 30 ms; a 100k-file tree fully walked in under 400 ms; UI never drops below 60 fps while streaming.

---

## 7. Drag and drop

### Dragging out
Qt Quick's `Drag` attached property on the row delegate. Set:

- `Drag.mimeData = { "text/uri-list": <newline-joined file:// URIs> }` — this is what Chrome, Slack, Discord, GIMP, and every GTK app read.
- Also set `"text/plain"` with the same paths for terminals and text fields.
- `Drag.supportedActions = Qt.CopyAction | Qt.MoveAction`.
- Start the drag from a `DragHandler` with a small threshold (~8 px) so it doesn't fight click-to-select.
- Multi-select drags carry all selected URIs; the drag image is a stack-of-rows with a count badge.

### Dropping in
A window-wide `DropArea` that accepts `text/uri-list` and `text/plain` (for pasted paths) — plus, worth doing, `application/octet-stream` fallback from apps that hand over raw bytes with a suggested filename.

- Drop **onto a folder row** → into that folder; drop on empty space → current directory.
- Highlight the target row on hover with a 300 ms spring-load that navigates into it (the one genuinely great Finder behavior).
- Default action: **move** within the same filesystem, **copy** across filesystems; `Ctrl` forces copy, `Shift` forces move, `Alt` offers a small menu including "link here".

### Wayland caveat
Drag-and-drop works through Qt's Wayland DnD implementation and needs no special handling, but it *must* be tested against a GTK app, an Electron app, and a Chromium tab — those three cover the failure modes. Add that to the release checklist, not to the test suite.

---

## 8. File operations, trash, undo

- All operations run on the ops thread with progress reported per-byte for large files and per-file otherwise.
- **Same-filesystem move = `rename(2)`**, instant, no progress UI at all.
- **Cross-filesystem copy** uses `copy_file_range(2)` with a `sendfile`/read-write fallback; preserve mtime and mode; do not follow symlinks (copy the link).
- **Conflicts**: one dialog, four buttons — Replace, Skip, Rename (with a suggested `name (2).ext`), and "Apply to all remaining". No per-file interrogation.
- **Progress UI**: no dialog. A thin progress line in the status bar. If an operation exceeds 5 seconds, it survives window close by finishing in a detached state? — **no.** Simpler: block window close with "1 operation in progress" and let it finish. Dead simple wins.
- **Delete = trash**, per the XDG spec, including `.trashinfo` with the original path so Nautilus/gio can restore it too. Trash on a different filesystem falls back to `$mount/.Trash-$uid`.
- **Undo (`Ctrl+Z`)** reverses the last N=20 operations from the `Journal`: rename → rename back; move → move back; trash → restore from `.trashinfo`; copy → trash the copies. Permanent delete is not undoable and says so in the confirm dialog. A one-line "Moved 3 files to Documents — Ctrl+Z to undo" in the status bar for 5 seconds after each op.

---

## 9. Rename

**Inline (`F2`):** the row becomes an editable text field in place, with the basename pre-selected and the extension left out of the selection. `Enter` commits, `Escape` cancels, `Tab` commits and moves to the next selected file (so renaming ten files never leaves the keyboard). Validate as you type: empty, `/`, and existing-name are shown by tinting the field, not by a dialog.

**Bulk (`Ctrl+Shift+R`):** the vimv approach, which is both the simplest to build and the most powerful thing in the app. Write the selected basenames to a temp file, open `$EDITOR` (Omarchy's nvim) in a floating terminal, and on exit diff the lines against the originals:

- Line count changed → abort with an error, no partial renames.
- Compute the rename set, detect cycles (`a→b, b→a`) and resolve them through temporary names.
- Apply atomically-ish: stage all renames, roll back on first failure.
- The whole thing lands in the `Journal` as **one** undoable operation.

This gives regex rename, sequential numbering, case changes, and sorting for free, with zero UI.

---

## 10. Remote files — SSH, SFTP, and everything else

Remote access is two strategies behind one `Location` abstraction:

- **Mount-first.** Anything that can be exposed as a FUSE mount becomes an ordinary path, and every local code path — browsing, drag and drop, rename, preview, open-with — works unchanged. This is how sshfs, rclone, and gio-backed protocols are handled, and it's why the remote feature costs almost no UI code.
- **Native SFTP backend.** For when FUSE isn't installed, isn't permitted, or is too slow: a real in-process SFTP implementation that the model talks to directly. Slower to build, but it's the difference between "works on my machine" and "works on a locked-down box", and SFTP is the one protocol worth paying that cost for.

| Protocol | Access path | Tier |
|---|---|---|
| SSH / SFTP | `sshfs` mount, or native SFTP backend | v1 |
| SCP / rsync targets | `rsync` for transfers over either of the above | v1 |
| SMB / CIFS | `gio mount smb://` (or `mount.cifs`) | v1, mount-only |
| WebDAV / NextCloud | `gio mount davs://` | v1, mount-only |
| NFS | already an fstab mount; just detect and label it | v1, detect-only |
| S3, Drive, Dropbox, B2, … | `rclone mount` | v1, opt-in |
| MTP (Android phones) | `gio mount mtp://` or `jmtpfs` | v1, mount-only |
| FTP | `gio mount ftp://` | supported by accident, never advertised |

Everything except the native SFTP backend is "shell out to a mount helper and then forget it's remote" — perhaps 400 lines total for the whole table.

### 10.1 SSH over sshfs (the default path)

A pure protocol backend means reimplementing listing, stat, copy, watch, drag-out, and preview for a second code path, so the default is to mount:

1. Parse `~/.ssh/config` for `Host` entries (respecting `Include`), plus `known_hosts` as a secondary source. Show them in the sidebar and in search (`Ctrl+F` on a host name jumps to connecting).
2. Connecting mounts with `sshfs` under `$XDG_RUNTIME_DIR/omafile/<host>/`:
   ```
   sshfs host:/ $XDG_RUNTIME_DIR/omafile/host \
     -o reconnect,ServerAliveInterval=15,ServerAliveCountMax=3,\
        cache_timeout=60,kernel_cache,compression=no,idmap=user
   ```
3. From there **everything is a local path**: browsing, drag and drop, rename, copy, preview, open-with all work unchanged. Auth is plain OpenSSH, so keys, agents, `ProxyJump`, and Chase's Cloudflare Access tunnel all work with no code.
4. Unmount on window close if no other omafile window holds the mount (refcount in a runtime dir lockfile).

**Two remote-specific behaviors that matter:**

- **Search runs remotely, not over the mount.** Walking sshfs is agonizing. When the location is a mount omafile owns, run `ssh host 'fd --absolute-path . /path'` and rewrite the returned paths into local mount paths. This turns a 30-second remote search into a 300 ms one and is the single biggest reason to build a remote path at all.
- **Copy runs over `rsync`, not the mount.** Dragging a file to/from a remote location shells out to `rsync -a --info=progress2` between the local path and `host:path`, parsed for the progress bar. Ten times faster than FUSE and it gives resume for free.

Degrade honestly: if `sshfs` isn't installed, the sidebar shows the hosts with a one-line "install sshfs to browse" note. If a mount dies, the watcher notices `ENOTCONN`, shows a reconnect banner, and remounts on click.

### 10.2 Native SFTP backend

`libssh2` (small, C, no Qt dependency) or `libssh`. One class, `SftpBackend`, implementing the same interface as the local backend:

```
list(path) -> stream of Entry      via SSH_FXP_READDIR, which returns
                                    full attrs — one round trip per ~100
                                    entries, no per-file stat
stat(path)                          SSH_FXP_LSTAT
open/read(path, off, len)           for preview and copy
rename / mkdir / remove             direct FXP ops
realpath(path)                      for symlinks and `~` expansion
```

Design notes that matter more than the protocol details:

- **One connection, many channels, one thread.** A single `QThread` owns the session; every request is a queued job with a completion callback. Never open a second SSH connection for the same host — connection setup dominates everything else.
- **Reuse OpenSSH's config and agent.** Read `~/.ssh/config` for `HostName`, `Port`, `User`, `IdentityFile`, and `ProxyJump`, and authenticate through `$SSH_AUTH_SOCK` first, key file second, keyboard-interactive last. Handling `ProxyJump` in-process is fiddly; the escape hatch is to shell out to `ssh -W` as the transport and speak SFTP over its stdio — that gets `ProxyJump`, `ProxyCommand`, and Cloudflare Access tunnels working for free, which is worth more than a pure-library implementation.
- **Host key verification against `known_hosts`**, with a real trust-on-first-use prompt showing the fingerprint. Never auto-accept.
- **Aggressive pipelining.** SFTP over a 40 ms link is round-trip-bound, so keep 32+ read requests in flight during a copy and prefetch the next directory listing when a folder is selected but not yet entered. Naive request-response SFTP feels broken; pipelined SFTP feels local.
- **No watcher.** SFTP has no inotify. Refresh on focus, on operation completion, and on `Ctrl+R`; show the last-refreshed time in the status bar rather than pretending the view is live.
- **Cache stat results per directory** with a short TTL so scrolling doesn't re-request attributes.

The UI difference between mounted and native is exactly one status-bar glyph. Everything else — search, rename, drag out — routes through the backend interface. Drag *out* to another app is the one genuine gap: other apps can't read an internal URI, so dragging a file from a native-SFTP location downloads it to a temp dir first and hands over that path, with a small progress indicator during the drag.

### 10.3 Cloud and network shares

**rclone** covers S3, Google Drive, Dropbox, Backblaze, and forty others with one code path. If `rclone` is on PATH, read the remote names from `rclone listremotes` and show them in the sidebar; connecting runs:

```
rclone mount <remote>: $XDG_RUNTIME_DIR/omafile/rclone/<remote> \
  --vfs-cache-mode writes --dir-cache-time 30s --daemon
```

Omafile does not configure rclone — no OAuth flows, no credential UI. If a remote isn't set up, the sidebar entry says "run `rclone config`". That keeps the entire cloud story at roughly 60 lines.

**SMB, WebDAV, MTP** go through `gio mount`, which is present on any system with a desktop portal and handles the authentication prompts itself through the portal:

```
gio mount smb://server/share      -> /run/user/<uid>/gvfs/smb-share:...
gio mount davs://host/remote.php/webdav
gio mount mtp://...               -> phones over USB
```

Resolve the resulting gvfs path with `gio info --attribute=standard::target-uri` and treat it as local. `gio mount --list` at startup (cheap, cached) populates the sidebar with anything already mounted, which is also how omafile picks up mounts made by other apps.

**NFS and fstab mounts** need no code at all — parse `/proc/self/mountinfo`, label anything with a network filesystem type, and mark those locations "network" so the search tier and thumbnail policy adjust.

### 10.4 Removable media

Same sidebar, same mechanism, different source: watch `/proc/self/mountinfo` and list removable mounts (udisks2 mounts them at `/run/media/$USER/`). Show capacity, offer `Ctrl+E` to eject via `udisksctl unmount`/`power-off`, and refuse to eject while a file operation targeting that mount is running.

### 10.5 Connect-to and URI handling

- `Ctrl+S` opens a single-line "Connect to…" prompt that accepts `ssh://user@host/path`, `sftp://`, `smb://`, `davs://`, `rclone:remote:path`, or a bare `~/.ssh/config` host name, with completion from known hosts, rclone remotes, and recent connections. One field, no dialog, `Escape` cancels.
- `omafile ssh://host/srv` from the CLI does the same thing, so other apps and scripts can hand omafile a remote location.
- The path bar (`Ctrl+L`) accepts the same URIs, so navigation and connection are one concept.
- Recent connections live in a small MRU file and rank at the top of `Ctrl+F`, so reaching a remote box is: `Ctrl+F`, three letters, `Enter`.

### 10.6 Policy for remote locations

Behavior that must differ from local, enforced centrally by a `Location::isRemote()` check rather than scattered `if`s:

- **Search** runs remotely where possible (`ssh host fd …`), falls back to a bounded depth-limited walk otherwise, and never silently walks a slow mount to completion.
- **Transfers** use `rsync` over SSH; other protocols use plain streaming copy with resume where the backend supports it.
- **Thumbnails and previews** are off by default and only generate on explicit request, with a size cap (skip anything over ~20 MB).
- **Delete does not trash** — there is no reliable remote trash. Delete on a remote location always confirms and always says "permanent".
- **Undo is disabled** for remote deletes, and remote moves are journaled only when they're same-host renames.
- **Never block the UI on a dead connection.** Every remote call has a timeout (10 s default) and a visible reconnect banner.

### 10.7 Credentials

Nothing is stored by omafile. SSH keys stay in the agent, SMB/WebDAV credentials in the portal's keyring via gio, cloud tokens in rclone's own config. If a protocol needs a password with no keyring available, prompt per session and hold it in memory only. This is the boring answer and it's also the one that never leaks a token into a dotfile.

---

## 11. Preview and thumbnails

- Preview pane is off by default, `Ctrl+P`, and takes the right 40% of the window.
- Images: `QImageReader` with `setScaledSize` so a 40 MP photo never fully decodes. Text/code: first 200 lines, monospace, no syntax highlighting (dead simple). PDFs and video: first-frame thumbnail only, no viewer.
- Thumbnails follow the freedesktop spec at `~/.cache/thumbnails/{normal,large}` with the URI-MD5 filename and `Thumb::MTime` check — so omafile shares a cache with every other app rather than building its own.
- Generate only for visible rows, on a low-priority thread, with an LRU in-memory cache of ~200 decoded pixmaps. Cancel generation when the row scrolls out of view.
- Never generate a thumbnail for a file on an sshfs mount unless the user opens the preview pane explicitly.

---

## 12. Performance budgets

Treat these as tests, not aspirations. `bin/test` should fail if they regress.

| Metric | Budget |
|---|---|
| Cold start to first paint | < 160 ms |
| Listing a 10k-entry directory | < 50 ms to first rows, < 150 ms complete |
| Keystroke → filtered list | < 5 ms |
| `Ctrl+F` → first result | < 30 ms |
| Scroll | 60 fps sustained on 100k rows |
| RSS, idle, one window | < 90 MB |

How to hit the startup number: embed QML in Qt resources and precompile with `qmlcachegen` (qmake does this if `CONFIG += qtquickcompiler`); import nothing beyond `QtQuick`, `QtQuick.Controls.Material`, and `QtQuick.Layouts`; do zero filesystem work before `show()` — paint the empty themed window first, then let the first `Lister` batch arrive. Defer `Hosts`, `Thumbnails`, and `Opener` construction until first use.

Listing fast means `QDirIterator` with `QDir::NoDotAndDotDot` and **no stat at all** on the first pass — name and d_type from `getdents64` are enough to draw a row. Size and mtime are filled in by a second pass that only stats the visible window plus a buffer, so a directory with 100k files draws instantly and fills in as you scroll.

---

## 13. Omarchy integration and packaging

- **PKGBUILD** modeled on omacut's: installs `/usr/bin/omafile`, `/usr/share/applications/omafile.desktop`, the icon, and the MIT license. `./bin/install` runs the build then `makepkg -fsi`.
- **`.desktop`** declares `MimeType=inode/directory;` and `Categories=System;FileTools;FileManager;` so `xdg-mime default omafile.desktop inode/directory` makes every other app open folders in omafile.
- **CLI:** `omafile [path]`, `omafile --select <file>` (open parent, preselect), reading `$PWD` when given nothing.
- **Hyprland:** propose `Super+F` for omafile (where Omarchy currently launches Nautilus), and a float rule sized ~1200×800 centered — a file manager wants to be a floating window over the tiling layout, like the Omarchy menus.
- **Theme:** live reload as in §4, so `omarchy theme set` re-colors an open window.
- Don't fight Omarchy's conventions anywhere: `Escape` closes, `Ctrl+?` documents, `Super+F` fullscreens, `Ctrl+N` opens another window, XDG portal for any file picker.

---

## 14. Testing

`bin/test` builds a Qt Test binary over the C++ core — everything valuable here is headless:

- `FuzzyScorer`: a table of (query, candidates, expected order) cases. Include the nasty ones: `omf` matching `omafile.pro` over `some/other/file`, basename beating path, case sensitivity on uppercase queries.
- `Trash`: `.trashinfo` round-trip, name collisions in the trash, cross-filesystem fallback.
- `FileOps`: conflict-name generation, cycle detection in bulk rename, cross-device copy, symlink handling.
- `Journal`: undo correctness for every op type, including partially failed ops.
- `Lister`: correct handling of permission-denied dirs, broken symlinks, files with newlines and quotes in names (the classic breakage for anything that shells out — test `fd` parsing against a filename containing a newline, and use `--print0` if it fails).
- `Location`: URI parsing round-trips for every scheme in §10, including paths with spaces, `%` escapes, and non-UTF-8 bytes; correct mapping between a mount path and its original URI.
- `SftpBackend`: run against a real `sshd` in a container (`localhost`, throwaway key) — listing, rename, chmod, resume-after-partial-copy, symlink handling, permission-denied, and a deliberately killed connection mid-transfer. This is an integration test, gated on the container being available, skipped otherwise.
- Mount lifecycle: refcounting across simulated windows, stale-mount detection (`ENOTCONN`), and no orphaned mounts left in `$XDG_RUNTIME_DIR` after an unclean exit.
- Performance assertions for the §12 budgets against a generated tree.

Manual release checklist (not automated): drag out to Chrome, a GTK app, and an Electron app; drop in from each; theme switch with a window open; sshfs disconnect mid-browse.

---

## 15. Milestones

**M0 — Skeleton (½ day).** Repo laid out like omacut, `omafile.pro`, `bin/build`, a themed empty window that opens in under 160 ms. Prove the startup budget before writing anything else; it's much harder to recover later.

**M1 — Browse.** `Lister`, `DirectoryModel`, `Watcher`, breadcrumb, status bar, keyboard navigation, type-to-filter, `Enter`/`xdg-open`, hidden files, sorting. **This is the point where it replaces Nautilus for reading.** Ship it to yourself here and use it daily.

**M2 — Act.** Selection model, copy/cut/paste, drag and drop both ways, trash, undo, new folder, inline rename, terminal-here, copy path.

**M3 — Find.** `FuzzyScorer`, `Ctrl+F` recursive search with streaming, warm cache, `Ctrl+Alt+F` content search. **This is the feature that makes it better than Nautilus, not just prettier.**

**M4a — Remote by mount.** `~/.ssh/config` parsing, sidebar, sshfs mount lifecycle, remote-side search, rsync transfers, disconnect handling. Then the cheap wins in one pass: `gio mount` for SMB/WebDAV/MTP, `rclone listremotes` for cloud, `/proc/self/mountinfo` for NFS and removable media, `Ctrl+S` connect-to and URI handling. Most of that is one function each once the mount lifecycle exists.

**M4b — Native SFTP.** The `SftpBackend`, pipelining, host-key prompts, `ssh -W` transport for `ProxyJump`, temp-download on drag-out, remote policy enforcement. Ship M4a first and live on it — M4b is worth building only once you've felt where sshfs is actually too slow.

**M5 — Polish.** Preview pane, thumbnails, bulk rename via `$EDITOR`, open-with, bookmarks, `Ctrl+?` overlay, conflict dialog.

**M6 — Ship.** PKGBUILD, `.desktop`, icon, README with hotkey table and screenshot in the omacut/omawrite voice, performance tests in CI, then open the PR against Omarchy proposing it as the default file manager.

M1–M3 is the real product. M4 is the differentiator nobody else has. Resist adding anything to M1 or M2 that isn't in the table in §5.

---

## 16. Decisions still open

1. **Quattro theme format** — confirm what the Quickshell-based shell reads so `Theme` parses the right source rather than scraping terminal configs.
2. **`fd` as a hard dependency vs. a built-in walker.** Shelling out is ~50 lines and respects `.gitignore` for free; a built-in `getdents64` walker is ~200 lines, has no dependency, and avoids the filename-quoting class of bug entirely. Leaning toward `fd` with `--print0` for v1, and a built-in walker later if it proves worth it.
3. **Selection model semantics** — whether `Space` toggles selection (launcher-like) or extends (Finder-like) when a filter is active. Try both for a week.
4. **Whether the sidebar should exist at all**, given `Ctrl+F` and bookmarks-by-search. Build it, then try deleting it.
5. **`libssh2` vs `libssh` vs `ssh -W` + stdio SFTP.** The stdio approach inherits every OpenSSH config feature (`ProxyJump`, `ProxyCommand`, `Match`, certificates, Cloudflare Access) for near-zero code, at the cost of a subprocess per connection. That trade looks right for this project; confirm by measuring connection setup time on a real tunnel before committing.
6. **Whether mounted and native SFTP should be user-visible at all**, or whether omafile should just pick (native if `libssh2` is linked, mount if not) and never mention it. Leaning toward "never mention it", with an override in the config file for when the automatic choice is wrong.
7. **Icon set** — Nerd Font glyphs (zero deps, matches the terminal) vs. the active GTK icon theme (matches other apps). Nerd Font is more Omarchy; check how it reads at 32 px on a HiDPI panel first.
