#include "tubemusic.h"

#include <doob/imagefetch.h>
#include <doob/settingsregistry.h>
#include <doob/sourcemanager.h>

#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// The audio format preference: an m4a (AAC) container when available (the
// best match for the built-in player's backends), any other best audio
// otherwise.
constexpr const char *kAudioFormat = "bestaudio[ext=m4a]/bestaudio/best";

// The video's thumbnail from a -J read: the "thumbnail" field (yt-dlp's
// pick), falling down the "thumbnails" list (quality order) and then the
// always-present 0.jpg (YouTube's default thumbnail).
QString thumbnailFromJson(const QJsonObject &o, const QString &videoId)
{
    QString direct = o.value("thumbnail").toString();
    if (!direct.isEmpty())
        return direct;
    for (const auto &value : o.value("thumbnails").toArray()) {
        const QString thumb = value.toObject().value("url").toString();
        if (!thumb.isEmpty())
            return thumb;
    }
    if (!videoId.isEmpty())
        return QStringLiteral("https://i.ytimg.com/vi/%1/0.jpg")
            .arg(videoId);
    return {};
}

// The plugin's settings (the SettingsManager, the keys prefixed with the
// plugin's id — a convention, the SettingsManager does not enforce it). The
// binary's path is the runner's (kYtdlpKey, ytdlp.h).
constexpr const char *kModeKey = "tubemusic.playback_mode";
constexpr const char *kCacheCapKey = "tubemusic.cache_max_mb";
constexpr const char *kBrowserKey = "tubemusic.cookies_browser";
}

TubeMusicProvider::TubeMusicProvider(QObject *parent)
    : SourceProvider(parent)
{
}

// Claims the URL shapes Tube uses for a track/video. The host is the
// decider (www./m./music. subdomains alike); the path must name a video
// (watch?v=..., youtu.be/..., /shorts/...) — channel and search pages
// are not importable and are left to fall through.
bool TubeMusicProvider::accepts(const QUrl &url) const
{
    const QString host = url.host().toLower();
    if (host == QLatin1String("youtu.be"))
        return !url.path().isEmpty();
    if (host.endsWith(QLatin1String("youtube.com")))
        return url.path().startsWith(QLatin1String("/watch"))
            || url.path().startsWith(QLatin1String("/shorts/"))
            || url.path().startsWith(QLatin1String("/playlist"));
    return false;
}

// The stored form of a claimed link: only what identifies the video.
// The incidental arguments (list=, start_radio=, si=, t=, ...) are
// dropped, so a re-pasted link with extras hits the same item.
QUrl TubeMusicProvider::canonicalUrl(const QUrl &url) const
{
    QUrl out(url);
    if (url.host() == QLatin1String("youtu.be")
            || url.path().startsWith(QLatin1String("/shorts/"))) {
        // The video id is in the path: drop the query.
        out.setQuery(QString());
        return out;
    }
    QUrlQuery query;
    query.addQueryItem("v", QUrlQuery(url).queryItemValue("v"));
    out.setQuery(query);
    return out;
}

// The list a pasted link references (its list= argument): the list's
// title and its videos, in list order. The flat-playlist extraction
// yields one entry per video without a per-video metadata round-trip
// (a list can be long); --yes-playlist makes a watch URL resolve to
// the list it references rather than to the video.
void TubeMusicProvider::fetchList(const QUrl &url, const ListCallback &cb)
{
    if (QUrlQuery(url).queryItemValue("list").isEmpty()) {
        cb(ListEntries());
        return;
    }
    runWithCookieFallback({ QStringLiteral("-J"), QStringLiteral("--flat-playlist"),
                            QStringLiteral("--yes-playlist"), url.toString(), },
             [cb](const QString &out) {
        if (out.isEmpty()) {
            cb(ListEntries());
            return;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            cb(ListEntries());
            return;
        }
        const QJsonObject o = doc.object();
        const QJsonArray array = o.value("entries").toArray();
        ListEntries entries;
        entries.title = o.value("title").toString();
        for (const auto &value : array) {
            const QJsonObject item = value.toObject();
            const QString id = item.value("id").toString();
            if (id.isEmpty())
                continue; // an unavailable entry: no video to import
            // The flat listing carries the display metadata (title, the
            // channel, the duration) without a per-video round-trip: the
            // import wizard's review shows it as-is.
            ListVideo video(QUrl(
                QStringLiteral("https://www.youtube.com/watch?v=") + id));
            video.title = item.value("title").toString();
            video.artist = item.value("uploader").toString();
            const int durationSecs = item.value("duration").toInt();
            video.durationMs =
                durationSecs > 0 ? static_cast<qint64>(durationSecs) * 1000 : 0;
            // The flat listing usually carries no image; a listing that
            // does (a newer yt-dlp) previews it in the wizard.
            video.thumbnail = item.value("thumbnail").toString();
            entries.videos.append(video);
        }
        cb(entries);
    });
}

