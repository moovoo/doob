#ifndef PLAYLISTFILE_H
#define PLAYLISTFILE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

// An external playlist file (M3U/M3U8, PLS): the name a new playlist built
// from it should carry, and its entries in file order.
struct PlaylistFile
{
    QString name;
    QList<QUrl> entries;
};

// Parses the external playlist file at path into out. Returns false
// (leaving out unchanged) when the file cannot be read or has no entries.
// M3U/M3U8: every line that is not blank and not a '#' comment is an entry
// (#EXTINF and friends carry no URL). PLS: the File1..FileN keys in
// ascending numeric order; the Title key (when present) is the name.
// Relative entries resolve against the file's directory; scheme-bearing
// values (http, file, ...) are kept as pasted. The default name is the
// file's base name without extension.
bool parsePlaylistFile(const QString &path, PlaylistFile &out);

// Writes entries as an M3U8 playlist file (a #EXTM3U header, one line per
// entry: local entries as absolute paths, everything else as its URL
// string). Returns false on I/O failure.
bool writePlaylistFile(const QString &path, const QList<QUrl> &entries);

// The external playlist file extensions (no dots, lowercase).
QStringList playlistFileExtensions();
// True when url is a local file with one of those extensions.
bool isPlaylistFile(const QUrl &url);

// Flattens a file drop into import sources: every external playlist file
// is expanded into its entries (in file order, an unparseable file
// contributes nothing), everything else passes through — the result keeps
// the drop order.
QList<QUrl> flattenDropFiles(const QList<QUrl> &urls);

// A file chooser's local paths (the "Add files..." results) as import
// sources: the same flattening a drop of the same files gets — the add
// paths mirror the drop paths.
QList<QUrl> flattenLocalFiles(const QStringList &paths);

// How resolveSourceArgument resolves a value that is neither a pasted URL
// nor an existing file.
enum class SourceFallback
{
    // The user-input resolution (QUrl::fromUserInput): host-like forms
    // become http:// URLs. Pasted sources — command-line arguments and
    // import-wizard lines.
    UserInput,
    // A local file path, even when the file is missing: a playlist entry
    // that names a missing file stays a broken file reference, not a
    // guessed URL.
    LocalFile,
};

// Resolves a source argument (a command-line source, an import-wizard
// line, a playlist entry) to a QUrl: a pasted URL (one carrying "://") is
// kept as pasted, an existing path (absolute, or relative to baseDir) is a
// file: URL, and anything else is resolved per fallback.
QUrl resolveSourceArgument(const QString &baseDir, const QString &argument,
                           SourceFallback fallback = SourceFallback::UserInput);

#endif // PLAYLISTFILE_H
