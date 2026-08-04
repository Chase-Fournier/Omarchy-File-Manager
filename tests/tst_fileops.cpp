#include "tst_fileops.h"

#include "fileops.h"
#include "journal.h"
#include "operations.h"
#include "trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <functional>
#include <sys/stat.h>
#include <unistd.h>

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

ino_t inodeOf(const QString &path)
{
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0)
        return 0;
    return info.st_ino;
}

} // namespace

QString TestFileOps::path(const QString &relative) const
{
    return m_root.path() + QLatin1Char('/') + relative;
}

void TestFileOps::write(const QString &relative, const QByteArray &contents)
{
    const QString full = path(relative);
    QVERIFY(QDir().mkpath(QFileInfo(full).absolutePath()));
    QFile file(full);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
    file.write(contents);
}

void TestFileOps::initTestCase()
{
    QVERIFY(m_root.isValid());
    m_realHome = qgetenv("HOME");
    m_realDataHome = qgetenv("XDG_DATA_HOME");

    // Both, because the trash location is derived from XDG_DATA_HOME with $HOME as the
    // fallback, and a stray real-trash write would be a genuinely bad test failure.
    const QString fakeHome = m_root.path() + QStringLiteral("/home");
    QVERIFY(QDir().mkpath(fakeHome + QStringLiteral("/.local/share")));
    qputenv("HOME", fakeHome.toLocal8Bit());
    qputenv("XDG_DATA_HOME", (fakeHome + QStringLiteral("/.local/share")).toLocal8Bit());
}

void TestFileOps::cleanupTestCase()
{
    qputenv("HOME", m_realHome);
    if (m_realDataHome.isEmpty())
        qunsetenv("XDG_DATA_HOME");
    else
        qputenv("XDG_DATA_HOME", m_realDataHome);
}

void TestFileOps::init()
{
    const QDir work(path(QStringLiteral("work")));
    if (work.exists())
        QDir(work.absolutePath()).removeRecursively();
    QVERIFY(QDir().mkpath(work.absolutePath()));
}

// Drives one operation to completion. FileOps runs on the calling thread here, so a
// conflict signal is delivered synchronously and a connected handler can answer it
// before askConflict ever waits.
JournalEntry TestFileOps::runOperation(const std::function<void(FileOps *, quint64)> &call,
                                       QString *failure)
{
    FileOps ops;
    JournalEntry result;
    QString error;

    QObject::connect(&ops, &FileOps::finished, &ops,
                     [&result](quint64, const JournalEntry &entry) { result = entry; });
    QObject::connect(&ops, &FileOps::failed, &ops,
                     [&error](quint64, const QString &message) { error = message; });

    call(&ops, 1);

    if (failure)
        *failure = error;
    return result;
}

void TestFileOps::trashMovesFileAndWritesInfo()
{
    write(QStringLiteral("work/notes.txt"), "hello");
    const QString original = path(QStringLiteral("work/notes.txt"));

    Trash::Item item;
    QString error;
    QVERIFY2(Trash::moveToTrash(original, &item, &error), qPrintable(error));

    // The file is gone from where it was, and present in the trash.
    QVERIFY(!QFileInfo::exists(original));
    QVERIFY(QFileInfo::exists(item.trashedPath));
    QCOMPARE(readAll(item.trashedPath), QByteArray("hello"));
    QVERIFY(item.trashedPath.contains(QStringLiteral("/files/")));

    // The .trashinfo is what lets gio and Nautilus restore it too.
    const QByteArray info = readAll(item.infoPath);
    QVERIFY(info.startsWith("[Trash Info]"));
    QVERIFY(info.contains("Path=" + original.toUtf8()));
    QVERIFY(info.contains("DeletionDate="));
}