void TubeMusicProvider::setDataPath(const QString &path)
{
    m_dataPath = path;
}

QString TubeMusicProvider::cookiesPath() const
{
    // In the plugins directory (next to the plugin's data directory):
    // <data dir>/plugins/cookies.txt.
    return QFileInfo(m_dataPath).path() + QStringLiteral("/cookies.txt");
}

QStringList TubeMusicProvider::cookieArgs() const
{
    // A cookie-less client gets throttled by YouTube under import
    // volume: pass the snapshot when there is one.
    const QString path = cookiesPath();
    if (QFileInfo(path).size() > 0)
        return { QStringLiteral("--cookies"), path };
    return {};
}

void TubeMusicProvider::fetchCookies(const QString &browser,
                                     const std::function<void(bool)> &done)
{
    // The export happens before yt-dlp validates the (missing) URL, so
    // the exit code is irrelevant — the file is the result. A browser
    // without a cookie database leaves the file untouched, so success
    // requires a fresh (re)write.
    m_cookiesBroken = false;  // a fresh snapshot is worth trying
    const QString path = cookiesPath();
    QDir().mkpath(QFileInfo(path).path());
    const QDateTime before = QFileInfo(path).lastModified();
    runYtdlp({ QStringLiteral("--cookies-from-browser"), browser,
               QStringLiteral("--cookies"), path, },
             [path, before, done](const QString &) {
        const QFileInfo info(path);
        const bool written =
            info.exists() && info.size() > 0 && info.lastModified() > before;
        if (written)
            filterCookiesFile(path);
        done(written);
    });
}

void TubeMusicProvider::filterCookiesFile(const QString &path)
{
    // The browser export carries every site's session; keep only what
    // the provider can use (google/youtube domains).
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QStringList kept;
    for (const QString &line : content.split('\n')) {
        // Netscape format: comment lines stay, the domain is the first
        // field of a cookie line.
        if (line.isEmpty() || line.startsWith('#')
            || line.section('\t', 0, 0).contains(QStringLiteral("google"),
                                                  Qt::CaseInsensitive)
            || line.section('\t', 0, 0).contains(QStringLiteral("youtube"),
                                                  Qt::CaseInsensitive)
            || line.section('\t', 0, 0).contains(QStringLiteral("ggpht"),
                                                  Qt::CaseInsensitive))
            kept << line;
    }
    QFile out(path);
    if (out.open(QIODevice::WriteOnly))
        out.write(kept.join('\n').toUtf8());
}

void TubeMusicProvider::runYtdlp(const QStringList &args,
                                 const std::function<void(const QString &)> &done,
                                 bool withCookies, qint64 timeoutMs)
{
    m_runner.run((withCookies ? cookieArgs() : QStringList()) + args, done,
                 timeoutMs);
}

void TubeMusicProvider::runWithCookieFallback(const QStringList &args,
                                              const std::function<void(const QString &)> &done,
                                              qint64 timeoutMs)
{
    if (m_cookiesBroken || cookieArgs().isEmpty()) {
        runYtdlp(args, done, false, timeoutMs);
        return;
    }
    runYtdlp(args, [this, args, done, timeoutMs](const QString &out) {
        if (!out.isEmpty()) {
            done(out);
            return;
        }
        // A failed with-cookies run: the session is likely rotated
        // (invalid cookies 403 where cookie-less would work). Retry
        // without cookies and remember until the next fetch.
        m_cookiesBroken = true;
        qWarning("tubemusic: cookies appear stale — falling back to a "
                 "cookie-less run");
        runYtdlp(args, done, false, timeoutMs);
    }, true, timeoutMs);
}

