#ifndef NAMES_H
#define NAMES_H

#include <QDir>
#include <QString>

// The shared name rules of the named stores (the layouts' and the
// styles' directories): a name is the file's stem, so it must be a
// safe file name — and the fork of a stale preset current must not
// collide with an existing file.

namespace names
{
// A store name from user input: trimmed, leading dots stripped
// (hidden files out of the store). Path separators are refused
// outright ("" — a name that tried to escape the store is an input
// error, not something to guess at). Empty when nothing usable is
// left.
inline QString sanitize(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()
        || trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))
        || trimmed.contains(QLatin1Char(':')))
        return {};
    QString out = trimmed;
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    return out.trimmed();
}

// `base` + `ext` in `dir`, with a numeric suffix on a collision
// (base-2.ext, base-3.ext, ...): the fork target name of the save of
// a stale layout preset current (a "<name>.modified" already taken
// gets a suffix).
inline QString uniqueFile(const QDir &dir, const QString &base,
                          const QString &ext)
{
    QString candidate = base + ext;
    int n = 2;
    while (dir.exists(candidate))
        candidate = base + QStringLiteral("-%1").arg(n++) + ext;
    return candidate;
}
}

#endif // NAMES_H