void TestFileOps::trashPercentEncodesTheOriginalPath()
{
    write(QStringLiteral("work/a b#c%d.txt"));

    Trash::Item item;
    QString error;
    QVERIFY2(Trash::moveToTrash(path(QStringLiteral("work/a b#c%d.txt")), &item, &error),
             qPrintable(error));

    const QByteArray info = readAll(item.infoPath);
    // Spaces, '#' and '%' are escaped; '/' stays readable, per the spec.
    QVERIFY(info.contains("a%20b%23c%25d.txt"));
    QVERIFY(info.contains("Path=/"));

    // And it survives the round trip back to the original name.
    QVERIFY(Trash::restore(item, &error));
    QVERIFY(QFileInfo::exists(path(QStringLiteral("work/a b#c%d.txt"))));
}

void TestFileOps::trashDisambiguatesCollidingNames()
{
    write(QStringLiteral("work/one/report.txt"), "first");
    write(QStringLiteral("work/two/report.txt"), "second");

    Trash::Item first;
    Trash::Item second;
    QString error;
    QVERIFY(Trash::moveToTrash(path(QStringLiteral("work/one/report.txt")), &first, &error));
    QVERIFY2(Trash::moveToTrash(path(QStringLiteral("work/two/report.txt")), &second, &error),
             qPrintable(error));

    // Neither may clobber the other, and both must still be readable.
    QVERIFY(first.trashedPath != second.trashedPath);
    QVERIFY(first.infoPath != second.infoPath);
    QCOMPARE(readAll(first.trashedPath), QByteArray("first"));
    QCOMPARE(readAll(second.trashedPath), QByteArray("second"));
    QVERIFY(QFileInfo(second.trashedPath).fileName().contains(QStringLiteral("report")));
}

void TestFileOps::trashRestoresToItsOriginalPath()
{
    write(QStringLiteral("work/restore-me.txt"), "payload");
    const QString original = path(QStringLiteral("work/restore-me.txt"));

    Trash::Item item;
    QString error;
    QVERIFY(Trash::moveToTrash(original, &item, &error));
    QVERIFY(!QFileInfo::exists(original));

    QVERIFY2(Trash::restore(item, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(original));
    QCOMPARE(readAll(original), QByteArray("payload"));
    // Restoring must also clean up the info file, or the trash grows phantom entries.
    QVERIFY(!QFileInfo::exists(item.infoPath));
}

void TestFileOps::copiesFileContentsModeAndMtime()
{
    write(QStringLiteral("work/src/data.bin"), QByteArray(5000, 'z'));
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/dst"))));

    const QString source = path(QStringLiteral("work/src/data.bin"));
    QVERIFY(QFile::setPermissions(source,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner));

    struct stat before;
    QCOMPARE(::lstat(QFile::encodeName(source).constData(), &before), 0);

    QString failure;
    const JournalEntry entry = runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->copy({ source }, path(QStringLiteral("work/dst")), id);
        },
        &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    const QString target = path(QStringLiteral("work/dst/data.bin"));
    QCOMPARE(readAll(target), QByteArray(5000, 'z'));
    QCOMPARE(entry.created, QStringList { target });

    struct stat after;
    QCOMPARE(::lstat(QFile::encodeName(target).constData(), &after), 0);
    QCOMPARE(after.st_mode & 07777, before.st_mode & 07777);
    QCOMPARE(after.st_mtim.tv_sec, before.st_mtim.tv_sec);
}

void TestFileOps::copiesDirectoriesRecursively()
{
    write(QStringLiteral("work/src/tree/a.txt"), "a");
    write(QStringLiteral("work/src/tree/nested/b.txt"), "b");
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/dst"))));

    QString failure;
    runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->copy({ path(QStringLiteral("work/src/tree")) },
                      path(QStringLiteral("work/dst")), id);
        },
        &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    QCOMPARE(readAll(path(QStringLiteral("work/dst/tree/a.txt"))), QByteArray("a"));
    QCOMPARE(readAll(path(QStringLiteral("work/dst/tree/nested/b.txt"))), QByteArray("b"));
}