void TubeMusicProvider::fetchMetadata(const QUrl &url, MetadataCallback cb)
{
    runWithCookieFallback({ QStringLiteral("-J"), QStringLiteral("--no-playlist"),
                            url.toString(), },
             [cb, url](const QString &out) {
        if (out.isEmpty()) {
            cb(TrackMeta());
            return;
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            cb(TrackMeta());
            return;
        }
        const QJsonObject o = doc.object();
        TrackMeta meta;
        meta.title = o.value("title").toString();
        // A video has no artist tag: the channel stands in for it.
        const QString artist = o.value("artist").toString();
        meta.artist = artist.isEmpty() ? o.value("uploader").toString() : artist;
        meta.album = o.value("album").toString();
        meta.trackNumber = o.value("track").toInt();
        meta.year = o.value("year").toInt();
        meta.durationMs =
            static_cast<qint64>(o.value("duration").toDouble() * 1000.0);
        // The art: the thumbnail, downloaded on the shared image path
        // (an invalid url answers empty at once; the read's meta lands
        // with the art, empty art on a failed download — the callback
        // still lands exactly once, on the GUI thread).
        const QString thumb = thumbnailFromJson(o, videoId(url));
        ImageFetch::getInstance()->fetch(
            QUrl(thumb), [meta, cb](const QByteArray &data,
                                    const QString &mime) mutable {
            if (!data.isEmpty()) {
                meta.artData = data;
                meta.artMime = mime;
            }
            cb(meta);
        });
    });
}

QString TubeMusicProvider::cachePath() const
{
    return m_dataPath + QStringLiteral("/cache");
}

QString TubeMusicProvider::videoId(const QUrl &url)
{
    QString v = QUrlQuery(url).queryItemValue(QStringLiteral("v"));
    if (!v.isEmpty())
        return v;
    // youtu.be/<id>, /shorts/<id> — the last path segment.
    const QStringList segments = url.path().split('/', Qt::SkipEmptyParts);
    return segments.isEmpty() ? QString() : segments.last();
}

QString TubeMusicProvider::findCachedFile(const QString &id) const
{
    const QDir dir(cachePath());
    for (const QFileInfo &info :
         dir.entryInfoList({ id + QStringLiteral(".*") }, QDir::Files)) {
        // In-progress downloads are .part files: not a hit.
        if (info.fileName().endsWith(QStringLiteral(".part"))
            || info.fileName().endsWith(QStringLiteral(".ytdl")))
            continue;
        if (info.size() > 0)
            return info.absoluteFilePath();
    }
    return {};
}

void TubeMusicProvider::evictCache() const
{
    const qint64 capMb =
        SettingsManager::getInstance()->value(kCacheCapKey).toLongLong();
    if (capMb <= 0)
        return;
    const qint64 capBytes = capMb * 1024 * 1024;
    QDir dir(cachePath());
    QFileInfoList files = dir.entryInfoList(QDir::Files);
    qint64 total = 0;
    QMutableListIterator<QFileInfo> it(files);
    while (it.hasNext()) {
        QFileInfo &info = it.next();
        if (info.fileName().endsWith(QStringLiteral(".part"))
            || info.fileName().endsWith(QStringLiteral(".ytdl"))) {
            it.remove();  // an in-progress download: not cache
            continue;
        }
        total += info.size();
    }
    // The least-recently-modified files go first (a file's mtime is
    // when it was downloaded/refreshed).
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &a, const QFileInfo &b) {
                  return a.lastModified() < b.lastModified();
              });
    for (const QFileInfo &info : files) {
        if (total <= capBytes)
            break;
        if (QFile::remove(info.absoluteFilePath()))
            total -= info.size();
    }
}

