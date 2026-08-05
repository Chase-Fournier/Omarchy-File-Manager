#pragma once

#include <QByteArray>
#include <QObject>
#include <QTemporaryDir>

// §11's thumbnail cache and §1's "open with" handler lookup. Both are interoperability
// surfaces: get the naming or the parsing wrong and omafile silently stops sharing a
// cache — or an application list — with the rest of the desktop.
class TestPolish : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void thumbnailPathFollowsTheSpec();
    void thumbnailRoundTrips();
    void staleThumbnailIsRejected();
    void untaggedThumbnailIsRejected();
    void missingThumbnailIsNull();
    void scalingNeverEnlarges();
    void thumbnailIsPrivate();

    void parsesMimeAssociations();
    void ignoresOtherSections();
    void readsLocalisedNames();

private:
    QTemporaryDir m_home;
    QByteArray m_realCache;
};
