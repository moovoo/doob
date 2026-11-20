#ifndef CLIIMPORT_H
#define CLIIMPORT_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class PlaylistManager;

// What a doob invocation carries — its own command line, or the command
// line a second instance forwarded (AppInstance): the sources as entered
// (paths, playlist files, URLs) and the two behaviour flags.
struct CliRequest
{
    QStringList sources;
    // Start playing the first imported track (the default leaves the
    // playback alone — an append must not interrupt what is playing).
    bool play = false;
    // The media sources go into a new playlist (the current one by
    // default, like a drop on the current playlist's view).
    bool newPlaylist = false;
    // The new playlist's name ("" = the default "Playlist").
    QString newPlaylistName;

    bool operator==(const CliRequest &other) const
    {
        return sources == other.sources
            && play == other.play
            && newPlaylist == other.newPlaylist
            && newPlaylistName == other.newPlaylistName;
    }
};

// Routes the sources of an invocation (its own command line, or the
// forwarded one) through the same import paths the UI's adds use:
//
//   * playlist files (a local file with an m3u/m3u8/pls extension) each
//     become their own playlist (PlaylistManager::importPlaylistFile —
//     named after the file, marked imported, its entries imported);
//   * the rest (files, URLs) go into the current playlist, or into a
//     fresh one when the request asks for it (PlaylistManager::
//     importSources — async, write-through, the tags read on the worker).
//
// The manager's imports are serialized (one at a time), so the jobs run
// in the request's order, each chained on the previous one's
// importFinished (a job that starts no import — an unparseable file, no
// usable source — or finishes it synchronously — every source already
// known to the store — advances at once). A second import() while one is
// running is refused (qWarning), like the manager's.
//
// When the request plays, the first imported track starts the playback
// as soon as its row lands (the rows arrive in source order, so the
// first row is the first source): with media sources that is the first
// media row of the media's target, with only playlist files the first
// entry of the first imported file.
//
// The routing needs the loaded singletons and the plugins (a source's
// provider may claim it): main calls it for its own command line and
// connects it to AppInstance::payloadReceived for the forwarded ones —
// both after the window is shown.
class CliImport : public QObject
{
    Q_OBJECT
public:
    explicit CliImport(QObject *parent = nullptr);

    // The source of one argument: a scheme-bearing argument is a URL as
    // pasted (a provider's, a stream's, a file:), an existing path
    // (absolute, or relative to the current directory) is a file, and
    // anything else goes through QUrl::fromUserInput (the host-like
    // forms, which assume http).
    static QUrl sourceFromArgument(const QString &argument);

    // Splits the urls in order: the playlist files (a local file with a
    // playlist extension) and the media (everything else).
    struct Split
    {
        QList<QUrl> media;
        QList<QUrl> playlistFiles;
    };
    static Split splitSources(const QList<QUrl> &urls);

    void import(const CliRequest &request);

private:
    // One step of the job chain: a playlist file (importPlaylistFile) or
    // the media's block (importSources into its target).
    struct Job
    {
        bool isFile = false;
        QString file;          // isFile: the playlist file
        quint64 playlistId = 0;  // !isFile: the media's target
        QList<QUrl> sources;    // !isFile: the media's block
    };
    // The play target, decided before the imports start (the rows can
    // land synchronously for sources that need no read): the media's
    // target and its first media row, else the first playlist file's
    // first entry.
    void connectPlay(PlaylistManager *pl, quint64 mediaTarget, bool newPlaylist);
    // Starts the job at index and chains the rest on its
    // importFinished; a job that started no import (or finished it
    // synchronously) advances at once.
    void startJob(const QList<Job> &jobs, int index);
    // Starts the playback on the playlist's row once the row has landed
    // (a one-shot connection on playlistChanged).
    void playFirstRow(quint64 playlistId, int row);

    bool m_running = false;
};

#endif // CLIIMPORT_H
