# CLAUDE.md

The whole of omafile's documentation that is not the README: what it is meant to be, what
was decided along the way, and what still needs checking.

The build plan this was written against has been folded in — "Requirements" below is what
survived of it, keeping its § numbering because the decisions all cite it. Everything the
plan got wrong, or that was superseded by contact with a real machine, is recorded in
Decisions rather than left in place.

## Commands

```bash
./bin/build                        # qmake6 && make -> build/omafile
./bin/test                         # all suites + the §12 startup budget
./build/omafile [path]             # run; defaults to $PWD
./build/omafile --select <file>    # open the parent, preselect the file
./build/omafile --dump-theme       # print the resolved palette, headless (key<TAB>value)

OMAFILE_TRACE_STARTUP=quit ./build/omafile   # print ms to first paint and exit
./build/tests/tst_omafile <testName>         # run one test across all suites
```

**Debugging QML at runtime:** `console.log` is swallowed and Qt routes messages to
journald here. Use `console.warn` plus `QT_ASSUME_STDERR_HAS_CONSOLE=1`, or read
`journalctl --user | grep omafile`.

**Driving the pointer.** There is no `ydotool`/`wtype`/`dotool` here, and `hyprctl
dispatch sendshortcut` covers keys but not buttons — so clicks are injected through
`/dev/uinput`, which is writable by the `input` group without root. Three things had to be
right before any of it was trustworthy, and each one first looked like an application bug:

- **One device for the whole session.** Created and destroyed per click, roughly half the
  clicks vanish: the compositor has not finished setting the device up before the button
  arrives. Create it once, feed it commands on stdin.
- **Nudge before pressing.** A pointer that has never moved has no surface focus, so its
  first button goes nowhere. One pixel out and back is enough.
- **Wait for the old window to die before launching the new one.** Two omafile windows get
  tiled, every hardcoded coordinate then points somewhere else entirely, and the failures
  look exactly like a broken feature. Poll `hyprctl clients` to zero, then to one, and
  derive coordinates from the window's real geometry.

Even then, read a "nothing happened" carefully: it also means *the click landed on
something that does not log*. Probe every surface under test with `console.warn` before
concluding a click was lost — and remember the preview pane occupies the right of the
list, so a coordinate that looks like it is over a row may not be.

## Layout

```
src/main.cpp          entry point, CLI, --dump-theme, startup tracing
src/location.*        the value type every path flows through, local and remote
src/entry.h           one directory entry (POD)
src/lister.*          worker-thread directory walk + lazy stat pass
src/watcher.*         coalesced inotify on the current directory
src/directorymodel.*  the model the UI talks to: sort, filter, selection, diffing
src/formatting.*      human sizes and relative times (pure, testable)
src/trash.*           the XDG trash spec
src/fileops.*         copy/move/delete/mkdir/rename on the ops thread
src/journal.*         bounded undo history
src/operations.*      the QML-facing front for ops, clipboard, journal, conflicts
src/clipboard.*       system clipboard in the formats other file managers read
src/opener.*          xdg-open and new windows
src/theme.*           Omarchy theme parsing + hot reload
src/qml/Main.qml      the window: verbs, keyboard, list, drag, overlay wiring
src/qml/Breadcrumb.qml where you are — crumbs, their drop targets, the Ctrl+L field
src/qml/StatusBar.qml the one line at the bottom
src/qml/Menus.qml     what each right-click menu contains (pure data, no layout)
src/qml/EntryRow.qml  one row: name, drag source, inline rename
src/qml/Overlay.qml   the single modal surface
src/qml/Shortcuts.qml the keyboard map, single source of truth (§5)
tests/                one binary, one suite per class, registered in tests/main.cpp
tests/qml/            the QML suite — a second binary, see below
```

---

## Requirements

Everything below is a constraint the code is expected to hold to, not a description of it.
Section numbers are the ones the decisions cite.

### §1 What it is, and what it is not

One line: open a folder in a blink, find anything by typing, act on it without touching
the mouse — and drag it out when you want to.

**Non-goals, to be defended rather than revisited:**

- No dual-pane, no miller columns, no tabs. One window, one location; `Ctrl+N` for another.
- No settings UI. Config is a small file nobody needs to open.
- No plugins, no scripting API, no extensions.
- No trash browser beyond undo and "open the trash folder".
- No protocol implementations beyond what a mount helper already provides — omafile never
  becomes a network client zoo.
- No credential storage, no OAuth flows, no "add account" wizards.
- No file-type-associations editor: that is `xdg-mime`'s job.

### §3 Threading

- **The GUI thread does not touch the filesystem** — not even `QFileInfo`. Every stat
  happens on a worker. Two documented exceptions: `Theme` reads once before first paint,
  and `QFileSystemWatcher::addPath`, which is why `Watcher` lives on the worker.
- One thread for listing and watching, one for file operations. Queued signals carry
  batches, never one item at a time.
- Every long operation checks a cancel flag in its inner loop. Navigating away or typing
  another key cancels immediately and the results are *dropped*, not merged.
- One process per window. No daemon, no IPC, no shared state — which is only viable
  because startup is cheap, so §12 is what keeps this design honest.

### §4 How it looks

No toolbar, no menu bar, no icon grid: a breadcrumb, a list, a status bar. Nerd Font
glyphs for file types rather than an icon theme — one colour, one weight, no rainbow.
Monospace throughout, because it is a list of paths and alignment matters. Row height
~32 px, generous window padding, no borders between rows. Exactly two uses of the accent
colour: the selected row, and the search-match highlight. The sidebar is hidden by
default. Transitions are ≤ 120 ms or absent; no spinner for under 300 ms of work.

### §5 Keyboard

**Bare letter keys type into the filter.** No vim `hjkl` navigation. Filenames are text,
and the fastest path from "window open" to "file selected" is to start typing the name,
exactly like the Omarchy launcher. Everything else is modified or an arrow key. This is
the one deliberate deviation from vim convention; it is written down so it is not
relitigated.

The binding table itself lives in `src/qml/Shortcuts.qml` and is the single source of
truth — the `Ctrl+?` overlay and the README table are generated from or mirror it.

### §6 Search, in three tiers

1. **Filter** — bare letters, in-directory, fuzzy, no I/O.
2. **`Ctrl+F`** — recursive name search from here, streaming, cancellable, warm-cached.
3. **`Ctrl+Alt+F`** — content search over ripgrep, with line numbers and previews.

Ranking is a pure `(needle, haystack) -> (score, positions)` function with no fzf
dependency, so it is directly testable. Smart case: an all-lowercase needle matches
case-insensitively; any uppercase character makes the whole match case-sensitive.

### §7 Drag and drop

Both directions. `Ctrl` forces copy, `Shift` forces move; otherwise move within a
filesystem and copy across one. The release checklist is manual: drag out to a GTK app, an
Electron app and a Chromium tab, and drop into omafile from each.

### §8 Operations, trash, undo

Copy, move, delete, mkdir and rename run on the ops thread with progress and conflict
signals. Conflicts offer Replace / Skip / Rename / Apply-to-all — never per-file
interrogation. Trash follows the XDG spec so other file managers can restore what omafile
trashed. Undo is a bounded journal and must never itself be destructive. An operation in
flight blocks window close rather than being detached: dead simple wins.

### §9 Rename

Inline rename in place, and bulk rename through `$EDITOR`: the selected names are written
to a file, the editor opens, and the diff is applied on exit. A changed line count aborts
the whole edit rather than renaming a prefix of it. Swaps and rotations are ordered so no
file is ever overwritten, and the whole edit is one undo.

### §10 Remote

- **§10.1** — hosts come from `~/.ssh/config`; omafile keeps no host list of its own. The
  alias is handed to ssh rather than a rebuilt `user@host`, so `ProxyJump`, `IdentityFile`
  and `Match` keep working. The mount is the remote *root*, not the remote home. Search
  runs on the far end, which is the single biggest reason to build a remote path at all.
- **§10.3** — SMB, WebDAV, MTP and cloud are reached by delegating to `gio` and `rclone`
  mounts, never by implementing a protocol. Missing helpers grey out; they never fail.
- Mount and forget: a remote is an ordinary path once mounted, so there is no second
  browsing code path.
- **§10.6 — what must differ for a remote location**, decided by one `isRemote()` check
  rather than scattered `if`s: search runs on the far end where possible and is otherwise
  depth-bounded, never an unbounded crawl; previews and thumbnails are on request only;
  **delete does not trash**, because there is no dependable remote trash, so it confirms
  and says "permanent"; nothing blocks the UI on a dead connection.