void TubeMusicProvider::resolvePlayback(const MediaItem &item,
                                        const PlaybackCallback &cb)
{
    // Two modes (the "tubemusic.playback_mode" setting, default download
    // first):
    //  - "stream": resolve a fresh signed stream URL per play (WITHOUT
    //    cookies — a session-bound URL is a 403 for the player's bare
    //    fetch). Instant start, but the playback dies with the network
    //    connection.
    //  - "download": cache a local copy — the download goes through
    //    yt-dlp's retried downloader, and the playback is then
    //    network-independent; a cached track plays instantly (and
    //    offline).
    if (SettingsManager::getInstance()->value(kModeKey).toString()
        == QStringLiteral("stream")) {
        runYtdlp({ QStringLiteral("-f"), QString::fromLatin1(kAudioFormat),
                   QStringLiteral("--get-url"), QStringLiteral("--no-playlist"),
                   item.source.toString(), },
                 [cb, item](const QString &out) {
                     const QUrl stream = out.isEmpty() ? QUrl() : QUrl(out);
                     qDebug("tubemusic: resolved %s -> %s",
                            qPrintable(item.source.toString()),
                            stream.isValid() ? qPrintable(stream.toString())
                                             : "(no stream)");
                     cb(stream);
                 },
                 /*withCookies=*/false);
        return;
    }

    const QString id = videoId(item.source);
    if (id.isEmpty()) {
        qDebug("tubemusic: no video id in %s", qPrintable(item.source.toString()));
        cb(QUrl());
        return;
    }
    const QString cached = findCachedFile(id);
    if (!cached.isEmpty()) {
        evictCache();
        qDebug("tubemusic: cache hit %s -> %s", qPrintable(id), qPrintable(cached));
        cb(QUrl::fromLocalFile(cached));
        return;
    }
    // A download of this id is already in flight (a gapless pre-roll
    // resolution and the plain play of the same item, a double play
    // press): join it — one download per id, every caller's callback
    // lands on the completion.
    if (m_downloads.contains(id)) {
        m_downloads[id].append(cb);
        return;
    }
    QDir().mkpath(cachePath());
    qDebug("tubemusic: downloading %s", qPrintable(id));
    // yt-dlp writes a .part file and renames it on completion: a
    // failed/interrupted download leaves no finished file behind. A
    // stale-cookies failure resumes the same .part cookie-less.
    m_downloads.insert(id, { cb });
    runWithCookieFallback({ QStringLiteral("-f"), QString::fromLatin1(kAudioFormat),
               QStringLiteral("--no-playlist"), QStringLiteral("-o"),
               cachePath() + '/' + id + QStringLiteral(".%(ext)s"),
               item.source.toString(), },
             [this, id](const QString &) {
                 // Every caller of this download, then forget it.
                 const QList<PlaybackCallback> callbacks = m_downloads.take(id);
                 const QString file = findCachedFile(id);
                 if (file.isEmpty()) {
                     qDebug("tubemusic: download failed for %s", qPrintable(id));
                     for (const PlaybackCallback &pending : callbacks)
                         pending(QUrl());
                     return;
                 }
                 evictCache();
                 qDebug("tubemusic: downloaded %s -> %s", qPrintable(id),
                        qPrintable(file));
                 for (const PlaybackCallback &pending : callbacks)
                     pending(QUrl::fromLocalFile(file));
             },
             /*timeoutMs=*/static_cast<qint64>(10) * 60 * 1000);
}

