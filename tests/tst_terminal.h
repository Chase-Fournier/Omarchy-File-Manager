#pragma once

#include <QByteArray>
#include <QObject>

// How omafile launches a terminal and an editor. Both halves of this shipped wrong and
// the symptom was identical either way — a window that appears and vanishes — so the
// rules are pinned here rather than left to be rediscovered from a flashing terminal.
class TestTerminal : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void specLaunchersTakeTheCommandDirectly();
    void emulatorsWantDashE();
    void graphicalEditorsAreNotTerminalEditors();
    void graphicalEditorsThatCannotWaitAreKnown();
    void editorCommandKeepsItsArguments();
    void visualBeatsEditor();

private:
    QByteArray m_realEditor;
    QByteArray m_realVisual;
};