- **§10.7 — credentials are never stored.** Keys stay in the agent, share credentials in
  the portal's keyring via gio, cloud tokens in rclone's own config. A password with no
  keyring is held in memory for one use and never written or logged.

### §11 Preview and thumbnails

Off by default, `Ctrl+P`, the right 40% of the window. Images decode with
`QImageReader::setScaledSize` set *before* `read()`, so a 40 MP photo is never fully
decoded to draw it small. Text is the first 200 lines, monospace, no syntax highlighting.
Thumbnails follow the freedesktop spec — `~/.cache/thumbnails/{normal,large}`, the MD5 of
the `file://` URI, a `Thumb::MTime` check — so the cache is shared with the rest of the
desktop. Generate only for visible rows, and never for a file on a network mount unless
the preview pane is explicitly open.

### §12 Performance budgets

Tests, not aspirations: `bin/test` fails if they regress.

| Budget | Target | Actual |
|---|---|---|
| Cold start to first paint | < 140 ms | **~100 ms** median, settled machine |
| 10k-entry directory, complete | < 150 ms | **7 ms** |
| Keystroke → filtered list | < 5 ms | **1 ms** (fuzzy, 10k entries) |
| First search result | < 30 ms | **3 ms** |
| 100k-file tree walked | < 400 ms | **61 ms** |
| RSS, idle, one window | < 90 MB | **131 MB** ✗ — see To verify |

How the startup number is held: QML embedded in resources and precompiled with
`qtquickcompiler`, nothing imported beyond `QtQuick`, no filesystem work before the window
is shown, and `Hosts`/`Thumbnails`/`Opener`/search built on first use rather than at
startup.

Listing fast means `opendir`/`readdir` with **no stat on the first pass** — name and
`d_type` are enough to draw a row. Size and mtime are filled in by a second pass over the
visible window plus a buffer.

### §13 Packaging

A PKGBUILD installing `/usr/bin/omafile`, the `.desktop` declaring `inode/directory`, the
icon and the licence. Soft dependencies are detected once at startup and greyed out rather
than failing. Don't fight the desktop's conventions anywhere.

### §14 Testing

Everything valuable is headless. The hazards worth naming, because each one has bitten:

- Filenames containing newlines and quotes — the classic breakage for anything that shells
  out. Search is the only place omafile does, and it uses `--print0`.
- Permission-denied directories and broken symlinks in a listing.
- Cross-filesystem copy, symlink handling, and cycle detection in bulk rename.
- Undo correctness for every operation, including partially failed ones.
- Mount refcounting across windows, and no orphaned mounts left in `$XDG_RUNTIME_DIR`
  after an unclean exit.
- The §12 budgets, asserted against a generated tree.

Manual release checklist, not automated: drag out to a GTK app, an Electron app and a
Chromium tab; drop in from each; switch themes with a window open; disconnect an sshfs
mount mid-browse.

### §16 Questions the plan left open

Kept by their original numbers because the decisions cite them. Resolved ones say so.

1. **Quattro theme format** — resolved; see Decisions.
2. **`fd` versus a built-in walker** — resolved: `fd` with `--print0`.
3. **Selection semantics** — open. Whether `Space` toggles selection (launcher-like) or
   extends it (Finder-like) with a filter active. Try both for a week.
4. **Whether the sidebar should exist at all**, given search and pins — build it, then try
   deleting it. Built; not yet tried without.
5. **Native SFTP transport** — open, and the gate on building one at all. `libssh2` vs `libssh` vs
   `ssh -W` + stdio SFTP. The stdio approach inherits every OpenSSH config feature for
   near-zero code at the cost of a subprocess per connection; confirm by measuring
   connection setup on a real tunnel before committing.
6. **Whether mounted and native SFTP should be user-visible** — leaning toward never
   mentioning it, with a config override for when the automatic choice is wrong.
7. **Icon set** — open. Nerd Font glyphs (zero dependencies, matches the terminal) versus
   the active GTK icon theme. This is also what decides whether the list ever shows
   thumbnails.

---

## Status

Everything specified above is built and in daily use, except the native SFTP backend,
which is deliberately not started — §16.5 gates it and the measurement cannot be made
here. Since then: right-click menus on all four surfaces, pinning files as well as
folders, breadcrumb drop targets, and the three separate desktop hooks that decide what
opens a folder (see Decisions).

222 C++ tests and 9 QML tests pass. `bin/test` builds and runs both binaries and holds the
§12 budgets; CI does the same on Arch and then packages.

---

## Decisions

### Right-click menus

**Plain QtQuick, not QtQuick.Controls' `Menu`.** Importing Controls pulls its style plugin
in at startup, and §12's budget (already the tightest number in the project) has no room
for a feature only reached on demand. The cost is about 90 lines and re-implementing
keyboard navigation, which the rest of the app needs anyway.

**Every entry calls a verb the window already implements.** `Menus.qml` builds arrays of
`{ label, action, enabled, separator }` where each `action` is an arrow function onto an
existing `root` function, so there is exactly one implementation of each operation to keep
correct.

It was claimed here — and in `README.md` — that every entry also has a keyboard shortcut.
That was never true: "New file", "Edit ~/.ssh/config", "Edit omafile config", "Connect in
a terminal" and "Remove bookmark" have no key and never did. Adding keys to make the
sentence true would spend five shortcuts on verbs nobody reaches for daily, so the
sentence was corrected instead.

**Disabled rather than hidden** for entries that do not apply — "Open with…" on a
directory, "Move to trash" on a network mount (§10.6 has no dependable trash there),
"Paste" with an empty clipboard. A menu whose items move around between openings cannot be
learned.

**The "config" entries open the file, not a settings screen** (§1 rules out a settings UI).
An SSH host offers `~/.ssh/config`, an rclone remote offers `rclone config` in a terminal,
and every place offers omafile's own `config.toml` via the new `Settings.configPath`.

**`New file` uses `O_EXCL`**, not `QFile::open(WriteOnly)`, which truncates. The context
menu puts "New file" one click away from any directory, so a name collision must fail
rather than silently empty whatever was there. `tst_fileops::newFileNeverTruncates` pins
both halves of that.

**The empty run to the right of the breadcrumb opens the blank-space menu too.** It is the
nearest thing this window has to a title bar, and it is where a right-click goes looking
for "New file" when the list is full to the bottom and there is no blank space left in it.
The strip is anchored `crumbs.right → parent.right`, so it starts exactly where the last
segment ends and never swallows a right-click meant for a crumb — verified by probe at
steady state, because at construction the breadcrumb has no width yet and the strip
momentarily spans the whole header. It is hidden while `Ctrl+L` has the path open for
editing, since the field covers that strip and the click belongs to the field.

**Dismissing does the click as well as closing.** A click outside the menu closes it *and*
still lands on whatever it hit: a row selects, another right-click opens the menu there
instead. The dismissal `MouseArea` declines the press — `event.accepted = false` inside
`onPressed`, not `onClicked` — which hands it to the item underneath. Only `onPressed` can
do this; by `clicked` the event is already composed and there is nobody left to give it to.
The panel carries its own `MouseArea` so that a click on its padding or a separator is
swallowed rather than falling through to the list behind it.

The alternative — eating the click — charges two clicks for every one you meant, and makes
a stray right-click cost something. Verified with real clicks in both directions.

**Two QML traps this hit, both worth remembering:**

- **`TextMetrics` has no `implicitWidth`.** It exposes `width`/`advanceWidth`. Reading the
  wrong one yields `undefined`, which propagates through arithmetic to a `NaN` width — and
  a `NaN`-wide item renders at zero with *no* warning. The menu opened correctly, took
  focus, and was simply invisible. Anything sized off a measured string should be checked
  against a probe print, not assumed.
- **The keep-inside-the-window clamp has to be a binding on the panel, not a calculation
  in `openAt()`.** At the moment `openAt` runs, the panel has not been laid out, so its
  height still reads as empty and the clamp does nothing. As a binding it corrects itself
  once the real size arrives. This is the same layout-timing class of bug as the drag badge
  and the Overlay focus grab — the third instance in this project.

### A running operation is modal now

