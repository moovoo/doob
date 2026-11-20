#ifndef TAGGEDFILE_H
#define TAGGEDFILE_H

#include <QMap>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUrl>

// Generates a real 1-second FLAC file with the given tags (native vorbis
// comment keys, e.g. TITLE, ALBUMARTIST, TRACKNUMBER) using the ffmpeg
// binary (FLAC uses its native encoder, so no external codec is required).
// Returns an invalid QUrl if ffmpeg is unavailable or the encode fails, so
// callers can QSKIP rather than fail. The file is overwritten if it exists.
inline QUrl makeTaggedFileWith(const QString &dirPath, const QString &name,
                               const QMap<QString, QString> &tags)
{
    const QString path = dirPath + "/" + name;
    QStringList args = {
        "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "sine=frequency=440:duration=1",
    };
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it)
        args << "-metadata" << it.key() + "=" + it.value();
    args << path;

    QProcess proc;
    proc.start("ffmpeg", args);
    if (!proc.waitForFinished(15000) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0)
        return {};
    return QUrl::fromLocalFile(path);
}

inline QUrl makeTaggedFile(const QString &dirPath, const QString &name,
                           const QString &title, const QString &artist,
                           const QString &album)
{
    return makeTaggedFileWith(dirPath, name,
                              { { "TITLE", title },
                                { "ARTIST", artist },
                                { "ALBUM", album } });
}

#endif // TAGGEDFILE_H
