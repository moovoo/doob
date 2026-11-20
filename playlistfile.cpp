#include "playlistfile.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

QUrl resolveSourceArgument(const QString &baseDir, const QString &argument,
                           SourceFallback fallback)
{
    // A pasted URL (one carrying "://") is kept as pasted (a provider's,
    // a stream's, a file:).
    if (argument.contains(QLatin1String("://")))
        return {argument};
    // A path, resolved against baseDir.
    const QString path = QDir(baseDir).absoluteFilePath(argument);
    QUrl file = QUrl::fromLocalFile(path);
    if (fallback == SourceFallback::LocalFile)
        return file;
    // A pasted source: an existing file is a file: URL; anything else is
    // the user-input resolution (the host-like forms, which assume http).
    if (QFile::exists(path))
        return file;
    return QUrl::fromUserInput(argument);
}

namespace {

// One entry of a playlist file: a pasted URL, or a path resolved against
// the playlist file's directory (a missing file stays a broken reference,
// not a guessed URL).
QUrl resolveEntry(const QString &baseDir, const QString &line)
{
    return resolveSourceArgument(baseDir, line.trimmed(),
                                 SourceFallback::LocalFile);
}

bool parseM3U(const QString &path, const QString &baseDir, PlaylistFile &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        out.entries.append(resolveEntry(baseDir, line));
    }
    return !out.entries.isEmpty();
}

bool parsePLS(const QString &path, const QString &baseDir, PlaylistFile &out)
{
    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError)
        return false;
    // The PLS spec keeps every key under the [playlist] section; QSettings
    // namespaces by section, so enter it (a file without the section simply
    // has no entries and fails below).
    settings.beginGroup(QStringLiteral("playlist"));
    const QString title = settings.value(QStringLiteral("Title")).toString();
    if (!title.isEmpty())
        out.name = title;

    // File1..FileN in ascending numeric order (not lexicographic: File2
    // comes before File10).
    QList<int> numbers;
    for (const QString &key : settings.allKeys()) {
        if (!key.startsWith(QLatin1String("File"), Qt::CaseInsensitive))
            continue;
        bool ok = false;
        const int number = key.mid(4).toInt(&ok);
        if (ok && number > 0)
            numbers.append(number);
    }
    std::sort(numbers.begin(), numbers.end());
    for (int number : numbers) {
        const QString value = settings.value(
            QStringLiteral("File%1").arg(number)).toString();
        if (!value.isEmpty())
            out.entries.append(resolveEntry(baseDir, value));
    }
    return !out.entries.isEmpty();
}

} // namespace

bool parsePlaylistFile(const QString &path, PlaylistFile &out)
{
    PlaylistFile result;
    result.name = QFileInfo(path).completeBaseName();
    const QString baseDir = QFileInfo(path).absolutePath();
    const QString ext = QFileInfo(path).suffix().toLower();

    bool ok = false;
    if (ext == QLatin1String("m3u") || ext == QLatin1String("m3u8"))
        ok = parseM3U(path, baseDir, result);
    else if (ext == QLatin1String("pls"))
        ok = parsePLS(path, baseDir, result);
    if (!ok)
        return false;

    out = std::move(result);
    return true;
}

bool writePlaylistFile(const QString &path, const QList<QUrl> &entries)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out << "#EXTM3U\n";
    for (const QUrl &entry : entries) {
        if (entry.isLocalFile())
            out << entry.toLocalFile() << '\n';
        else
            out << entry.toString() << '\n';
    }
    out.flush();
    return static_cast<bool>(out);
}

QStringList playlistFileExtensions()
{
    return { "m3u", "m3u8", "pls" };
}

bool isPlaylistFile(const QUrl &url)
{
    if (!url.isLocalFile())
        return false;
    return playlistFileExtensions().contains(
        QFileInfo(url.toLocalFile()).suffix().toLower());
}

QList<QUrl> flattenDropFiles(const QList<QUrl> &urls)
{
    QList<QUrl> sources;
    for (const QUrl &url : urls) {
        if (!isPlaylistFile(url)) {
            sources.append(url);
            continue;
        }
        PlaylistFile file;
        if (parsePlaylistFile(url.toLocalFile(), file))
            sources.append(file.entries);
    }
    return sources;
}

QList<QUrl> flattenLocalFiles(const QStringList &paths)
{
    QList<QUrl> urls;
    for (const QString &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    return flattenDropFiles(urls);
}