TubeMusicSettingsPage::TubeMusicSettingsPage(TubeMusicProvider *provider,
                                              QWidget *parent)
    : QWidget(parent)
    , m_provider(provider)
{
    SettingsManager* settings = SettingsManager::getInstance();

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* label = new QLabel("yt-dlp binary", this);
    layout->addWidget(label);

    m_binary = new QLineEdit(this);
    m_binary->setObjectName("ytdlpBinary");
    m_binary->setText(settings->value(kYtdlpKey).toString());
    layout->addWidget(m_binary);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    QPushButton* save = new QPushButton("Save", this);
    save->setObjectName("saveBinaryButton");
    connect(save, &QPushButton::clicked, this, &TubeMusicSettingsPage::onSaveClicked);
    buttonLayout->addWidget(save);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);

    // Playback: stream the signed URL directly (instant start, the
    // playback dies with the network connection) or download the audio
    // first (a few seconds to start, then network-independent — cached
    // tracks play instantly and offline).
    layout->addWidget(new QLabel("Playback", this));
    m_mode = new QComboBox(this);
    m_mode->setObjectName("playbackMode");
    m_mode->addItems({ "Download first (cache)", "Stream direct" });
    m_mode->setCurrentIndex(
        settings->value(kModeKey).toString() == QStringLiteral("stream") ? 1 : 0);
    layout->addWidget(m_mode);

    QHBoxLayout* capLayout = new QHBoxLayout();
    capLayout->addWidget(new QLabel("Max cache size (MB)", this));
    m_cacheMaxMb = new QSpinBox(this);
    m_cacheMaxMb->setObjectName("cacheMaxMb");
    m_cacheMaxMb->setRange(64, 8192);
    m_cacheMaxMb->setSingleStep(64);
    m_cacheMaxMb->setValue(settings->value(kCacheCapKey).toInt());
    capLayout->addWidget(m_cacheMaxMb);
    capLayout->addStretch(1);
    layout->addLayout(capLayout);

    m_cacheStatus = new QLabel(this);
    m_cacheStatus->setObjectName("cacheStatus");
    m_cacheStatus->setWordWrap(true);
    layout->addWidget(m_cacheStatus);
    updateCacheStatus();

    // The YouTube cookies: a cookie-less client gets throttled (and
    // eventually banned) by YouTube under import volume, so the user
    // exports the cookies of a browser they are logged in with.
    layout->addWidget(new QLabel("YouTube cookies", this));

    QHBoxLayout* cookiesLayout = new QHBoxLayout();
    m_browser = new QComboBox(this);
    m_browser->setObjectName("cookiesBrowser");
    m_browser->addItems({ "chrome", "chromium", "edge", "brave", "firefox",
                          "opera", "vivaldi", "safari", "whale", });
    const int browserIndex = m_browser->findText(settings->value(kBrowserKey).toString());
    m_browser->setCurrentIndex(browserIndex >= 0 ? browserIndex : 0);
    QPushButton* fetch = new QPushButton("Fetch cookies", this);
    fetch->setObjectName("fetchCookiesButton");
    connect(fetch, &QPushButton::clicked, this, &TubeMusicSettingsPage::onFetchCookies);
    QPushButton* open = new QPushButton("Open YouTube", this);
    open->setObjectName("openYouTubeButton");
    connect(open, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl("https://music.youtube.com"));
    });
    cookiesLayout->addWidget(m_browser);
    cookiesLayout->addWidget(fetch);
    cookiesLayout->addWidget(open);
    layout->addLayout(cookiesLayout);

    m_cookiesStatus = new QLabel(this);
    m_cookiesStatus->setObjectName("cookiesStatus");
    m_cookiesStatus->setWordWrap(true);
    layout->addWidget(m_cookiesStatus);
    updateCookiesStatus();
}

void TubeMusicSettingsPage::onSaveClicked()
{
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue(kYtdlpKey, m_binary->text());
    settings->setValue(kModeKey,
                     m_mode->currentIndex() == 1
                         ? QStringLiteral("stream")
                         : QStringLiteral("download"));
    settings->setValue(kCacheCapKey, m_cacheMaxMb->value());
    settings->setValue(kBrowserKey, m_browser->currentText());
    updateCacheStatus();
}

void TubeMusicSettingsPage::updateCacheStatus()
{
    QDir dir(m_provider->cachePath());
    qint64 total = 0;
    int count = 0;
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files)) {
        // In-progress downloads are not cache yet.
        if (info.fileName().endsWith(QStringLiteral(".part"))
            || info.fileName().endsWith(QStringLiteral(".ytdl")))
            continue;
        total += info.size();
        ++count;
    }
    m_cacheStatus->setText(QStringLiteral("Cache: %1 MB used (%2 files)")
                               .arg(total / (static_cast<qint64>(1024) * 1024))
                               .arg(count));
}

