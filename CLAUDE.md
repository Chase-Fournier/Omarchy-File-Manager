# CLAUDE.md

Working notes for omafile. The spec is [omafile-plan.md](omafile-plan.md) — read it first;
this file records what is actually built, what was decided along the way, and what still
needs checking.

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
src/qml/Main.qml      the window
src/qml/EntryRow.qml  one row: name, drag source, inline rename
src/qml/Overlay.qml   the single modal surface
src/qml/Shortcuts.qml the keyboard map, single source of truth (§5)
tests/                one binary, one suite per class, registered in tests/main.cpp
```

---

## Progress

### M0 — Skeleton ✅
Repo per §2, qmake6 with `qtquickcompiler`, `Theme`, themed window, startup budget in
`bin/test`.

### M1 — Browse ✅
`Lister`, `Watcher`, `DirectoryModel`, `Location`, breadcrumb, status bar, keyboard
navigation, type-to-filter with match highlighting, Enter/xdg-open, hidden files, sorting,
`--select`.

### M2 — Act ✅
Multi-select, copy/cut/paste (Super and Ctrl), drag and drop both directions, XDG trash,
undo, new folder, inline rename, terminal-here, copy path, conflict resolution, progress
line. **91 tests pass.**

### M3 — Find ✅
All three tiers of §6: fuzzy in-directory filter, `Ctrl+F` recursive name search over
`fd` with streaming and a warm cache, `Ctrl+Alt+F` content search over ripgrep with line
numbers and previews. **134 tests pass.**

### M4a — Remote by mount ✅
`~/.ssh/config` and `known_hosts` parsing, the sidebar (`Ctrl+B`), sshfs/rclone/gio mount
lifecycle with cross-window refcounting, `/proc/self/mountinfo` for NFS and removable
media, `Ctrl+S` connect-to, `Ctrl+D` bookmarks, `Ctrl+E` eject, remote-side search, and
§10.6's no-trash-on-remote policy. **151 tests pass.**

### M4b — Native SFTP — deliberately not started
See "To verify" below: the plan makes it conditional, and its enabling decision (§16.5)
is unresolved and unmeasurable on this machine.

**Measured against §12:**

| Budget | Target | Actual |
|---|---|---|
| Cold start to first paint | < 120 ms | **112 ms** median (7 runs) |
| 10k-entry directory, complete | < 150 ms | **7 ms** |
| Keystroke → filtered list | < 5 ms | **1 ms** (fuzzy, 10k entries) |
| First search result (§6) | < 30 ms | **3 ms** |
| 100k-file tree walked (§6) | < 400 ms | **61 ms** |
| RSS, idle, one window | < 90 MB | **131 MB** ✗ — see below |

---

## Decisions

### Theme (M0)

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

### Browse (M1)

**`opendir`/`readdir`, not `QDirIterator`.** Qt's iterator stats entries to satisfy its
filters, and skipping that *is* the §12 listing budget. It is also why filenames with
newlines and quotes work: nothing shells out, so there is nothing to quote.

**Two passes: names now, stats later.** Listing does zero stat calls; size and mtime are
filled in only for visible rows plus a 24-row buffer.

**Batches land in one go rather than streaming.** Measurement says streaming is not needed
yet — 10k entries complete in 7 ms. Revisit north of ~500k.

**The watcher diffs; it never resets.** A re-list is merge-walked against the current rows
and applied as single-row inserts and removes, so scroll position survives and the
selection follows the *file*, not the row number.

**The watch is established before the listing starts,** closing the window where a file
created during the initial listing would be missed by inotify forever.

**Backspace edits the filter before it navigates up — a deviation from §5,** which assigns
it to "parent directory" unconditionally. With bare letters typing into the filter,
correcting a typo would otherwise throw you into the parent.

### Remote (M4a)

**omafile keeps no host list of its own.** Hosts come from `~/.ssh/config` (following
`Include`, with a depth limit so a self-including config cannot hang) and from
`known_hosts` as a secondary source. Only concrete aliases are offered: `Host *` and
`!negated` patterns describe rules, not places. Hashed `known_hosts` entries are skipped
because the name genuinely cannot be recovered from them.

**The alias is what gets handed to ssh, never a rebuilt `user@host`.** The alias carries
the whole config entry with it — `ProxyJump`, `IdentityFile`, `Match` — and reconstructing
a target from parsed fields would silently throw all of that away.

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

**Soft dependencies gray out, they never fail.** `sshfs` and `rclone` are *not* installed
on this machine, so the honest-degradation paths are the ones actually exercised here: SSH
hosts still appear in the sidebar carrying "install sshfs to browse", and rclone remotes
simply do not appear. That is §10.1/§10.3's requirement, and it is the default experience
of anyone who has not installed the optional tools.

### Find (M3)

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

### Act (M2)

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

**Drag-out was broken four ways at once** (reported from real use, after M2 "shipped"):
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

**Startup headroom is thinning:** 112 ms against 120 ms, up from 104 ms in M1.

**§16.3 is still open.** `Space` toggles selection (launcher-like) rather than extending
it. The plan says try both for a week.

**Quattro paths are only tested against synthetic fixtures** — this machine runs 3.8.3.

**Operation summaries could read better.** "Copied 3 to demo" is terse and does not name
the files when there is only one.

**M4b (native SFTP) is not built, deliberately.** §15 says to "ship M4a first and live on
it — M4b is worth building only once you've felt where sshfs is actually too slow", and
§16.5 leaves the enabling decision open pending a measurement: `libssh2` vs `libssh` vs
`ssh -W` + stdio SFTP, "confirm by measuring connection setup time on a real tunnel before
committing". That measurement cannot be made here — there is no sshfs, no test host, and
§14 wants the backend tested against a real sshd in a container. Building it now would be
speculative work against an unresolved decision. It needs a real remote box and a week of
using M4a first.

**The sidebar has never been seen.** `Ctrl+B` was wired and the build runs clean, but the
session locked before it could be captured. Everything behind it is unit-tested; the
rendering is not.

**Mounting has never been executed.** Without sshfs or rclone installed, `mountSsh` and
`mountRclone` have only ever taken their "not installed" branch. The gio path is likewise
unrun. The refcount logic, the option strings and the gvfs path resolution are all
unproven against a real mount.

**No bulk rename yet** (§9's vimv approach) — that is M5, along with the `Ctrl+?` overlay
that will be generated from `Shortcuts.qml`'s table.

**Find-mode UI is verified only headlessly.** `Ctrl+F` was confirmed on screen (ranked
results, bolded matches, "27 results · of 382 scanned"), but `Ctrl+Alt+F` never landed
through `hyprctl sendshortcut`, so content search is proven by its tests and not by use.
Worth driving by hand.

**The MRU recency boost from §6 is not implemented.** Scoring has no notion of recently
opened paths yet.

**`plocate` whole-filesystem search is written but untested** — the `/`-prefixed query
path has no coverage and plocate is not installed here.

**Sort bindings are not in the plan's §5 table.** Either add `Ctrl+1/2/3` to it or pick
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
