#pragma once

#include <QByteArray>
#include <QObject>
#include <QTemporaryDir>

// Pinning, which is the sidebar's only persisted state — everything else it shows is
// re-derived from the system on each rebuild. A pin outlives the process, so the file it
// is kept in is a contract with the next run.
class TestPlaces : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void pinsSurviveARestart();
    void pinnedFilesAreNotDrawnAsFolders();
    void unpinningRemovesOnlyThatOne();
    void pinningTwiceIsANoOp();
    void trashSitsUnderThePins();
    void trashIsListedBeforeAnythingIsTrashed();

private:
    QString pinsFile() const;

    QTemporaryDir m_root;
    QByteArray m_realHome;
    QByteArray m_realConfigHome;
    QByteArray m_realRuntimeDir;
};
