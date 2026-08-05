#pragma once

#include <QObject>

// §14 names cycle detection in bulk rename as a thing that must be tested. A bulk rename
// that gets its ordering wrong does not fail loudly — it silently eats a file.
class TestBulkRename : public QObject
{
    Q_OBJECT

private slots:
    void renamesTheChangedLinesOnly();
    void abortsWhenTheLineCountChanged();
    void rejectsEmptyNames();
    void rejectsSlashes();
    void rejectsDuplicateTargets();

    void ordersSoNothingIsClobbered();
    void breaksATwoWaySwap();
    void breaksAThreeWayRotation();
    void handlesAChainThatIsNotACycle();

    void splitsEditorBuffers();
    void executesCorrectlyOnDisk_data();
    void executesCorrectlyOnDisk();
};
