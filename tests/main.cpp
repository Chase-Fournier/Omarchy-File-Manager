#include "tst_bulkrename.h"
#include "tst_directorymodel.h"
#include "tst_filemanager1.h"
#include "tst_fileops.h"
#include "tst_formatting.h"
#include "tst_fuzzyscorer.h"
#include "tst_location.h"
#include "tst_places.h"
#include "tst_polish.h"
#include "tst_remote.h"
#include "tst_search.h"
#include "tst_settings.h"
#include "tst_terminal.h"
#include "tst_theme.h"

#include <QCoreApplication>
#include <QMetaMethod>
#include <QTest>

namespace {

// Test-function names given on the command line, with any :datatag stripped.
QStringList requestedFunctions(int argc, char *argv[])
{
    QStringList wanted;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument.startsWith(QLatin1Char('-')))
            continue;
        wanted << argument.section(QLatin1Char(':'), 0, 0);
    }
    return wanted;
}

// qExec fails a suite outright when a function filter names a slot it does not have.
// With several suites in one binary, running one test would otherwise report a failure
// for every suite that does not contain it.
bool suiteHasAny(const QObject *suite, const QStringList &wanted)
{
    if (wanted.isEmpty())
        return true;

    const QMetaObject *meta = suite->metaObject();
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        if (wanted.contains(QString::fromLatin1(meta->method(i).name())))
            return true;
    }
    return false;
}

} // namespace

// One binary, several suites. QTEST_MAIN allows only one test object per executable and
// §14 calls for a lot more of them, so the suites are registered here instead.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList wanted = requestedFunctions(argc, argv);

    int failures = 0;
    int ran = 0;
    const auto run = [&](QObject *suite) {
        if (suiteHasAny(suite, wanted)) {
            failures += QTest::qExec(suite, argc, argv);
            ++ran;
        }
        delete suite;
    };

    run(new TestFormatting);
    run(new TestFuzzyScorer);
    run(new TestBulkRename);
    run(new TestLocation);
    run(new TestSettings);
    run(new TestTerminal);
    run(new TestTheme);
    run(new TestDirectoryModel);
    run(new TestFileManager1);
    run(new TestFileOps);
    run(new TestSearch);
    run(new TestRemote);
    run(new TestPlaces);
    run(new TestPolish);

    if (ran == 0) {
        qWarning("no suite contains %s", qPrintable(wanted.join(QLatin1String(", "))));
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
