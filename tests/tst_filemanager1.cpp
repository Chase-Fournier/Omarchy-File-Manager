#include "tst_filemanager1.h"

#include "filemanager1.h"

#include <QTest>

// The caller percent-encodes; omafile has to hand a real path to --select. Getting this
// wrong is the same bug the drag-out uri-list had, where '#' and '?' survived unescaped
// and the receiving side silently truncated the path.
void TestFileManager1::decodesFileUris()
{
    QCOMPARE(FileManager1::localPaths({ QStringLiteral("file:///home/chase/notes.txt") }),
             QStringList { QStringLiteral("/home/chase/notes.txt") });

    // A space, a '#', a '?' and a '%' — each of which ends a path early if it is read as
    // anything but an encoded byte.
    QCOMPARE(FileManager1::localPaths(
                 { QStringLiteral("file:///home/chase/a%20b%23c%3Fd%25e.txt") }),
             QStringList { QStringLiteral("/home/chase/a b#c?d%e.txt") });

    // Non-ASCII, which arrives UTF-8 percent-encoded.
    QCOMPARE(FileManager1::localPaths({ QStringLiteral("file:///home/chase/r%C3%A9sum%C3%A9") }),
             QStringList { QString::fromUtf8("/home/chase/résumé") });

    // Several at once: applications pass a selection, not always one file.
    QCOMPARE(FileManager1::localPaths({ QStringLiteral("file:///tmp/one"),
                                        QStringLiteral("file:///tmp/two") }),
             QStringList({ QStringLiteral("/tmp/one"), QStringLiteral("/tmp/two") }));

    // Some callers hand over a bare path rather than a URI.
    QCOMPARE(FileManager1::localPaths({ QStringLiteral("/tmp/plain") }),
             QStringList { QStringLiteral("/tmp/plain") });
}

// Anything omafile cannot reach as a path is dropped rather than turned into a window
// showing nowhere.
void TestFileManager1::ignoresWhatItCannotShow()
{
    QVERIFY(FileManager1::localPaths({ QStringLiteral("https://example.com/a.txt") }).isEmpty());
    QVERIFY(FileManager1::localPaths({ QStringLiteral("trash:///old.txt") }).isEmpty());
    QVERIFY(FileManager1::localPaths({ QString() }).isEmpty());
    QVERIFY(FileManager1::localPaths({}).isEmpty());

    // A mixed list keeps the half it can use rather than refusing the call.
    QCOMPARE(FileManager1::localPaths({ QStringLiteral("https://example.com/a"),
                                        QStringLiteral("file:///tmp/real") }),
             QStringList { QStringLiteral("/tmp/real") });
}