**§4 asked for "a thin progress line, never a dialog", and this is a deliberate departure**
— requested, after watching a 50 MB zip run with the window still accepting keystrokes.
The hairline in the status bar says *that* something is happening; it does not stop you
navigating away, retyping a filter, or starting a second operation on a directory being
rewritten underneath you. The bar is now the shared modal `Overlay` in a new `progress`
mode: a determinate track, the entry being written under it, and Escape to cancel.

**It waits 300 ms before appearing, which is §4's own rule** — no spinner for under 300 ms
of work. Almost everything in this app finishes well inside that, and a dialog that
flashes on every paste would be worse than the hairline ever was. The status-bar line is
kept: it is still the right answer for the fast majority.

**It steps aside for a conflict.** A copy that hits a name collision has to be answerable,
so `conflictActive` closes the progress overlay and reopens it once the question is
answered. Two modal scrims stacked on each other would otherwise make the conflict
unreadable.

**`waitForReadyRead()` watches the wrong channel by default.** It waits on *stdout*, and
bsdtar writes both its per-entry lines and its errors to *stderr* — so every pass timed
out at 200 ms, nothing was parsed until the process had already exited, and the bar sat
empty for the whole operation before jumping to done. `setReadChannel(StandardError)` is
the fix. The symptom looked exactly like "progress is not implemented", which is why it
survived the first screenshot: the overlay was correct and the data never arrived.

### Compressing

**`bsdtar` and nothing else.** libarchive is a dependency of `pacman`, so it is on every
Arch machine — which means zip, tar+gzip, tar+zstd, tar+xz and 7z all arrive with no new
dependency and no second code path. `-a` picks the format from the extension the caller
asked for, so the filename and the format cannot disagree; `-C <parent> <names>` keeps the
paths inside relative, which is what makes the archive unpack the same way anywhere
instead of carrying this machine's absolute paths.

**Nothing is deleted, which is the whole requirement.** An archive is a copy. Undo removes
the archive, and `compressingLeavesTheOriginalsAlone` asserts the inputs are still there
with their contents — it would fail on any future "compress and clean up" shortcut.

**`suggestName` always steps to "(2)"; it is not a name generator.** Calling it up front
put the very first archive at `pack (2).zip` and left `pack.zip` missing. It exists to
resolve a collision, so it is only reached when the plain name is taken. The test caught
this immediately, which is the argument for asserting on the file that appears rather than
on the absence of an error.

**Progress is real, not a spinner.** `bsdtar -v` prints one `a <path>` line per entry on
*stderr*, so counting entries first and matching those lines gives a genuine bar. A
cancelled compression kills the process and deletes the partial archive: a half-written
zip that looks finished is worse than no zip at all, and the same goes for a failure —
`failedCompressionLeavesNothingBehind` pins that.

### "Reveal in File Explorer" is not xdg-open

**`org.freedesktop.FileManager1` is a third way of asking for a file manager**, and omafile
answered none of it. VS Code's "Reveal in File Explorer" and Chromium's "Show in folder"
make a D-Bus call to that well-known name; whoever owns it answers. Nautilus ships
`/usr/share/dbus-1/services/org.freedesktop.FileManager1.service` with
`Exec=/usr/bin/nautilus --gapplication-service`, so on any machine with Nautilus installed
it wins — and neither `xdg-mime` nor the compositor's keybinding has any bearing on it.
Confirmed by calling `ShowItems` by hand and watching Nautilus open.

So there are three independent switches and setting one does not set the others:
`xdg-mime` for anything that opens a folder, the compositor's binding for the key, and
this for "reveal".

**The service is a mode, not a startup cost.** `omafile --dbus-service` is started by D-Bus
on demand, owns the name, and turns each call into an ordinary omafile process — §3's "a
second window is a second process" applies here too, so it holds nothing and exits after
30 seconds idle. An ordinary launch never touches D-Bus.

**`QT += dbus` costs nothing** because `libQt6DBus` is already a load-time dependency of
QtGui's platform integration. Measured before writing a line of code — 96 ms against
95 ms — because this is exactly the mistake `quickcontrols2` was.

**`ShowItems` is `--select`.** The interface's "show me this file" is the flag omafile
already had, so the handler spawns `omafile --select <path>` and existing behaviour does
the work.

**The service file is installed by the user, not the package.** Two packages cannot own
`/usr/share/dbus-1/services/org.freedesktop.FileManager1.service`, so taking it would mean
conflicting with Nautilus. A copy in `~/.local/share/dbus-1/services/` takes precedence
over the system one, so the package ships a template under `/usr/share/omafile/` and the
README gives the one-line copy. Reversible with `rm`.

### The permanent "gvfs" row in the sidebar

**`$XDG_RUNTIME_DIR/gvfs` is a container, not a place**, and it was being listed as a
volume. `fuse.gvfsd-fuse` is in the network-filesystem list — correctly, because files
under it *are* remote — but the mount itself is the FUSE bridge gvfsd exposes, present for
as long as gvfsd-fuse is running, which on a desktop with a portal is always. Every share
gvfs holds is a **subdirectory** of that one mount rather than a mount of its own, so
`/proc/self/mountinfo` shows exactly one line no matter how many shares are connected.

The result was a row called "gvfs" (from `label()` taking the last path component) that
never went away and, with nothing connected, pointed at an empty directory. With something
connected it still said only "gvfs".

So the bridge is skipped and its children are listed instead, one row per share, named by
decoding gvfs's own directory names — `smb-share:server=nas,share=media` becomes "media on
nas". Unrecognised spellings are shown as-is rather than as an empty row.

**Read with readdir and no stat.** Listing the bridge is answered by gvfsd from its own
table, but stat-ing a share reaches for the far end, and a share whose server has gone
away would block the caller — which here is the GUI thread.

**Three owners, three unmount commands.** A gvfs share is not omafile's to unmount:
`fusermount3` on it would take down the bridge and every other share with it, and udisks
has never heard of it. `Places::eject` routes gvfs shares to `gio mount -u`, which is why
`isGvfsShare` exists as a path test rather than being inferred from the filesystem type —
the type is the same for the bridge and everything inside it.

### Pinning files as well as folders

**A pin is a bookmark that also accepts a file**, which is most of why this needed any work
at all: `addBookmark` already stored whatever path it was handed, but everything downstream
assumed a directory. A pinned file was drawn with the folder glyph and, when clicked,
asked the model to *navigate into a file* — so the feature was one line from existing and
would have looked broken the first time anyone used it.

**A pinned file opens; a pinned folder navigates.** That is the same answer Enter gives
each of them in the list, so a pin behaves like the row it was made from rather than
inventing a third rule. `Places::activate` routes on `QFileInfo::isDir`, not on the stored
kind, because the target can change type between sessions.

**Ctrl+D cannot pin a file** — it means "the folder I am looking at", and there is no
sensible way to make one key mean both that and "the row under the cursor". So the row is
pinned from the context menu, and `pinSelection` goes through `Dir.actionPaths()` like
every other verb, which gets multi-select for free. A mixed selection follows whichever way
the first path goes, so the entry's label is never a lie about what it will do.

**One vocabulary: "pin".** It was "Bookmark here" on Ctrl+D and "Remove bookmark" in the
sidebar menu, and "bookmark a file" reads oddly. The stored file is still
`~/.config/omafile/bookmarks` — renaming it would break everyone's pins for a word.

**`tst_places` is the first test to construct a `Places`.** It fakes `HOME`,
`XDG_CONFIG_HOME` *and* `XDG_RUNTIME_DIR` — the first two for the obvious reasons, the
third because the constructor sweeps orphaned mounts and must not go anywhere near a mount
this machine is really holding. The glyph assertion was confirmed to fail when both pins
are drawn the same way.

### The QML layer has tests now

