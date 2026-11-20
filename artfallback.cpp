#include "artfallback.h"

#include <QDateTime>

#include "artsource.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "settingsmanager.h"

namespace {
// The value of the setting that disables the fallback.
const QString kOff = QStringLiteral("off");
}

ArtFallback* ArtFallback::getInstance(QObject *parent)
{
    if (m_instance == nullptr)
        m_instance = new ArtFallback(parent);
    return m_instance;
}

void ArtFallback::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

ArtFallback::ArtFallback(QObject *parent)
    : QObject(parent)
{
    SettingsManager::getInstance()->registerSetting(
        { FallbackKey, QStringLiteral("Art fallback"),
          QStringLiteral("The enrichment source that looks up the art of "
                         "a playing item that has none (its id in the "
                         "sources registry, the built-in is \"itunes\"); "
                         "\"off\" disables the lookup."),
          QStringLiteral("itunes"), QString(), });
    connect(PlaybackManager::getInstance(),
            &PlaybackManager::playingItemChanged, this,
            &ArtFallback::onPlayingItemChanged);
}

void ArtFallback::setMissTtlMs(qint64 ttlMs)
{
    m_missTtlMs = ttlMs;
}

void ArtFallback::onPlayingItemChanged(const MediaItem &item)
{
    // The stop (an invalid item) and the items that already carry art
    // never start a lookup.
    if (!item.isValid()
            || MediaItemStore::getInstance()->hasArtwork(item.id))
        return;
    // One lookup at a time: a play start in flight is skipped (the
    // item's next play start retries).
    if (m_lookupRunning)
        return;
    const QString sourceId =
        SettingsManager::getInstance()->value(FallbackKey).toString();
    if (sourceId.isEmpty() || sourceId == kOff)
        return;
    ArtSource* source = ArtRegistry::getInstance()->source(sourceId);
    if (source == nullptr)
        return; // a removed plugin's id: no source, no lookup
    if (isNegativeCached(item))
        return;
    m_lookupRunning = true;
    source->fetch(item, ArtKind::Art,
                  [this, item](const QString &mime,
                               const QByteArray &data) {
        m_lookupRunning = false;
        if (data.isEmpty()) {
            recordMiss(item.id);
            return;
        }
        // The hit goes through the store's write-through: the
        // itemArtworkChanged signal updates the displays.
        MediaItemStore::getInstance()->setArtwork(item.id, mime, data);
    });
}

bool ArtFallback::isNegativeCached(const MediaItem &item)
{
    const auto it = m_misses.constFind(item.id);
    return it != m_misses.constEnd()
        && QDateTime::currentMSecsSinceEpoch() - *it < m_missTtlMs;
}

void ArtFallback::recordMiss(quint64 id)
{
    pruneExpiredMisses();
    m_misses.insert(id, QDateTime::currentMSecsSinceEpoch());
}

void ArtFallback::pruneExpiredMisses()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_misses.begin(); it != m_misses.end();)
        it = (now - it.value() >= m_missTtlMs) ? m_misses.erase(it)
                                               : std::next(it);
}
