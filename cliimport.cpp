#include "cliimport.h"

#include <QDir>

#include <memory>

#include "playlist.h"
#include "playlistfile.h"
#include "playbackmanager.h"
#include "playlistmanager.h"

namespace {

// The one-shot connection: the lambda runs at most once (it disconnects
// the connection that holds it). The connection's storage is heap-owned
// (a shared_ptr the lambda captures): the lambda can fire long after the
// connecting frame has returned, where a stack-captured
// QMetaObject::Connection would dangle.
template <typename Sender, typename Signal, typename Context, typename Lambda>
void connectOnce(Sender *sender, Signal signal, Context *context, Lambda lambda)
{
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(sender, signal, context,
                          [connection, lambda = std::move(lambda)](auto &&...args) mutable {
        QObject::disconnect(*connection);
        lambda(std::forward<decltype(args)>(args)...);
      });
}

} // namespace

CliImport::CliImport(QObject *parent)
    : QObject(parent)
{
}

QUrl CliImport::sourceFromArgument(const QString &argument)
{
    return resolveSourceArgument(QDir::currentPath(), argument);
}

CliImport::Split CliImport::splitSources(const QList<QUrl> &urls)
{
    Split split;
    for (const QUrl &url : urls)
        (isPlaylistFile(url) ? split.playlistFiles : split.media).append(url);
    return split;
}

void CliImport::import(const CliRequest &request)
{
    PlaylistManager *pl = PlaylistManager::getInstance();

    if (m_running) {
        qWarning("CliImport::import: an import is already running");
        return;
    }

    QList<QUrl> urls;
    for (const QString &argument : request.sources) {
        const QUrl url = sourceFromArgument(argument);
        if (!url.isEmpty() && url.isValid())
            urls.append(url);
    }
    const Split split = splitSources(urls);

    // The media's target, created now (the rows land later: the import
    // is queued behind the running ones).
    quint64 mediaTarget = 0;
    if (!split.media.isEmpty())
        mediaTarget = request.newPlaylist
            ? pl->addPlaylist(request.newPlaylistName)
            : pl->currentPlaylistId();

    // The jobs, in the request's order: each playlist file is a job of
    // its own, the media a single block at the first media source's
    // place.
    QList<Job> jobs;
    bool mediaJobAdded = false;
    for (const QUrl &url : urls) {
        if (isPlaylistFile(url)) {
            Job job;
            job.isFile = true;
            job.file = url.toLocalFile();
            jobs.append(job);
        } else if (!mediaJobAdded && mediaTarget != 0) {
            Job job;
            job.playlistId = mediaTarget;
            job.sources = split.media;
            jobs.append(job);
            mediaJobAdded = true;
        }
    }
    if (jobs.isEmpty())
        return;

    if (request.play)
        connectPlay(pl, mediaTarget, request.newPlaylist);

    m_running = true;
    startJob(jobs, 0);
}

void CliImport::connectPlay(PlaylistManager *pl, quint64 mediaTarget,
                            bool newPlaylist)
{
    if (mediaTarget != 0) {
        const int base = newPlaylist ? 0 : pl->count();
        playFirstRow(mediaTarget, base);
        return;
    }
    // Playlist files only: the first file job runs synchronously in
    // startJob and creates its playlist — the playlistsChanged its
    // creation emits (synchronously) hands over the id, before the
    // entries' import starts, so the row watch connects in time. The
    // m_running guard drops it if the chain has already ended without a
    // creation (an unparseable first file).
    connectOnce(pl, &PlaylistManager::playlistsChanged, this, [this, pl] {
        if (!m_running)
            return;
        const Playlist last = pl->playlists().last();
        if (last.isValid())
            playFirstRow(last.id, 0);
    });
}

void CliImport::startJob(const QList<Job> &jobs, int index)
{
    PlaylistManager *pl = PlaylistManager::getInstance();
    if (index >= jobs.size()) {
        m_running = false;
        return;
    }
    const Job &job = jobs.at(index);
    if (job.isFile)
        pl->importPlaylistFile(job.file);
    else
        pl->importSources(job.playlistId, job.sources);
    // A job that started no import (an unparseable file, a playlist that
    // cannot be created, no usable source) — or that finished it
    // synchronously (every source already known to the store, no reads
    // in flight) — has no importFinished to wait for: advance at once.
    if (!pl->importing()) {
        startJob(jobs, index + 1);
        return;
    }
    // One-shot: a synchronous finish emits importFinished re-entrantly
    // (inside the previous job's emission), and a stale connection would
    // re-run this job forever.
    connectOnce(pl, &PlaylistManager::importFinished, this,
                [this, jobs, index](quint64) {
        startJob(jobs, index + 1);
    });
}

void CliImport::playFirstRow(quint64 playlistId, int row)
{
    PlaylistManager *pl = PlaylistManager::getInstance();
    // One-shot: the rows land in source order, so the count passing row
    // is the first source's row.
    connectOnce(pl, &PlaylistManager::playlistChanged, this,
                [this, pl, playlistId, row](quint64 id) {
        if (id != playlistId)
            return;
        if (static_cast<int>(pl->playlist(playlistId).items.size()) > row)
            PlaybackManager::getInstance()->playFromPlaylist(playlistId, row);
    });
}