**`tests/qml/` is a second binary, `tst_omafile_qml`.** It has to be: QtQuickTest needs a
`QGuiApplication` and a scene graph, where `tst_omafile` deliberately runs headless under
`QCoreApplication`. Both are built and run by `bin/test` and by CI. It runs with
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` — no GPU required, which is what
makes it work on a CI runner; verified in the same `archlinux:latest` container CI uses.

**Components are tested on their own, with a stubbed `app`.** `ContextMenu` needs nothing
but the properties it reads off the window — pad, rowHeight, fonts, colours — so the tests
supply a `QtObject` with those and never construct the model, the ops thread or a
directory. Only `Theme` is registered for real, because the components import it.

**The tests were confirmed to fail against the bugs they describe.** Re-introducing
`longest.implicitWidth` reproduces `panel width is 0 — NaN collapses to 0`, and making the
dismissal accept the press reproduces `the click was swallowed instead of passing
through`. Both took an afternoon and a screenshot to find the first time; they now fail in
36 ms. A UI test that has never been seen to fail is decoration.

**Two things about the harness that are not obvious:**

- **A `Loader` cannot build a component with an unset `required property`.** Setting
  `source:` and assigning in `onLoaded` is too late — the load fails *silently*, leaving
  `item` null and every test reporting "cannot call openAt of null" rather than anything
  about the real cause. Use `setSource(url, { "app": stub })`.
- **A C++ `DEFINES +=` is invisible to QML.** The component path is relative to the test
  file instead.

### Main.qml is 979 lines, down from 1238

Three pieces came out, chosen for having no layout coupling to the rest of the window:
`Menus.qml` (the menu entries — pure data), `StatusBar.qml`, and `Breadcrumb.qml`.

**The call sites did not move.** `EntryRow`, `Sidebar` and the blank-space areas still call
`app.menuForRow` and friends; those are now one-line wrappers onto `menus.forRow`. A
refactor that also rewrites its callers cannot be checked against "nothing changed".

**`pathField` was the coupling worth removing.** Main.qml reached into the text field from
four places to set its text, focus it and read it back. `Breadcrumb` now exposes
`beginEditing(path)` and `editedPath()` and owns the field.

**Note that QML resolves ids at runtime.** After moving the field, Main.qml still referred
to `pathField` and *the build succeeded* — Ctrl+L would have failed at the moment it was
pressed. Grep for the ids you moved; the compiler will not.

**The split costs nothing at startup** — median 112 ms against 111 ms before, on 15 samples
each. qtquickcompiler builds every .qml into the binary, so more files is not more I/O.
Worth having measured rather than assumed, since §12 is the tightest budget here.

**Verified by screenshot and by hand**, not only by the suite: the window renders
identically, Ctrl+L still turns the breadcrumb into a populated, selected field, and both
the row and place menus still build with the right entries and the right ones greyed out.
A pixel-diff of before and after is *not* a useful check here — the directory being shown
is the repo itself, so the listing legitimately changed when these files were added.

### Copying a folder into itself

**`FileOps::copy` and `move` refuse a destination inside the source.** `copyTree` lists
each directory's children *after* creating the level below, so copying `a` into `a/b` put
the fresh copy where the next listing would find it and copy it again: down to PATH_MAX,
about a thousand levels, re-writing every file in the source every couple of levels. A
1 GB folder wrote hundreds of gigabytes before failing on ENAMETOOLONG.

Reachable without doing anything unusual — copy a folder, step into one of its subfolders,
paste. `Operations::paste` had no guard of any kind, and `dropUris`'s "already here" check
(which the drag path does have) only compares the immediate parent, so it never saw this.
Spring-loading during a drag reaches it too.

The check is on **canonical** paths, so a symlink or a `..` cannot slip past it, and it
compares against `source + "/"` so that `/home/a` does not swallow `/home/abc`.

**The test asserts nothing was written, not that it failed.** Without the guard the old
code *does* eventually fail — at PATH_MAX — so an assertion on the error message alone
passes either way and proves nothing. What separates fixed from broken is whether
`outer/inner/outer` exists. Confirmed by reverting the guard and watching that one
assertion fail.

### Shell strings

**`Terminal::shellQuote` exists because two places built shell commands by concatenation.**
Remote search sent `fd … -- '<path>'` to `ssh`, quoting the path by wrapping it in single
quotes — which fails on the one character it needs to handle. A directory named `Bob's`
broke the search; a crafted one ran commands on the far end. `connectInTerminal` did the
same with the host alias and mount path into `sh -c`.

Neither could be fixed by "quoting harder": inside single quotes a POSIX shell escapes
nothing at all, so an embedded quote has to close, escape and reopen — `'it'\''s'`. This
is §14's newline-in-a-filename hazard in different clothes, and it applies wherever a
command is a *string* rather than an argument list: `sh -c`, and anything given to ssh,
which always runs its command through the remote shell.

**Tested by running the quoted word back through a real `sh`**, not by comparing strings —
the property is "sh sees one argument, unchanged", and only sh can answer that. Covers
`;`, `&&`, backticks, `$(…)`, a newline, a bare `'`, and the empty string.

### QtQuick.Controls was linked but never used

`omafile.pro` carried `quickcontrols2` from the original skeleton and `main.cpp` called
`QQuickStyle::setStyle("Material")`, while **no QML imports QtQuick.Controls at all** —
`ContextMenu.qml` was written in plain QtQuick specifically to avoid that cost. Both are
gone; `libQt6QuickControls2` and `libQt6QuickTemplates2` are no longer load-time
dependencies (12 Qt libraries down to 10).

**It bought no measurable startup win** — median 113 ms before, 111 ms after, on 13 samples
each, which is inside the noise. Recorded so nobody re-measures it hoping for more. The
change stands on removing dead linkage and a dead call, not on speed.

### Dragging to somewhere that is not on screen

**The breadcrumb accepts drags.** Spring-loading already took you *into* a folder mid-drag,
which was half a feature: there was nothing that took you back out again, and no way at all
to reach a directory above the one in view. Hovering a crumb for 500 ms navigates there,
and dropping on one drops there — so the way out is the same control that shows you where
you are.

The delay is longer than the list's 300 ms because crumbs sit in a row and several get
crossed on the way to the one that was meant, where rows are approached one at a time.
The targets are widened by 6 px horizontally: `~` is eight pixels of glyph, and the reach
also covers the " / " separators so there is no dead gap between them.

**`segmentPath` walks up exactly the way `navigateToSegment` does**, so a drop and a click
can never disagree about which directory a crumb means — pinned by
`breadcrumbSegmentsNameTheirDirectories`, which asserts the two against each other rather
than against a string it built itself.

### Opening things (terminals and editors)

**A window that opens and shuts is this bug, twice over.** "Edit ~/.ssh/config", "edit
omafile config" and bulk rename all flashed, from two independent causes that produce the
identical symptom:

- **$TERMINAL is `xdg-terminal-exec` here, and it does not take `-e`.** The launchers that
  implement the Default Terminal specification take the command directly; only the
  emulators want `-e`. Handed a `-e` it does not understand, xdg-terminal-exec opened
  nothing at all — measured, not guessed.
- **$EDITOR is `code`, which returns in 0.2 s.** A graphical editor hands the file to the
  instance already running and exits, so the terminal wrapped around it exits too. For
  bulk rename this is worse than cosmetic: the edit is "finished" before it has begun, and
  the rename would be applied to an untouched file.

**Opening a file uses the desktop's own editor** — `xdg-open`, the same thing a double
click reaches. omafile does not pick an editor. The single exception is a line number,
which xdg-open cannot express: a *terminal* editor is run with `+N` so a content-search
hit still lands on its line, and anything else opens at the top.

**The list names the terminal editors, not the graphical ones.** Inverting it that way
makes the unknown case safe: an editor nobody here anticipated is handed to the desktop,
which is at worst unhelpful, rather than gambled on a terminal that vanishes. The only
place the distinction cannot be dodged is bulk rename, which waits for the editor to exit
— a graphical editor needs `--wait`, and one that cannot be told to wait is refused with a
message rather than quietly renaming nothing.

**All of it lives in `src/terminal.*` because it previously lived in two places and grew
the same bug in both.** `Operations` and `Places` each had their own copy of "find a
terminal and put `-e` in front". `tst_terminal` pins both halves.

### CI

**The suite does not run as root, and the reason is a real test.** Every GitHub run since
the workflow landed failed on one assertion — `reportsPermissionDenied`, which chmods a
directory to 000 and expects the listing to fail. The container runs as root, and **root
is exempt from the permission bits**, so the listing succeeded and the test was right to
complain. Everything else passed, which is why the failure looked mysterious: eleven
suites green and exit code 1.

Fixed from both ends. The workflow creates the `builder` user before the test step rather
than just before packaging, so the assertion is actually exercised; and the test skips
itself under `geteuid() == 0`, so running the suite as root anywhere reports honestly
instead of failing on behaviour that cannot occur. Verified by reproducing the job in the
same `archlinux:latest` container both ways: as root it skips and exits 0, as `builder` it
runs and passes.

**Reproduce the job locally with docker** rather than guessing at CI: mount the repo
read-only, copy it inside, and run the workflow's steps. Mounting it writable would leave
root-owned build artefacts in the working tree.

