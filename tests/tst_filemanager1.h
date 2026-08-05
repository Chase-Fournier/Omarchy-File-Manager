#pragma once

#include <QObject>

// org.freedesktop.FileManager1, the interface behind "Reveal in File Explorer" and "Show
// in folder". What is testable without a bus is the URI handling, which is the part that
// has gone wrong before: §14's awkward filenames arrive here percent-encoded.
class TestFileManager1 : public QObject
{
    Q_OBJECT

private slots:
    void decodesFileUris();
    void ignoresWhatItCannotShow();
};