void TubeMusicSettingsPage::onFetchCookies()
{
    const QString browser = m_browser->currentText();
    m_cookiesStatus->setText("Fetching cookies from " + browser + "…");
    m_provider->fetchCookies(browser, [this, browser](bool ok) {
        if (!ok)
            m_cookiesStatus->setText(
                "Could not read the " + browser + " cookies — is the browser "
                "installed and logged in to YouTube?");
        else
            updateCookiesStatus("Cookies fetched from " + browser);
    });
}

void TubeMusicSettingsPage::updateCookiesStatus(const QString &note)
{
    const QString path = m_provider->cookiesPath();
    const QFileInfo info(path);
    if (!info.exists() || info.size() == 0) {
        m_cookiesStatus->setText(note.isEmpty()
                                     ? "No cookies file: yt-dlp runs cookie-less."
                                     : note);
        return;
    }
    int count = 0;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine());
            if (!line.isEmpty() && !line.startsWith('#'))
                ++count;
        }
    QString text = QStringLiteral("%1 cookies (last fetched %2)")
                       .arg(count)
                       .arg(info.lastModified().toString("yyyy-MM-dd HH:mm"));
    if (m_provider->cookiesBroken())
        text += " — stale, runs are cookie-less (re-fetch)";
    if (!note.isEmpty())
        text = note + " — " + text;
    m_cookiesStatus->setText(text);
}

TubeMusicPlugin::TubeMusicPlugin(QObject *parent)
    : DoobPlugin(parent)
{
}

void TubeMusicPlugin::initialize(DoobApi *api)
{
    // An api this plugin does not understand: stay a no-op (shutdown()
    // guards the resulting null api).
    if (api->version != DoobApi::ApiVersion)
        return;

    m_api = api;
    m_provider = new TubeMusicProvider(this);
    m_provider->setDataPath(api->pluginDataPath(QStringLiteral("tubemusic")));
    api->sources->registerProvider(m_provider);

    // The plugin's settings (the central settings.json; the keys are
    // prefixed with the plugin's id — a convention the SettingsManager does not
    // enforce). The stored values survive the unregistration in
    // shutdown(), so a re-added plugin finds them intact.
    api->settingsManager->registerSetting(
        { QString::fromLatin1(kYtdlpKey), QStringLiteral("yt-dlp binary"),
          QStringLiteral("The path to the yt-dlp binary "
                         "(\"yt-dlp\" = on the PATH)."),
          QStringLiteral("yt-dlp"), m_provider->id(), });
    api->settingsManager->registerSetting(
        { QString::fromLatin1(kModeKey), QStringLiteral("Playback mode"),
          QStringLiteral("\"download\" caches a local copy first (the "
                         "playback is network-independent); \"stream\" "
                         "resolves a signed URL per play."),
          QStringLiteral("download"), m_provider->id(), });
    api->settingsManager->registerSetting(
        { QString::fromLatin1(kCacheCapKey), QStringLiteral("Max cache size (MB)"),
          QStringLiteral("The download cache is evicted (oldest files "
                         "first) when it grows past this cap."),
          512, m_provider->id(), });
    api->settingsManager->registerSetting(
        { QString::fromLatin1(kBrowserKey), QStringLiteral("Cookies browser"),
          QStringLiteral("The browser the YouTube cookies are exported "
                         "from."),
          QStringLiteral("chrome"), m_provider->id(), });

    // The plugin's settings topic (the preferences dialog's "Plugins"
    // group): the page is created on demand with the provider (for the
    // cookie export).
    api->settings->registerTopic(m_provider->id(), m_provider->title(),
                                  m_provider->id(),
                                  [provider = m_provider] {
                                      return new TubeMusicSettingsPage(provider);
                                  });
}

void TubeMusicPlugin::shutdown()
{
    if (m_api == nullptr)
        return;
    m_api->sources->unregisterProvider(m_provider);
    m_api->settingsManager->unregisterSetting(QString::fromLatin1(kYtdlpKey));
    m_api->settingsManager->unregisterSetting(QString::fromLatin1(kModeKey));
    m_api->settingsManager->unregisterSetting(QString::fromLatin1(kCacheCapKey));
    m_api->settingsManager->unregisterSetting(QString::fromLatin1(kBrowserKey));
    m_api->settings->unregisterTopic(m_provider->id());
    m_provider = nullptr;
    m_api = nullptr;
}