### Theme

**Quattro theme format — resolves open decision §16.1.** Quattro (v4.0.0.alpha) deleted the
per-app theme files; a theme is now `colors.toml` + `shell.lock.toml` + a few app files.
The current-theme path also moved: Quattro reads `~/.local/state/omarchy/current/theme/`,
Omarchy 3.x used `~/.config/omarchy/current/theme/`. `Theme` checks both, Quattro first.

**`Theme` is a line-by-line port of omarchy's `bin/omarchy-theme-color`,** the shared
resolver every themed app goes through. Verified byte-identical across all 22 Quattro
themes and the legacy 3.x ANSI format. If it changes upstream, re-run the sweep and re-port.

**Hot reload watches the theme directory's *parent*,** because `omarchy-theme-set` does
`rm -rf current/theme && mv current/next-theme current/theme`, destroying any watch on the
directory itself.

### Browsing and the listing model

**`opendir`/`readdir`, not `QDirIterator`.** Qt's iterator stats entries to satisfy its
filters, and skipping that *is* the §12 listing budget. It is also why filenames with
newlines and quotes work: nothing shells out, so there is nothing to quote.

**Two passes: names now, stats later.** Listing does zero stat calls; size and mtime are
filled in only for visible rows plus a 24-row buffer.

**Batches land in one go rather than streaming.** Measurement says streaming is not needed
yet — 10k entries complete in 7 ms. Revisit north of ~500k.

**The cursor's position is remembered per directory, so a path retraces.** Walking
`Downloads → ~ → /home → /` and then back down puts the cursor on `home`, then
`warforged`, then `Downloads` — each level lands where it was left rather than on the
first row. `goParent` already selected the directory just left; this generalises it to
both directions and to any route, not just the one taken a moment ago.

Precedence when a listing lands: an explicit request (`--select`, the directory just left,
what an operation produced) beats the remembered position, which beats the first row. The
map is bounded at 256 directories and lives in the window, not on disk — say the word if
it should survive a restart.

**The cursor moving and the cursor's row changing are different signals.** They were not,
and it broke scrolling outright: scrolling asks for stats on the newly visible rows, the
stats arrive, `onStatsReady` re-announced `currentIndexChanged` purely so the status bar
could refresh its size text — and the QML handler on that signal calls
`positionViewAtIndex`, dragging the view straight back to the selection. Every scroll
fought its own snap-back.

`currentIndexChanged` now means only "the cursor moved to a different row" and is the sole
thing that scrolls the view; `currentDetailsChanged` means "the current row's contents
changed" and never scrolls anything. `arrivingStatsDoNotMoveTheCursor` pins the invariant,
and the QML side additionally refuses to scroll to an index it is already at.

Fixing it also stopped the preview pane re-decoding the selected file on every stat that
landed during a scroll, which was the same signal being overloaded.

**The watcher diffs; it never resets.** A re-list is merge-walked against the current rows
and applied as single-row inserts and removes, so scroll position survives and the
selection follows the *file*, not the row number.

**The watch is established before the listing starts,** closing the window where a file
created during the initial listing would be missed by inotify forever.

**Backspace edits the filter before it navigates up — a deviation from §5,** which assigns
it to "parent directory" unconditionally. With bare letters typing into the filter,
correcting a typo would otherwise throw you into the parent.

### Packaging

**The PKGBUILD builds from the working tree, not a release tarball.** That is what makes
`./bin/install` a one-liner while the project is still moving; point `source=` at a tag
when it is actually released. `check()` runs the full suite, so a package cannot be built
from a tree whose tests fail.

**`desktop-file-validate` caught two things worth fixing:** `x-directory/normal` is a
deprecated MIME type (only `inode/directory` is needed, which is what §13 specifies
anyway), and listing both `System` and `Utility` gives an application *two* main
categories, which makes it appear twice in a menu. Both removed; the entry now validates
silently.

**CI packages as well as tests.** Building and running the suite proves the code works;
running `makepkg` proves the PKGBUILD still installs what it claims. The latter is the
part that rots silently, because nobody runs it between releases.

**The screenshot is taken with `--select`, not with synthetic keystrokes.** Driving the
cursor with `hyprctl sendshortcut` failed repeatedly and landed on the wrong row; passing
`--select <file>` puts it exactly where the shot needs it, deterministically.

### Remembered UI state

**The window reopens the way you left it** — sidebar and preview pane. Two files, because
they answer different questions and only one is ours to write:

    $XDG_CONFIG_HOME/omafile/config.toml   yours:  "always start with the sidebar open"
    $XDG_STATE_HOME/omafile/state.toml     ours:   "the sidebar was open last time"

Precedence, strongest first: **a command-line flag, then config.toml, then the remembered
state, then off.** So remembering is a convenience that never overrides an explicit
instruction, and `--no-sidebar` behaves the same whatever the last session did. A run
started with an override does *not* write its overridden values back, or a single
`--no-preview` would silently reset the preference.

`config.toml` accepts `on` / `off` / `remember`; `remember` is how to ask for the default
behaviour back explicitly. omafile never writes to that file — §1 rules out a settings UI,
and a file the app rewrites is a settings UI with extra steps.

### Preview, thumbnails, bulk rename and open-with

**Bulk rename plans before it touches anything.** `BulkRename::plan` is pure, so the
awkward cases are testable without a filesystem: a changed line count aborts the whole
edit rather than renaming a prefix of it (§9), and duplicate targets, empty names and
slashes are all refused up front.

**Rename cycles are broken with a temporary hop.** `a→b, b→a` cannot be executed in either
order without destroying a file, so one member is routed through
`.omafile-rename-N-<name>` first. A three-way rotation takes four moves. The proof is not
the unit assertions but `executesCorrectlyOnDisk`, which replays each plan against real
files and checks that every final name holds the content it should — an ordering bug shows
up there as a crossed or missing file, which is exactly how this fails in the wild.

**The whole edit is one journal entry**, recording the user's intent rather than the
temporary hops, so a single Ctrl+Z puts every name back. A failure part-way rolls back
what already ran.

**Previews decode off the GUI thread and honour the selection that asked for them.**
`QImageReader::setScaledSize` is set *before* `read()`, which is what stops a 40 MP photo
being fully decoded to draw it 400 px wide (§11). A decode that lands after the cursor
moved on is dropped by generation, not shown. Text is sniffed by content — a NUL byte in
the first block means binary — rather than by extension.

**The preview image reaches QML through a `QQuickImageProvider`,** not a property or a
temp file, so it is never decoded twice. The URL carries a counter because Qt's pixmap
cache would otherwise keep serving the first image under a name it had already seen.

**The thumbnail cache is verified interoperable, not merely spec-shaped.** The spec hashes
the `file://` *URI*, not the path — get that wrong and every other application's cache
entries are invisible. Proved by taking real entries this machine already had in
`~/.cache/thumbnails/large`, reading their `Thumb::URI` tag, and confirming
`md5(uri) == filename`. Both matched, so omafile finds thumbnails other apps made and
vice versa.

**`QImageWriter::setText` does not round-trip; `QImage::setText` does.** The tags appeared
to write — `write()` returned true and a `tEXt` chunk landed in the file — but reading
them back gave an empty key list every time, so every cached thumbnail was silently
rejected as untagged. Confirmed by testing both APIs side by side in isolation rather than
guessing. Use `QImage`'s pair.

**A cached thumbnail is deliberately not served as the preview.** The cache is 256 px; a
full-pane preview from it would be blurry. The preview decodes properly and *writes* the
cache as a side effect, so the rest of the desktop benefits without omafile showing a
worse picture than it could.

**Choice overlays wrap, and long ones stack.** The choices were laid out in a `Row`,
which cannot wrap — a set wider than the panel simply ran off the edge with no way to
reach the rest. It is a `Flow` now, and "Open with" sets `stacked` so each application
gets its own line; short verb sets like Replace / Skip / Rename / Cancel still read well
side by side.

**Handlers are deduplicated by display name.** One application often registers several
desktop ids — a native package and a Flatpak — all with the same `Name`. Two identical
"Google Chrome" rows cannot be told apart, so only the first survives; precedence has
already put the better one first.

**Open-with only reads the desktop's registry, never writes it.** §1 is explicit that
file-type associations are `xdg-mime`'s job. Handlers come from `mimeapps.list` (user
choices first) then `mimeinfo.cache`, and launching goes through `gio launch` so the
`.desktop` Exec semantics — field codes, `Terminal=true`, startup notification — are
applied by something that already implements them correctly.