// §8: copy the link, never what it points at. Following would duplicate whole trees and
// silently break relative links.
void TestFileOps::copiesSymlinksAsLinks()
{
    write(QStringLiteral("work/src/real.txt"), "real");
    QVERIFY(::symlink("real.txt",
                      QFile::encodeName(path(QStringLiteral("work/src/link.txt"))).constData())
            == 0);
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/dst"))));

    QString failure;
    runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->copy({ path(QStringLiteral("work/src/link.txt")) },
                      path(QStringLiteral("work/dst")), id);
        },
        &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    const QFileInfo copied(path(QStringLiteral("work/dst/link.txt")));
    QVERIFY(copied.isSymLink());
    QCOMPARE(copied.symLinkTarget(), path(QStringLiteral("work/dst/real.txt")));
}

void TestFileOps::refusesToCopyOntoItself()
{
    write(QStringLiteral("work/self.txt"), "keep me");

    QString failure;
    runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->copy({ path(QStringLiteral("work/self.txt")) }, path(QStringLiteral("work")),
                      id);
        },
        &failure);

    // The original must survive; truncating it to zero is the failure mode being guarded.
    QCOMPARE(readAll(path(QStringLiteral("work/self.txt"))), QByteArray("keep me"));
    QVERIFY(QFileInfo::exists(path(QStringLiteral("work/self (2).txt"))));
}

void TestFileOps::conflictSkipLeavesTargetAlone()
{
    write(QStringLiteral("work/src/f.txt"), "new");
    write(QStringLiteral("work/dst/f.txt"), "old");

    FileOps ops;
    QObject::connect(&ops, &FileOps::conflict, &ops,
                     [&ops](quint64, const QString &, const QString &) {
                         ops.resolveConflict(FileOps::Skip, false);
                     });
    JournalEntry entry;
    QObject::connect(&ops, &FileOps::finished, &ops,
                     [&entry](quint64, const JournalEntry &e) { entry = e; });

    ops.copy({ path(QStringLiteral("work/src/f.txt")) }, path(QStringLiteral("work/dst")), 1);

    QCOMPARE(readAll(path(QStringLiteral("work/dst/f.txt"))), QByteArray("old"));
    QVERIFY(entry.created.isEmpty());
}

void TestFileOps::conflictReplaceOverwrites()
{
    write(QStringLiteral("work/src/f.txt"), "new");
    write(QStringLiteral("work/dst/f.txt"), "old");

    FileOps ops;
    QObject::connect(&ops, &FileOps::conflict, &ops,
                     [&ops](quint64, const QString &, const QString &) {
                         ops.resolveConflict(FileOps::Replace, false);
                     });

    ops.copy({ path(QStringLiteral("work/src/f.txt")) }, path(QStringLiteral("work/dst")), 1);

    QCOMPARE(readAll(path(QStringLiteral("work/dst/f.txt"))), QByteArray("new"));
}

void TestFileOps::conflictRenameUsesSuggestion()
{
    write(QStringLiteral("work/src/f.txt"), "new");
    write(QStringLiteral("work/dst/f.txt"), "old");

    FileOps ops;
    QString suggested;
    QObject::connect(&ops, &FileOps::conflict, &ops,
                     [&](quint64, const QString &, const QString &suggestion) {
                         suggested = suggestion;
                         ops.resolveConflict(FileOps::Rename, false);
                     });

    ops.copy({ path(QStringLiteral("work/src/f.txt")) }, path(QStringLiteral("work/dst")), 1);

    // §8's suggested form.
    QCOMPARE(suggested, QStringLiteral("f (2).txt"));
    QCOMPARE(readAll(path(QStringLiteral("work/dst/f.txt"))), QByteArray("old"));
    QCOMPARE(readAll(path(QStringLiteral("work/dst/f (2).txt"))), QByteArray("new"));
}

