#ifndef JSONFILE_H
#define JSONFILE_H

#include <QByteArray>
#include <QString>

// The atomic write of the central JSON files (settings.json, the
// layouts' files): the data goes to "<path>.tmp" in
// the same directory and is
// renamed over path (the existing file removed first — QFile::rename does
// not replace one). A crash mid-write cannot tear the previous file; on a
// failure the target is left untouched and false is returned (with a
// warning naming who). The bytes are the already-serialized document —
// each caller keeps its own format (settings are indented, the layout is
// compact), so a rewrite never churns the file's style.
bool atomicWrite(const QString &path, const QByteArray &data, const char *who);

#endif // JSONFILE_H