**`Ctrl+?` is matched by key and modifier, not as a `Shortcut` sequence.** "?" needs Shift
on most layouts, so the event arrives as **Ctrl+Shift+Question** — which matches neither
`QKeySequence("Ctrl+?")` (Control only) nor `QKeySequence("Ctrl+Shift+/")` (which wants
`Key_Slash`, not `Key_Question`). Both were bound and neither ever fired; only F1 worked.
Testing `Key_Question || Key_Slash` with the Control modifier in `Keys.onPressed` works on
any layout. It stays in the shortcut table so the overlay still documents it.

**QML paints in declaration order, so an overlay must be declared after what it covers.**
The help overlay was declared next to `Shortcuts` — before the list — and so rendered
*underneath* it: the file list showed straight through a backdrop set to 0.97 opacity.
All modal surfaces now live together at the end of Main.qml for that reason.

**The `Ctrl+?` overlay is generated from `Shortcuts.qml`'s table.** §5 asks for exactly
this: the documentation cannot drift from the bindings, because adding a shortcut adds a
line to the overlay and removing one removes it. The single binding it cannot show is bare
letters, which are handled in `Keys.onPressed` and noted in the footer by hand.

### Remote

**omafile keeps no host list of its own.** Hosts come from `~/.ssh/config` (following
`Include`, with a depth limit so a self-including config cannot hang) and from
`known_hosts` as a secondary source. Only concrete aliases are offered: `Host *` and
`!negated` patterns describe rules, not places. Hashed `known_hosts` entries are skipped
because the name genuinely cannot be recovered from them.

**The alias is what gets handed to ssh, never a rebuilt `user@host`.** The alias carries
the whole config entry with it — `ProxyJump`, `IdentityFile`, `Match` — and reconstructing
a target from parsed fields would silently throw all of that away.

**gvfs is only used for hosts omafile knows nothing about.** A host written down in
`~/.ssh/config` may carry an `IdentityFile`, a `ProxyJump` or a `Match` block, and **gvfs
reads none of that** — it would connect with the wrong key or the wrong user and fail.
Those go through real ssh/sshfs, which honours the whole entry. gvfs is reserved for hosts
known only from `known_hosts` or typed as a URI, where there is no configuration to lose
and its password dialogs are the only way in. `SshHost::needsOpenSsh()` is the check.

**A host with no config entry cannot be connected to, and that is correct.** omafile keeps
no host list of its own (§10.1): it reads OpenSSH's. A machine present only in
`known_hosts` has no user and no key recorded anywhere, so the fix is a `Host` block —
which then works for `ssh`, `scp` and `rsync` too, not just here.

**The mount is `host:/`, the remote root — not `host:`, which is only the remote home.**
§10.1 says root, and it matters twice: with a home-rooted mount there is nothing above
`/home/<user>` to navigate to, and `ssh://host/etc/nginx` would resolve to `~/etc/nginx`
instead of `/etc/nginx`. Opening a host still *lands* in the remote home when it can be
identified, because a filesystem root is a useless place to arrive; the rest of the
machine is then simply up.

**An omafile-made mount root is a navigation boundary.** Walking up past it led into
`$XDG_RUNTIME_DIR/omafile`, which is plumbing rather than a place. The mount root *is*
"the server", so `goParent` stops there like it stops at `/`.

**Remote-side search falls back to a bounded local walk.** §10.1 runs `ssh host fd`, but
plenty of servers have no `fd` installed. When the remote command fails and returns
nothing, omafile walks the mount itself with `--max-depth 6` — §10.6's "bounded
depth-limited walk", never an unbounded crawl over a slow link. A depth-limited walk is
deliberately not cached as complete.

**Orphaned mounts are swept at startup.** A window killed with SIGTERM never runs its
destructor, so its claim and its mount survive it. On start, any mount whose claimants are
all dead processes is unmounted and its refcount directory removed — §14's "no orphaned
mounts left in $XDG_RUNTIME_DIR after an unclean exit". Verified with a fabricated claim
from a PID that cannot exist.

**The sidebar's contents are built on first use, not at construction.** Populating it
costs PATH lookups for sshfs/rclone/gio/udisks, an `~/.ssh/config` parse and a
`/proc/self/mountinfo` read; a window whose sidebar is closed should pay none of that.

**Mounts are refcounted across windows as one file per PID** in
`$XDG_RUNTIME_DIR/omafile/.refs/<key>/`. A claim from a process that no longer exists is
self-evidently stale and gets cleaned up, which is what stops an unclean exit leaving an
orphaned mount forever (§14).

**Search runs on the far end.** When the location is inside an sshfs mount omafile made,
the walk is `ssh host fd …` and the returned paths are rewritten back onto the mount, so
everything downstream sees an ordinary local path. §10.1 calls this the single biggest
reason to build a remote path at all; walking sshfs directly is agonising.

**No trash on a network mount** (§10.6). `Operations::trash` refuses and says so, and the
Delete key routes to the permanent-delete confirmation instead. Silently creating a
`.Trash-$uid` on someone's server would be worse than refusing.

**SSH mounts go through gvfs, not sshfs — the same way every other file manager on this
desktop does it.** `gio mount sftp://…` is what Nautilus, Files and Thunar use, and the
reason matters: **gvfs owns the authentication dialogs.** A password, a key passphrase, an
unknown host key, keyboard-interactive 2FA — gvfs can ask for all of them, and omafile
never touches a credential (§10.7). sshfs cannot ask the user *anything*: it has no
terminal and no dialog, which is why every failure arrived as an unexplained
"connection reset by peer".

§10.1 specified sshfs, and that was the wrong default for anything but key-only hosts.
sshfs is kept for the one case gvfs cannot serve: a host needing `ProxyJump`, which only
real ssh understands. gvfs does not read `~/.ssh/config` either, so the alias is resolved
to host/user/port from the config omafile already parsed before handing it over.

**When nothing automatic works, the user gets a terminal.** `connectInTerminal` runs sshfs
in a real terminal with no BatchMode and no `password_stdin`, so ssh can prompt for
whatever it likes and be answered directly; omafile then polls for the mount to appear and
navigates there. It is offered from the failure dialog rather than being a dead end. This
is the honest answer for a Cloudflare Access device flow or a hardware token, which no
amount of piping can automate.

**sshfs is asked to mount only after `sftp` has been asked what would happen.** sshfs
discards ssh's stderr and reports *every* failure as `read: Connection reset by peer` — an
unresolvable host, a refused connection, a missing sftp subsystem and "this server wants a
password" are literally indistinguishable through sshfs alone.

The probe is **`sftp -o BatchMode=yes -b /dev/null <alias>`, not `ssh <alias> true`**.
sshfs mounts over the *sftp subsystem* (`-s sftp` → `/usr/lib/sftp-server`), so a
locked-down box can answer `ssh host true` perfectly well and still have no sftp-server —
which is exactly the case that produced a bare "connection reset by peer" with no
explanation. Probing with ssh proves the wrong thing.

**A reset at mount time is not evidence of an authentication problem.** Treating it as one
was a real bug: the probe has already ruled auth out by then, so it prompted for a
password on a key-only server, sent it via `password_stdin`, and failed again with the
identical message. Only genuine auth signatures (denied / authenticat / password /
passphrase) trigger the prompt now.

**sshfs is never allowed to prompt on a terminal that does not exist.** The first mount
attempt adds `BatchMode=yes`, so a host that needs a password fails *immediately and
legibly* instead of hanging until the timeout on an invisible prompt. Only then does
omafile ask, masked, and retry with `-o password_stdin`, writing the password to the
child's stdin and closing the pipe. It is used once and never stored — no member field
holds it, and it is deliberately kept out of the status line and every log (§10.7).
A failure that does not look like an authentication problem is reported as-is rather than
prompting, so an unreachable host does not ask for a password it cannot use.

**Soft dependencies gray out, they never fail.** `rclone` is *not* installed on this
machine (`sshfs` now is), so the honest-degradation paths are the ones actually exercised here: SSH
hosts still appear in the sidebar carrying "install sshfs to browse", and rclone remotes
simply do not appear. That is §10.1/§10.3's requirement, and it is the default experience
of anyone who has not installed the optional tools.

### Search

