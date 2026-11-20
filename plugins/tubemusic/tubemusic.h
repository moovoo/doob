#ifndef TUBEMUSIC_H
#define TUBEMUSIC_H

#include <QHash>
#include <QWidget>

#include <doob/doobapi.h>
#include <doob/doobplugin.h>
#include <doob/settingsmanager.h>
#include <doob/sourceprovider.h>

#include <functional>

#include "ytdlp.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

// A source provider that claims Tube (Music) URLs.
//
// Metadata, lists and playback go through the yt-dlp binary (the
// YtdlpRunner: an async process pool, one run per callback):
// fetchMetadata maps `yt-dlp -J` onto a TrackMeta, fetchList expands a
// list reference (`yt-dlp -J --flat-playlist`), and resolvePlayback
// resolves a fresh stream URL per play (`yt-dlp --get-url`) — Tube's
// signed URLs expire after hours, so the resolution happens at play
// time, never at import. A pasted link's incidental arguments (list=,
// start_radio=, si=, ...) are stripped from the stored source
// (canonicalUrl keeps only the video id). A cookie-less client gets
// throttled by YouTube under import volume, so the user can export
// their browser's YouTube cookies to the plugin's cookies file
// (fetchCookies, the settings page's "Fetch cookies"); every run passes
// the file to yt-dlp when it is non-empty.
class TubeMusicProvider : public SourceProvider
{
public:
    explicit TubeMusicProvider(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("tubemusic"); }
    QString title() const override { return QStringLiteral("Tube Music"); }
    bool accepts(const QUrl &url) const override;
    QUrl canonicalUrl(const QUrl &url) const override;
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override;
    void fetchList(const QUrl &url, const ListCallback &cb) override;
    void resolvePlayback(const MediaItem &item,
                         const PlaybackCallback &cb) override;

    // The plugin's data directory (plugins/tubemusic, from
    // DoobApi::pluginDataPath).
    void setDataPath(const QString &path);
    // The plugin's cookies file (plugins/cookies.txt, in the plugins
    // directory) — the browser cookie snapshot the metadata/list runs
    // pass to yt-dlp. The playback resolution deliberately runs WITHOUT
    // it: a session-bound resolution yields a stream URL that only the
    // same client context can fetch (a bare player gets 403), while a
    // cookie-less one is self-contained.
    QString cookiesPath() const;

    // Exports the browser's cookies into the cookies file (yt-dlp
    // `--cookies-from-browser`), keeping only the google/youtube
    // domains (the raw export would copy every other site's session
    // into the data dir). The user must be logged in to YouTube in
    // that browser. done lands on the GUI thread exactly once (true
    // when the file was written).
    void fetchCookies(const QString &browser,
                      const std::function<void(bool)> &done);

    // The playback cache directory (plugins/tubemusic/cache/): the
    // audio files of the "download first" mode.
    QString cachePath() const;

    // True when the last with-cookies run failed (YouTube rotates
    // sessions — invalid cookies 403 where cookie-less would work):
    // the runs go cookie-less until fetchCookies runs again.
    bool cookiesBroken() const { return m_cookiesBroken; }

private:
    QString binaryPath() const;
    // The `--cookies` argument when the cookies file is non-empty (no
    // argument cookie-less).
    QStringList cookieArgs() const;
    // Keeps only the google/youtube cookie lines in the export.
    static void filterCookiesFile(const QString &path);
    // The video id of a (canonical) source — the cache file name stem.
    static QString videoId(const QUrl &url);
    // The finished cache file for an id (empty when absent; in-progress
    // .part downloads do not count).
    QString findCachedFile(const QString &id) const;
    // Keeps the cache at or below the configured size cap (the
    // least-recently-modified files go first).
    void evictCache() const;
    // Runs the binary with args (the cookie args prepended when
    // withCookies — off for the stream-mode playback, see cookiesPath)
    // asynchronously; done lands on the GUI thread with the stdout
    // (empty on any failure), exactly once. timeoutMs is the per-run
    // kill timeout (downloads get a generous one).
    void runYtdlp(const QStringList &args,
                  const std::function<void(const QString &)> &done,
                  bool withCookies = true,
                  qint64 timeoutMs = 30000);
    // A run that may use the cookies file, with the stale-cookie
    // fallback: a failed with-cookies run is retried cookie-less and
    // the breakage is remembered (fetchCookies resets it).
    void runWithCookieFallback(const QStringList &args,
                               const std::function<void(const QString &)> &done,
                               qint64 timeoutMs = 30000);

    QString m_dataPath;
    // Set by a failed with-cookies run (a rotated session), cleared by
    // fetchCookies: while set, the runs skip the cookies file.
    bool m_cookiesBroken = false;
    // The in-flight downloads by video id (the "download first" mode):
    // a second resolvePlayback of the same id while one runs joins it
    // (one download per id — a gapless pre-roll resolution and the
    // plain play of the same item must not race on the same .part
    // file); every caller's callback lands on the completion.
    QHash<QString, QList<PlaybackCallback>> m_downloads;
    // The process pool (the binary runs go through it).
    YtdlpRunner m_runner{this};
};

// The "Tube Music" settings topic page: the yt-dlp binary path and the
// YouTube cookies (the browser they are fetched from, a "Fetch cookies"
// button, "Open YouTube", and a status line) and the playback mode
// (stream direct / download-first cache, the cache size cap, and the
// current cache usage). Saving writes the plugin's settings (the
// SettingsManager); the provider picks the values up on its next run
// (no restart needed).
class TubeMusicSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit TubeMusicSettingsPage(TubeMusicProvider *provider,
                                   QWidget *parent = nullptr);

private slots:
    void onSaveClicked();
    void onFetchCookies();

private:
    void updateCookiesStatus(const QString &note = QString());
    void updateCacheStatus();

    TubeMusicProvider* m_provider;
    QLineEdit* m_binary = nullptr;
    QComboBox* m_mode = nullptr;
    QSpinBox* m_cacheMaxMb = nullptr;
    QLabel* m_cacheStatus = nullptr;
    QComboBox* m_browser = nullptr;
    QLabel* m_cookiesStatus = nullptr;
};

// The plugin: registers the provider and its settings topic in
// initialize(), unregisters them in shutdown() (the host unloads this
// library right after, which deletes the provider — the registrations
// must be undone first).
class TubeMusicPlugin : public DoobPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID DOOB_PLUGIN_IID FILE "tubemusic.json")
public:
    explicit TubeMusicPlugin(QObject *parent = nullptr);

    void initialize(DoobApi *api) override;
    void shutdown() override;

private:
    DoobApi *m_api = nullptr;
    TubeMusicProvider *m_provider = nullptr;
};

#endif // TUBEMUSIC_H
