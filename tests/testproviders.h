#ifndef DOOB_TESTS_TESTPROVIDERS_H
#define DOOB_TESTS_TESTPROVIDERS_H

#include <QObject>
#include <QPair>
#include <QStringList>
#include <utility>

#include "audioplayer.h"
#include "mediaitem.h"
#include "sourceprovider.h"
#include "trackmetadata.h"

// The shared test source providers (the per-test copies of these used to
// drift from each other). None declares Q_OBJECT: the doubles add no
// signals, slots or properties of their own, so the base metaobject
// suffices (and a shared header cannot provide the moc to the per-test
// translation units).

// A fully configurable provider: claims the URLs it is told to, answers
// fetchMetadata with the configured meta and (optionally) resolves the
// playback to the configured stream URL and/or player.
class FakeProvider : public SourceProvider
{
public:
    FakeProvider(QString id, QString title, QObject *parent = nullptr)
        : SourceProvider(parent), m_id(std::move(id)), m_title(std::move(title))
    {
    }

    QString id() const override { return m_id; }
    QString title() const override { return m_title; }

    bool accepts(const QUrl &url) const override
    {
        return m_accepts.contains(url.toString());
    }

    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        m_fetched.append(url.toString());
        cb(m_meta);
    }

    QString fetchFailureHint() const override { return m_hint; }

    QUrl playableUrl(const MediaItem &item) const override
    {
        Q_UNUSED(item);
        return m_playable;
    }

    AudioPlayer* player() const override { return m_player; }

    void setAccepts(const QStringList &urls) { m_accepts = urls; }
    void setMeta(const TrackMeta &meta) { m_meta = meta; }
    void setPlayableUrl(const QUrl &url) { m_playable = url; }
    void setPlayer(AudioPlayer *player) { m_player = player; }
    void setHint(const QString &hint) { m_hint = hint; }

    QStringList m_fetched;
    QString m_id;
    QString m_title;
    QStringList m_accepts;
    TrackMeta m_meta;
    QUrl m_playable;
    AudioPlayer* m_player = nullptr;
    QString m_hint;
};

// A provider that claims an explicit URL list and defers its metadata
// answers until deliver() (the shape of a slow async fetch).
class MetaProvider : public SourceProvider
{
public:
    explicit MetaProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("fake"); }
    QString title() const override { return QStringLiteral("Fake"); }
    bool accepts(const QUrl &url) const override
    {
        return m_urls.contains(url.toString());
    }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        ++m_metaCalls;
        if (m_defer) {
            m_pending.append(qMakePair(url, std::move(cb)));
            return;
        }
        cb(m_meta);
    }
    // Delivers the queued answers (all with the configured meta).
    void deliver()
    {
        for (const auto &entry : m_pending)
            entry.second(m_meta);
        m_pending.clear();
    }
    QString fetchFailureHint() const override { return m_hint; }

    QStringList m_urls;
    TrackMeta m_meta;
    bool m_defer = false;
    int m_metaCalls = 0;
    QList<QPair<QUrl, MetadataCallback>> m_pending;
    QString m_hint;
};

// A provider that claims everything and queues its playback resolutions
// until resolveNow() delivers the next one (an empty stream = a failure).
class DeferredProvider : public SourceProvider
{
public:
    explicit DeferredProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("deferred"); }
    QString title() const override { return QStringLiteral("Deferred"); }
    bool accepts(const QUrl &url) const override { Q_UNUSED(url); return true; }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        Q_UNUSED(url);
        cb(TrackMeta());
    }
    void resolvePlayback(const MediaItem &item, const PlaybackCallback &cb) override
    {
        Q_UNUSED(item);
        m_pending.append(cb);
    }

    // Delivers the next queued resolution (an empty stream = a failure).
    void resolveNow(const QUrl &stream = QUrl())
    {
        if (m_pending.isEmpty())
            return;
        PlaybackCallback cb = m_pending.takeFirst();
        cb(stream);
    }

    QList<PlaybackCallback> m_pending;
};

#endif // DOOB_TESTS_TESTPROVIDERS_H