**The scorer is a pure `(needle, haystack) -> (score, positions)` function** with no fzf
dependency, so ranking quality is directly testable — §14 calls it the highest-value test
in the repo, and it caught two real ranking deficiencies during development.

**Ranking is tuned by a chain of tie-breakers, in this order:** a match inside the
basename beats one smeared across parent directories (+96, more than any arrangement of
characters can make up); then shallower paths; then shorter names. The two failures worth
recording:
- *Exactness.* Without a length penalty, `main.cpp` and `main.cpp.bak` score identically
  for the query `main.cpp`, because both match consecutively from position 0.
- *That penalty then swamped everything else.* Uncapped, it made a long well-structured
  name (`DirectoryModel.cpp`, matching `dm` on a camelCase boundary) lose to a short mushy
  one (`downmix.cpp`). It is capped at 8 so length only ever breaks a near-tie.

**Smart case:** an all-lowercase needle matches case-insensitively; any uppercase
character makes the whole match case-sensitive.

**`fd --print0`, not newline-delimited output.** §14 names a filename containing a newline
as the classic breakage for anything that shells out, and search is the *only* place
omafile does. Covered by a test that plants such a file and requires it back.

**The warm cache is what makes the second search instant** (§6): a completed walk is kept
as a path list and re-ranked in memory with no process at all — measured at 0 ms against
4 ms for the cold walk. Only a *complete* walk is cached; a cancelled one would answer
later queries from a truncated tree. The watcher invalidates it on any directory change.

**Every keystroke cancels the walk outright and drops its results.** Same generation
counter as the `Lister`. Results are replaced rather than appended on each flush, because
ranking is global — a path found late can outrank everything already on screen.

**Search construction is deferred until the first search** (§12). Building the engine and
its thread eagerly is a cost every window pays for a feature most never use.

**`bin/test` measures startup *before* running the suite.** The 100k-file search budget
test writes and then deletes that tree, and measuring cold start while the kernel worked
through it produced a phantom 60 ms regression with 600 ms outliers. Ordering fixed it;
more samples alone did not.

**A filtered list is a ranking, not a listing.** With a filter active, rows are ordered by
score and directories keep priority only as a tiebreak. That breaks the watcher's
merge-walk invariant (which assumes both lists share the sort comparator), so a
watcher-triggered rebuild resets instead of diffing while a filter is active.

### Operations, clipboard and drag

**Trash is implemented directly against the XDG spec, not delegated to `gio`.** The point
is two-way interop: Nautilus and gio can restore what omafile trashed, and vice versa.
Confirmed live — trashing from `/tmp` correctly used the volume trash
`/tmp/.Trash-$uid` with a **mount-relative** `Path=`, which is what makes the trash still
work if the volume is mounted elsewhere next time.

**The `.trashinfo` file is the lock.** It is created with `O_EXCL`, and whoever wins that
race owns the name. Reserving the name any other way races with a second file manager.

**Conflicts block the worker on a condition variable.** §8 wants Replace / Skip / Rename /
Apply-to-all and explicitly rejects per-file interrogation. Pre-scanning cannot see
conflicts that appear mid-operation, so the worker parks and the GUI answers. `cancel()`
also wakes it, or a cancelled operation would hang forever holding the ops thread.

**Copying a symlink uses `readlink`, not `QFileInfo::symLinkTarget`.** Qt resolves the
target to an absolute path, which silently rewrites a relative link to point back at the
*source* tree — so every internal link in a copied directory would still refer to the
original. Caught by a test, not by reading.

**Undo trashes copies rather than deleting them.** Undo must never be the destructive
operation. Permanent delete is not journalled at all, which is exactly what the confirm
overlay promises.

**Selection is tracked by name, not row index,** so it survives a watcher diff, a filter
change and a re-sort. `actionPaths()` returns the explicit selection, or the current row
when nothing is selected — every verb goes through it, so "no selection" never means
"do nothing to everything".

**The clipboard writes both `text/uri-list` and `x-special/gnome-copied-files`.** uri-list
is universal but cannot express cut versus copy; the GNOME format can, and Nautilus,
Thunar and Dolphin all read it. That is what makes Super+X into another file manager work.
A cut is consumed by its paste, so it cannot fire twice.

**An item that has just become visible cannot take active focus in the same event-loop
turn.** The overlay looked focused (`selectAll()` renders a selection without focus) while
every keystroke went to the window behind it. Focus is now taken via `Qt.callLater`, and
the overlay's `FocusScope` sets `focus: visible` so it becomes the window's focused child.
This cost real time; treat any "the field is up but typing does nothing" symptom as this.

**Two theme-robustness computations, same root cause.** Several themes resolve
`lighter_background` and `selection` straight back to `background`. So the modal panel
colour and the selected-row tint are both computed rather than taken literally, and the
rename field's selection colour is deliberately *not* the row tint — on the row being
renamed those are the same colour, which made an active edit field look like ordinary text.

**Omarchy's universal clipboard does not send Ctrl+C.** Hyprland grabs Super+C/V/X
globally and re-sends the *terminal-safe* sequences to the active window:

    Super+C -> Ctrl+Insert      Super+V -> Shift+Insert      Super+X -> Ctrl+X

So an application never sees Super at all, and binding `Meta+C` — which is what omafile
did — can never fire. Binding `Ctrl+Insert`/`Shift+Insert` is what actually implements
§1's "the Omarchy convention Nautilus can't do"; presumably not handling those is exactly
why Nautilus can't. Check with `hyprctl binds | grep -A6 'modmask: 64'` before assuming
any Super binding reaches the app.

**Drag-out was broken four ways at once** (reported from real use, after drag and drop was called done):
`Drag.start()` is the *internal*-drag API and never reaches another application —
`Drag.startDrag()` is the one that creates a platform drag; there was no
`Drag.imageSource`, so nothing appeared under the cursor; a `DragHandler` sat alongside
the row's `MouseArea` and the two fought over the press so neither owned it; and the
uri-list was built with JavaScript's `encodeURI`, which leaves `#` and `?` unescaped and
silently truncates such paths in the receiving app. The drag now starts from the
`MouseArea` that already holds the press, after an 8 px threshold, and the cursor image
comes from `grabToImage` on the row — started from inside that callback, because the grab
lands a frame later and starting before it leaves nothing attached to the pointer. The URI
list is built in C++ (`Operations::uriList`) and unit-tested against `#`, `?`, `%`, spaces.

**A `DropArea` declared inside a `ListView` lands in the wrong place.** A ListView is a
Flickable, so declared visual children are parented to its `contentItem` — which is sized
to the *content*, not the viewport, and moves as you scroll. `anchors.fill: parent` there
fills the scrolling content, leaving the drop target somewhere other than where the list
appears. The DropArea and the empty-state label are now siblings anchored to the list.
Verified by forcing the drop highlight visible and confirming it spans the viewport.

**The row's MouseArea sets `preventStealing: true`.** Without it the Flickable steals the
press, which both scrolled the list when you meant to drag a file and killed the drag
before it could start. In a file manager, dragging a row means dragging the file; the
wheel scrolls.

