#include "tst_terminal.h"

#include "terminal.h"

#include <QTest>

void TestTerminal::initTestCase()
{
    m_realEditor = qgetenv("EDITOR");
    m_realVisual = qgetenv("VISUAL");
}

void TestTerminal::cleanupTestCase()
{
    if (m_realEditor.isEmpty())
        qunsetenv("EDITOR");
    else
        qputenv("EDITOR", m_realEditor);

    if (m_realVisual.isEmpty())
        qunsetenv("VISUAL");
    else
        qputenv("VISUAL", m_realVisual);
}

// $TERMINAL on Omarchy is xdg-terminal-exec, which takes the command directly. It was
// being handed a -e it does not understand, and answered by opening nothing at all.
void TestTerminal::specLaunchersTakeTheCommandDirectly()
{
    const QStringList command { QStringLiteral("sh"), QStringLiteral("-c"),
                                QStringLiteral("echo hi") };

    const QStringList xdg = Terminal::argsFor(QStringLiteral("xdg-terminal-exec"), command);
    QVERIFY2(!xdg.contains(QStringLiteral("-e")), qPrintable(xdg.join(QLatin1Char(' '))));
    // `--` so a command beginning with a dash is not mistaken for an option.
    QCOMPARE(xdg, QStringList({ QStringLiteral("--") }) + command);

    QCOMPARE(Terminal::argsFor(QStringLiteral("omarchy-launch-terminal"), command), command);

    // The decision is on the program name, not the path it was found at.
    QCOMPARE(Terminal::argsFor(QStringLiteral("/usr/bin/xdg-terminal-exec"), command),
             QStringList({ QStringLiteral("--") }) + command);
}

void TestTerminal::emulatorsWantDashE()
{
    const QStringList command { QStringLiteral("nvim"), QStringLiteral("+12"),
                                QStringLiteral("/tmp/notes.txt") };
    for (const QString &emulator : { QStringLiteral("alacritty"), QStringLiteral("ghostty"),
                                     QStringLiteral("kitty"), QStringLiteral("foot"),
                                     QStringLiteral("xterm") }) {
        QCOMPARE(Terminal::argsFor(emulator, command),
                 QStringList({ QStringLiteral("-e") }) + command);
    }
}

// The list names the terminal editors, not the graphical ones, so that an editor nobody
// anticipated is handed to the desktop rather than gambled on a terminal — the failure
// that produced a window flashing open and shut.
void TestTerminal::graphicalEditorsAreNotTerminalEditors()
{
    for (const QString &editor : { QStringLiteral("vim"), QStringLiteral("nvim"),
                                   QStringLiteral("nano"), QStringLiteral("hx"),
                                   QStringLiteral("emacs") })
        QVERIFY2(Terminal::editorIsTerminal(editor), qPrintable(editor));

    for (const QString &editor : { QStringLiteral("code"), QStringLiteral("subl"),
                                   QStringLiteral("zed"), QStringLiteral("gedit"),
                                   QStringLiteral("kate") })
        QVERIFY2(!Terminal::editorIsTerminal(editor), qPrintable(editor));

    // Unknown editors are treated as graphical: opening in the desktop's own handler is
    // merely unhelpful, where a terminal that closes instantly loses the file entirely.
    QVERIFY(!Terminal::editorIsTerminal(QStringLiteral("some-editor-from-2032")));
}

// Bulk rename waits for the editor to exit. A graphical editor that cannot be told to
// wait would report "finished" before anything had been typed.
void TestTerminal::graphicalEditorsThatCannotWaitAreKnown()
{
    QCOMPARE(Terminal::editorWaitArgs(QStringLiteral("code")),
             QStringList({ QStringLiteral("--wait") }));
    QCOMPARE(Terminal::editorWaitArgs(QStringLiteral("subl")),
             QStringList({ QStringLiteral("--wait") }));

    // No answer is a refusal, which the caller turns into a message rather than a no-op.
    QVERIFY(Terminal::editorWaitArgs(QStringLiteral("gedit")).isEmpty());
    QVERIFY(Terminal::editorWaitArgs(QStringLiteral("some-editor-from-2032")).isEmpty());
}

void TestTerminal::editorCommandKeepsItsArguments()
{
    qunsetenv("VISUAL");
    qputenv("EDITOR", "nvim -u NONE");
    QCOMPARE(Terminal::editorCommand(),
             QStringList({ QStringLiteral("nvim"), QStringLiteral("-u"),
                           QStringLiteral("NONE") }));

    // A quoted path with a space stays one argument.
    qputenv("EDITOR", "\"/opt/my editor/bin/ed\" --fast");
    QCOMPARE(Terminal::editorCommand().first(), QStringLiteral("/opt/my editor/bin/ed"));

    qunsetenv("EDITOR");
    QVERIFY(Terminal::editorCommand().isEmpty());
}

void TestTerminal::visualBeatsEditor()
{
    qputenv("EDITOR", "vi");
    qputenv("VISUAL", "nvim");
    QCOMPARE(Terminal::editorCommand().first(), QStringLiteral("nvim"));
}
