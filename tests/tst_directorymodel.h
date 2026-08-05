#pragma once

#include <QObject>
#include <QTemporaryDir>

class DirectoryModel;

// Covers the Lister and Watcher too: they only exist to feed this model, and testing
// them through it is what proves the threading and cancellation actually work.
class TestDirectoryModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listsDirectoriesFirstThenNames();
    void hiddenFilesAreOptIn();
    void filterNarrowsAndReportsMatchPositions();
    void statsArriveOnlyForRequestedRows();
    void arrivingStatsDoNotMoveTheCursor();
    void handlesAwkwardFilenames();
    void reportsBrokenSymlinks();
    void reportsPermissionDenied();
    void breadcrumbSegmentsNameTheirDirectories();
    void navigationSelectsTheDirectoryJustLeft();
    void remembersWhereTheCursorWasInEachDirectory();
    void cursorMemoryYieldsToAnExplicitSelection();
    void watcherAppliesDiffWithoutResetting();
    void sortingBySizeAndTime();
    void listing10kIsWithinBudget();
    void filteringAKeystrokeIsWithinBudget();

private:
    // Spins the event loop until the model finishes whatever it started.
    bool waitForIdle(DirectoryModel *model, int timeoutMs = 10000);
    void write(const QString &relative, const QByteArray &contents = QByteArray());

    QTemporaryDir m_dir;
};
