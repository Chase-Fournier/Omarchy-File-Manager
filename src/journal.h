#pragma once

#include "trash.h"

#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>

// One completed operation, in the form needed to reverse it (§8):
//   rename -> rename back        move  -> move back
//   trash  -> restore from info  copy  -> trash the copies
// Permanent delete produces no entry at all, which is why the confirm dialog says so.
struct JournalEntry
{
    enum Kind { None, Trashed, Moved, Renamed, Copied, Created };

    Kind kind = None;

    // What to say in the status bar: "Moved 3 files to Documents".
    QString summary;

    // Trashed: everything needed to put each file back.
    QList<Trash::Item> trashed;

    // Moved / Renamed: original -> final, reversed by swapping the pair.
    QList<QPair<QString, QString>> moves;

    // Copied / Created: paths brought into existence, undone by trashing them.
    QStringList created;

    bool isUndoable() const { return kind != None; }
};

Q_DECLARE_METATYPE(JournalEntry)

// A bounded history of completed operations. Bounded because an unbounded one is a
// memory leak that also holds trash entries alive long after anyone would undo them.
class Journal
{
public:
    static constexpr int kCapacity = 20;

    void record(const JournalEntry &entry);
    bool canUndo() const { return !m_entries.isEmpty(); }
    JournalEntry takeLast();
    void clear() { m_entries.clear(); }
    int count() const { return int(m_entries.size()); }

private:
    QList<JournalEntry> m_entries;
};
