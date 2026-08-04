#include "tst_formatting.h"

#include "formatting.h"

#include <QTest>

void TestFormatting::humanSize_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<QString>("expected");

    QTest::newRow("not stat'd") << qint64(-1) << QString();
    QTest::newRow("zero") << qint64(0) << QStringLiteral("0 B");
    QTest::newRow("bytes") << qint64(842) << QStringLiteral("842 B");
    QTest::newRow("just under a kilobyte") << qint64(1023) << QStringLiteral("1023 B");
    QTest::newRow("exactly a kilobyte") << qint64(1024) << QStringLiteral("1.0 KB");
    QTest::newRow("plan's example") << qint64(3174) << QStringLiteral("3.1 KB");
    // Past 10 the decimal stops carrying information and is dropped.
    QTest::newRow("two digit kilobytes") << qint64(51 * 1024) << QStringLiteral("51 KB");
    QTest::newRow("megabytes") << qint64(1024 * 1024 * 3 / 2) << QStringLiteral("1.5 MB");
    QTest::newRow("gigabytes") << qint64(1024) * 1024 * 1024 * 4 << QStringLiteral("4.0 GB");
    QTest::newRow("terabytes") << qint64(1024) * 1024 * 1024 * 1024 * 2
                               << QStringLiteral("2.0 TB");
}

void TestFormatting::humanSize()
{
    QFETCH(qint64, bytes);
    QFETCH(QString, expected);
    QCOMPARE(Formatting::humanSize(bytes), expected);
}

void TestFormatting::relativeTime_data()
{
    QTest::addColumn<qint64>("age");
    QTest::addColumn<QString>("expected");

    QTest::newRow("seconds") << qint64(5) << QStringLiteral("now");
    QTest::newRow("just under a minute") << qint64(59) << QStringLiteral("now");
    QTest::newRow("minutes") << qint64(5 * 60) << QStringLiteral("5m");
    QTest::newRow("hours") << qint64(3 * 3600) << QStringLiteral("3h");
    QTest::newRow("plan's example") << qint64(2 * 86400) << QStringLiteral("2d");
    QTest::newRow("weeks") << qint64(21 * 86400) << QStringLiteral("3w");
    // A clock that runs backwards must not render "-4d".
    QTest::newRow("future") << qint64(-3600) << QStringLiteral("now");
}

void TestFormatting::relativeTime()
{
    QFETCH(qint64, age);
    QFETCH(QString, expected);

    const qint64 now = 1770000000; // fixed, so the suite never depends on the clock
    QCOMPARE(Formatting::relativeTime(now - age, now), expected);
}