// "Apply to all remaining" is the whole reason conflicts block the worker instead of
// being pre-scanned: one answer has to cover every later collision.
void TestFileOps::conflictApplyToAllAsksOnce()
{
    for (const QString &name : { QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                 QStringLiteral("c.txt") }) {
        write(QStringLiteral("work/src/") + name, "new");
        write(QStringLiteral("work/dst/") + name, "old");
    }

    FileOps ops;
    int asked = 0;
    QObject::connect(&ops, &FileOps::conflict, &ops,
                     [&](quint64, const QString &, const QString &) {
                         ++asked;
                         ops.resolveConflict(FileOps::Replace, true);
                     });

    ops.copy({ path(QStringLiteral("work/src/a.txt")), path(QStringLiteral("work/src/b.txt")),
               path(QStringLiteral("work/src/c.txt")) },
             path(QStringLiteral("work/dst")), 1);

    QCOMPARE(asked, 1);
    QCOMPARE(readAll(path(QStringLiteral("work/dst/a.txt"))), QByteArray("new"));
    QCOMPARE(readAll(path(QStringLiteral("work/dst/c.txt"))), QByteArray("new"));
}

// §8: a move within one filesystem is rename(2) — instant, atomic, same inode.
void TestFileOps::sameFilesystemMoveKeepsInode()
{
    write(QStringLiteral("work/src/movable.txt"), "payload");
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/dst"))));

    const ino_t before = inodeOf(path(QStringLiteral("work/src/movable.txt")));
    QVERIFY(before != 0);

    QString failure;
    const JournalEntry entry = runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->move({ path(QStringLiteral("work/src/movable.txt")) },
                      path(QStringLiteral("work/dst")), id);
        },
        &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    QVERIFY(!QFileInfo::exists(path(QStringLiteral("work/src/movable.txt"))));
    QCOMPARE(inodeOf(path(QStringLiteral("work/dst/movable.txt"))), before);
    QCOMPARE(entry.moves.size(), 1);
}

void TestFileOps::renameRejectsInvalidNames()
{
    write(QStringLiteral("work/original.txt"));
    write(QStringLiteral("work/taken.txt"));

    QString failure;
    runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->renameEntry(path(QStringLiteral("work/original.txt")),
                             QStringLiteral("sub/name.txt"), id);
        },
        &failure);
    QVERIFY(!failure.isEmpty());

    // Renaming onto an existing, different file must be refused rather than clobber it.
    runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->renameEntry(path(QStringLiteral("work/original.txt")),
                             QStringLiteral("taken.txt"), id);
        },
        &failure);
    QVERIFY(!failure.isEmpty());
    QVERIFY(QFileInfo::exists(path(QStringLiteral("work/original.txt"))));

    // A valid rename still works.
    const JournalEntry entry = runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->renameEntry(path(QStringLiteral("work/original.txt")),
                             QStringLiteral("renamed.txt"), id);
        },
        &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(QFileInfo::exists(path(QStringLiteral("work/renamed.txt"))));
    QCOMPARE(entry.kind, JournalEntry::Renamed);
}

void TestFileOps::newFolderRefusesToClobber()
{
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/existing"))));

    QString failure;
    runOperation([&](FileOps *ops, quint64 id) {
        ops->makeDirectory(path(QStringLiteral("work")), QStringLiteral("existing"), id);
    }, &failure);
    QVERIFY(!failure.isEmpty());

    const JournalEntry entry = runOperation([&](FileOps *ops, quint64 id) {
        ops->makeDirectory(path(QStringLiteral("work")), QStringLiteral("fresh"), id);
    }, &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(QFileInfo(path(QStringLiteral("work/fresh"))).isDir());
    QCOMPARE(entry.kind, JournalEntry::Created);
}

void TestFileOps::journalIsBounded()
{
    Journal journal;
    for (int i = 0; i < Journal::kCapacity + 15; ++i) {
        JournalEntry entry;
        entry.kind = JournalEntry::Renamed;
        entry.summary = QString::number(i);
        journal.record(entry);
    }

    QCOMPARE(journal.count(), Journal::kCapacity);
    // The most recent survives; the oldest were dropped.
    QCOMPARE(journal.takeLast().summary, QString::number(Journal::kCapacity + 14));

    // Entries that cannot be undone are never recorded at all.
    Journal other;
    other.record(JournalEntry {});
    QVERIFY(!other.canUndo());
}

void TestFileOps::undoRestoresTrashedFiles()
{
    write(QStringLiteral("work/gone.txt"), "content");
    const QString original = path(QStringLiteral("work/gone.txt"));

    QString failure;
    const JournalEntry trashed = runOperation(
        [&](FileOps *ops, quint64 id) { ops->trash({ original }, id); }, &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(!QFileInfo::exists(original));
    QCOMPARE(trashed.kind, JournalEntry::Trashed);

    runOperation([&](FileOps *ops, quint64 id) { ops->undo(trashed, id); }, &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(readAll(original), QByteArray("content"));
}

void TestFileOps::undoReversesAMove()
{
    write(QStringLiteral("work/src/m.txt"), "payload");
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/dst"))));

    QString failure;
    const JournalEntry moved = runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->move({ path(QStringLiteral("work/src/m.txt")) },
                      path(QStringLiteral("work/dst")), id);
        },
        &failure);
    QVERIFY(QFileInfo::exists(path(QStringLiteral("work/dst/m.txt"))));

    runOperation([&](FileOps *ops, quint64 id) { ops->undo(moved, id); }, &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(QFileInfo::exists(path(QStringLiteral("work/src/m.txt"))));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("work/dst/m.txt"))));
}

