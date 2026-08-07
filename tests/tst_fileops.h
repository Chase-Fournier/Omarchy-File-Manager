#pragma once

#include <QObject>
#include <QTemporaryDir>

class FileOps;
struct JournalEntry;

// Trash, FileOps and Journal together: they are only meaningful in combination, and the
// operations here are the destructive ones, so they get the most direct coverage.
//
// Every test runs under a fake $HOME and $XDG_DATA_HOME. That is not tidiness — without
// it these tests would move files into the developer's real trash.
class TestFileOps : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void trashMovesFileAndWritesInfo();
    void trashPercentEncodesTheOriginalPath();
    void trashDisambiguatesCollidingNames();
    void trashRestoresToItsOriginalPath();

    void copiesFileContentsModeAndMtime();
    void copiesDirectoriesRecursively();
    void copiesSymlinksAsLinks();
    void refusesToCopyOntoItself();
    void refusesToCopyIntoItsOwnSubdirectory();

    void conflictSkipLeavesTargetAlone();
    void conflictReplaceOverwrites();
    void conflictRenameUsesSuggestion();
    void conflictApplyToAllAsksOnce();

    void sameFilesystemMoveKeepsInode();
    void renameRejectsInvalidNames();
    void newFolderRefusesToClobber();
    void newFileNeverTruncates();

    void compressingLeavesTheOriginalsAlone();
    void compressedArchiveRoundTrips();
    void compressingTwiceDoesNotClobber();
    void failedCompressionLeavesNothingBehind();

    void extractsIntoAFolderOfItsOwn();
    void extractionFolderDropsTheWholeSuffix();
    void extractingTwiceDoesNotMerge();
    void extractionRefusesToEscapeItsFolder();
    void aFileLargerThanOneChunkCopiesWhole();
    void copyReportsARateAndItemWorkDoesNot();
    void rateSmoothingSeedsSettlesAndRefusesNonsense();

    void describesWhatAnEntryIs();
    void executableBitFollowsTheReadBits();
    void createsSymlinksThatPointAtTheOriginal();

    void journalIsBounded();
    void undoRestoresTrashedFiles();
    void undoReversesAMove();
    void undoTrashesCopies();
    void permanentDeleteIsNotUndoable();
    void uriListEncodesAwkwardPaths();

private:
    QString path(const QString &relative) const;
    void write(const QString &relative, const QByteArray &contents = "x");
    // Runs a FileOps slot to completion, returning the journal entry it produced.
    JournalEntry runOperation(const std::function<void(FileOps *, quint64)> &call,
                              QString *failure = nullptr);

    QTemporaryDir m_root;
    QByteArray m_realHome;
    QByteArray m_realDataHome;
};
