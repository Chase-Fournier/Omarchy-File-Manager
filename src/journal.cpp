#include "journal.h"

void Journal::record(const JournalEntry &entry)
{
    if (!entry.isUndoable())
        return;

    m_entries.append(entry);
    while (m_entries.size() > kCapacity)
        m_entries.removeFirst();
}

JournalEntry Journal::takeLast()
{
    if (m_entries.isEmpty())
        return {};
    return m_entries.takeLast();
}
