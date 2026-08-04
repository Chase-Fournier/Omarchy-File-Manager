#pragma once

#include <QObject>
#include <QTemporaryDir>

// Tiers 2 and 3 of §6, driven headlessly. SearchEngine blocks its own thread by design,
// so these call it directly and let it block the test thread instead.
class TestSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void findsFilesByName();
    void ranksBasenameMatchesFirst();
    void survivesNewlinesInFilenames();
    void emptyQueryReturnsTheWholeTree();
    void staleGenerationIsDropped();
    void warmCacheAnswersWithoutWalking();
    void reportsMissingTool();

    void findsFileContents();
    void contentSearchReportsLineAndPreview();

    void firstResultIsWithinBudget();
    void walkingALargeTreeIsWithinBudget();

private:
    void write(const QString &relative, const QByteArray &contents = "x");

    QTemporaryDir m_dir;
    bool m_haveFd = false;
    bool m_haveRg = false;
};
