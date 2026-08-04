#pragma once

#include <QObject>

class TestLocation : public QObject
{
    Q_OBJECT

private slots:
    void parsesLocalPaths();
    void expandsTilde();
    void resolvesRelativeAgainstBase();
    void normalizesDotSegments();
    void parsesRemoteUris();
    void parsesRcloneRemotes();
    void roundTripsEveryScheme_data();
    void roundTripsEveryScheme();
    void handlesSpacesAndEscapes();
    void segmentsForBreadcrumb();
    void parentAndChild();
    void rootHasNoParent();
};