// §8: undoing a copy trashes the copies. Undo must never be the destructive operation.
void TestFileOps::undoTrashesCopies()
{
    write(QStringLiteral("work/src/c.txt"), "payload");
    QVERIFY(QDir().mkpath(path(QStringLiteral("work/dst"))));

    QString failure;
    const JournalEntry copied = runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->copy({ path(QStringLiteral("work/src/c.txt")) },
                      path(QStringLiteral("work/dst")), id);
        },
        &failure);
    QCOMPARE(copied.kind, JournalEntry::Copied);

    runOperation([&](FileOps *ops, quint64 id) { ops->undo(copied, id); }, &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    // The copy is gone from the destination but recoverable from the trash, and the
    // original is untouched.
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("work/dst/c.txt"))));
    QCOMPARE(readAll(path(QStringLiteral("work/src/c.txt"))), QByteArray("payload"));
}

void TestFileOps::permanentDeleteIsNotUndoable()
{
    write(QStringLiteral("work/doomed.txt"));
    write(QStringLiteral("work/doomedDir/inside.txt"));

    QString failure;
    const JournalEntry entry = runOperation(
        [&](FileOps *ops, quint64 id) {
            ops->removePermanently({ path(QStringLiteral("work/doomed.txt")),
                                     path(QStringLiteral("work/doomedDir")) },
                                   id);
        },
        &failure);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));

    QVERIFY(!QFileInfo::exists(path(QStringLiteral("work/doomed.txt"))));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("work/doomedDir"))));
    // No journal entry, which is exactly what the confirm dialog promises.
    QVERIFY(!entry.isUndoable());
}

// The drag-out payload. QML's encodeURI leaves '#' and '?' alone, so building this in
// JavaScript silently truncated any path containing them once the receiving app parsed
// the list — which is a data-loss bug wearing a formatting bug's clothes.
void TestFileOps::uriListEncodesAwkwardPaths()
{
    const QString list = Operations::uriList({ QStringLiteral("/tmp/a b.txt"),
                                               QStringLiteral("/tmp/hash#tag.txt"),
                                               QStringLiteral("/tmp/query?x=1.txt"),
                                               QStringLiteral("/tmp/100%.txt") });

    const QStringList lines = list.split(QStringLiteral("\r\n"));
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines.at(0), QStringLiteral("file:///tmp/a%20b.txt"));
    QCOMPARE(lines.at(1), QStringLiteral("file:///tmp/hash%23tag.txt"));
    QCOMPARE(lines.at(2), QStringLiteral("file:///tmp/query%3Fx=1.txt"));
    QCOMPARE(lines.at(3), QStringLiteral("file:///tmp/100%25.txt"));

    // RFC 2483 says CRLF, and GTK's parser is strict about it.
    QVERIFY(list.contains(QStringLiteral("\r\n")));
}