**`Drag.startDrag()` needs `Drag.active = true` first.** With `Drag.Automatic`, setting
`active` only *arms* the drag; `startDrag()` executes it and refuses outright ("startDrag()
drag must be active") if it was never armed. Both lines, in that order, or nothing ever
attaches to the cursor.

**The drag source lives at window level, never in the delegate.** `startDrag()` spins a
nested event loop; an internal drop refreshes the model; the ListView then destroys the
delegate the loop is about to return into. That crashed the app on every internal drag.
The proxy item and the drag image now belong to the window, which outlives any row.

**The drag image is a composed badge, not the row.** A row is the full width of the
window, so grabbing it produced a drag image spanning the screen. The badge is built
off-screen and sized from `TextMetrics` rather than from its laid-out `Row` width —
layout has not run at prepare time, so reading the child's width there yields a 24 px
sliver. `Qt.callLater` is *not* late enough to dodge that; only measuring the text is.

**An operation's results end up selected.** What was produced is derived from the journal
entry rather than guessed before the operation runs, so a file that conflict-resolution
renamed to `foo (2).txt` is still the one selected afterwards.

**Range selection is anchor-based.** `Shift+click` and `Shift+Up/Down` both select the run
between the anchor and the cursor; plain moves reset the anchor, extending moves
deliberately do not, so `Shift+Down Down Down` grows one run instead of toggling pairs.
Ranges add to an existing `Ctrl+click` selection rather than replacing it.

**Window close is blocked while an operation runs** (§8's "dead simple wins"), rather than
detaching the operation.

---

## To verify

**RSS is 131 MB against a 90 MB budget — the one §12 miss.** Most of it is not ours:
`Pss_Anon` (the app's own dirty memory) is **27 MB**; ~45 MB is Mesa gallium + LLVM code
pages, file-backed and shared with every other GL client. Decide whether §12's number
should be restated as private memory — in which case we are comfortably inside — or
whether it really means RSS, which may be unreachable for any Qt Quick app on this stack.
Don't optimize until that is settled.

**Drag-out works** (confirmed in real use): out to external applications, and internally
without crashing. What is still unverified is §7's release checklist proper — a GTK app,
an Electron app and a Chromium tab specifically — and drops *into* omafile from each.

**The multi-select drag image is a count badge, not §7's stack of rows.** A multi-file
drag reads "12 items" rather than showing overlapping row thumbnails.

**Cross-filesystem copy (the `EXDEV` path) has no test.** It needs a second filesystem to
exercise; the same-filesystem `rename(2)` path is covered and verified by inode.

**The conflict overlay has never been seen.** Its logic is tested headlessly (skip,
replace, rename, apply-to-all) but the panel itself has not been rendered once.

**`hyprctl dispatch sendshortcut` is unreliable — roughly one dispatch in three lands.**
It cost most of an afternoon chasing phantom bugs: several "broken" features were verified
working once the dispatch actually arrived, and F2 and Ctrl+1 never arrive at all. Do not
trust it as evidence that something is broken; confirm with logging before believing it.

**The startup budget is 140 ms** (§12, `bin/test`, `README.md`), raised from 120 at the
user's request — but the raise was the smaller half of the fix. `bin/test` was failing
about half the time, and chasing that turned up a flaw in the harness rather than in the
code: **startup was being sampled straight out of `make -j$(nproc)`**. The comment at the
top of `measure_startup` already warned about exactly this hazard from the other side (the
100k-file tree the search test writes), and the build immediately above it was doing the
same thing unremarked. Same binary, seconds apart: `bin/test` read a median of **175 ms**
while a manual loop read **100 ms**.

So `measure_startup` now settles for fifteen seconds and throws away two warm-up runs
before sampling — five seconds was enough when it built one test binary and stopped being
enough when the QML suite made that two, and the budget sits at 140 with the measurement honest — median **99 ms**, and a
number that would actually notice a regression. 160 was tried first, purely to out-wait
the noise; once the noise had a cause, that was no longer necessary.

The creep is still real — 86 ms for the first empty window, ~100 ms now — and profiling what is constructed
before first paint is still the honest fix. Judge it on a settled machine: the same binary
reads 100 ms idle and 175 ms under a compile.

**~~No right-click has been synthesized.~~ All four surfaces are now driven by real
clicks** — see "Driving the pointer" above. Verified: right-click on a row, on blank space,
on the strip beside the breadcrumb, and on a sidebar place; left- and right-click
pass-through while a menu is open; clicks on the panel itself being swallowed; and a menu
*action* running end to end ("Copy path" on `src` put `/home/warforged/Projects/Omafile/src`
on the clipboard).

**The other menu actions still have not been clicked.** Their callees are all confirmed to
exist and be `Q_INVOKABLE`, each is a verb that already works from the keyboard, and the
one that was exercised worked — but the arrow functions are lazy, so an argument-order
mistake inside one would not surface until that row is chosen. "New file" is covered
headlessly by a test; the rest are covered by inspection only.

**§16.3 is still open.** `Space` toggles selection (launcher-like) rather than extending
it. The plan says try both for a week.

**Quattro paths are only tested against synthetic fixtures** — this machine runs 3.8.3.

**Operation summaries could read better.** "Copied 3 to demo" is terse and does not name
the files when there is only one.

**Native SFTP is not built, deliberately.** The plan's own advice was to ship the
mount-based path first and live on it — a native backend is worth building only once you
have felt where sshfs is actually too slow — and §16.5 leaves the enabling decision open
pending a measurement: `libssh2` vs `libssh` vs
`ssh -W` + stdio SFTP, "confirm by measuring connection setup time on a real tunnel before
committing". That measurement cannot be made here — there is no sshfs, no test host, and
§14 wants the backend tested against a real sshd in a container. Building it now would be
speculative work against an unresolved decision. It needs a real remote box and a week of
living on the mount-based path first.

**~~The sidebar has never been seen.~~ It has now** — captured while screenshotting the
context menus: home, Downloads, Documents, Pictures, then the SSH hosts and remotes, all
rendering with the right glyphs and indentation. What is still unseen is its *behaviour*:
`Ctrl+B` toggling, clicking a place, and the right-click menu reaching it.

**Connecting blocks the GUI thread** for as long as the ssh probe plus the mount takes —
bounded at roughly 8 s by `ConnectTimeout`, but still a freeze. Mounting should move to a
worker like every other slow operation; it has not yet.

**The password retry is still unrun.** The probe and its classification are verified
against a real unresolvable host, but `-o password_stdin` has never run against a server
that actually asks for a password. A key *passphrase* (as opposed to a host password) may
additionally need `SSH_ASKPASS`, which is not implemented — `ssh-agent` covers it in
practice on Omarchy.

**~~Mounting has never been executed end to end.~~ It has now** — verified against a real
Oracle Cloud host: omafile mounted it over sshfs itself, browsed it, and filled in sizes
and mtimes over the link. The remaining unrun paths are rclone, gio/SMB/WebDAV/MTP, and
the password prompt (that server is publickey-only).

**Superseded note —** Without sshfs or rclone installed, `mountSsh` and
`mountRclone` have only ever taken their "not installed" branch. The gio path is likewise
unrun. The refcount logic, the option strings and the gvfs path resolution are all
unproven against a real mount.

**Thumbnails are cached but never displayed in the list.** The cache is written and is
interoperable, but the file list still shows type glyphs only — §4 is emphatic about
"Nerd Font glyphs for file types, not an icon theme. One color, one weight, no rainbow",
and photo thumbnails in a 32 px row would contradict that. §11's "generate only for
visible rows" therefore has no consumer yet. Whether the list should show them at all is
really §16.7's open question; the machinery is ready either way.

**Open-with has not been driven by hand.** The parsing is tested, but the chooser overlay
has never been opened against a real MIME type.

**Startup has crept: 86 ms for the first empty window, ~100 ms now** — see the budget note above. The
measurement is extremely sensitive to machine load: the same binary measures 95 ms idle
and 167 ms immediately after a `makepkg` run. Judge it only on a settled machine, and take
the median of a dozen samples, not three.

**Bulk rename's `$EDITOR` round trip has not been run by hand.** The planning is covered by
19 tests including on-disk replay, but launching a terminal, editing, and applying the
result on exit has only been exercised through code review.

**Find-mode UI is verified only headlessly.** `Ctrl+F` was confirmed on screen (ranked
results, bolded matches, "27 results · of 382 scanned"), but `Ctrl+Alt+F` never landed
through `hyprctl sendshortcut`, so content search is proven by its tests and not by use.
Worth driving by hand.

**The MRU recency boost from §6 is not implemented.** Scoring has no notion of recently
opened paths yet.

**`plocate` whole-filesystem search is written but untested** — the `/`-prefixed query
path has no coverage and plocate is not installed here.

**Sort bindings were never in §5's table.** Either add `Ctrl+1/2/3` to it or pick
different keys.

---

## Conventions

- C++17, Qt 6, qmake6. No CMake.
- Comments explain *why*, and appear where behavior is non-obvious or mirrors an external
  contract. Don't narrate the code.
- Shell scripts in `bin/` follow Omarchy's style: `#!/bin/bash`, two-space indent,
  `[[ ]]` for strings, `(( ))` for numbers.
- Tests run headless under `QCoreApplication` and use a fake `$HOME`/`$XDG_DATA_HOME` or a
  `QTemporaryDir`. The trash tests in particular **must** keep the fake HOME — without it
  they move files into the developer's real trash.
- The GUI thread does not touch the filesystem. Documented exceptions: `Theme` (one small
  read before first paint) and `QFileSystemWatcher::addPath`, which is why the `Watcher`
  lives on the worker thread.
- Two worker threads, per §3: one for listing/watching (owned by `DirectoryModel`), one for
  file operations (owned by `Operations`).
