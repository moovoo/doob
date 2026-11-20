#ifndef MEDIAITEM_H
#define MEDIAITEM_H

#include <QDateTime>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QUrl>
#include <QVariantMap>

struct MediaItem
{
    enum Kind { Local = 0, Remote, Plugin };

    quint64 id = 0;
    // Library membership: the library is a set over the single item universe
    // (persisted as the in_library column). Owned by the LibraryManager.
    bool inLibrary = false;
    Kind kind = Local;
    QUrl source;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString composer;
    QString genre;
    QString comment;
    int trackNumber = 0;
    int trackTotal = 0;
    int discNumber = 0;
    int discTotal = 0;
    int year = 0;
    qint64 durationMs = 0;
    int bitrate = 0;    // kbps
    int sampleRate = 0; // Hz
    int channels = 0;
    // The source provider that owns the item (its id; empty for built-in
    // local files and plain stream URLs). Set once at import time; the
    // source is stored as pasted, never rewritten.
    QString provider;
    // Usage statistics: when the item was imported, when it last started
    // playing, and how often (runtime-updated by the store).
    QDateTime importedAt;
    QDateTime lastPlayedAt;
    quint64 playCount = 0;
    // Tags that do not map to a standard field (key normalized to uppercase).
    QMap<QString, QString> extra;

    bool isValid() const { return id != 0 && !source.isEmpty(); }

    bool operator==(const MediaItem &other) const { return id == other.id; }
    bool operator!=(const MediaItem &other) const { return id != other.id; }

    QVariantMap toVariantMap() const
    {
        QVariantMap m;
        m["id"] = static_cast<qulonglong>(id);
        m["inLibrary"] = inLibrary;
        m["kind"] = static_cast<int>(kind);
        m["source"] = source.toString();
        m["title"] = title;
        m["artist"] = artist;
        m["album"] = album;
        m["albumArtist"] = albumArtist;
        m["composer"] = composer;
        m["genre"] = genre;
        m["comment"] = comment;
        m["trackNumber"] = trackNumber;
        m["trackTotal"] = trackTotal;
        m["discNumber"] = discNumber;
        m["discTotal"] = discTotal;
        m["year"] = year;
        m["durationMs"] = static_cast<qint64>(durationMs);
        m["bitrate"] = bitrate;
        m["sampleRate"] = sampleRate;
        m["channels"] = channels;
        m["provider"] = provider;
        m["importedAt"] = importedAt;
        m["lastPlayedAt"] = lastPlayedAt;
        m["playCount"] = static_cast<qulonglong>(playCount);
        QVariantMap extraMap;
        for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
            extraMap.insert(it.key(), it.value());
        m["extra"] = extraMap;
        return m;
    }

    static MediaItem fromVariantMap(const QVariantMap &m)
    {
        MediaItem item;
        item.id = static_cast<quint64>(m.value("id", 0ULL).toULongLong());
        item.inLibrary = m.value("inLibrary", false).toBool();
        item.kind = static_cast<Kind>(m.value("kind", static_cast<int>(Local)).toInt());
        item.source = QUrl(m.value("source").toString());
        item.title = m.value("title").toString();
        item.artist = m.value("artist").toString();
        item.album = m.value("album").toString();
        item.albumArtist = m.value("albumArtist").toString();
        item.composer = m.value("composer").toString();
        item.genre = m.value("genre").toString();
        item.comment = m.value("comment").toString();
        item.trackNumber = m.value("trackNumber", 0).toInt();
        item.trackTotal = m.value("trackTotal", 0).toInt();
        item.discNumber = m.value("discNumber", 0).toInt();
        item.discTotal = m.value("discTotal", 0).toInt();
        item.year = m.value("year", 0).toInt();
        item.durationMs = m.value("durationMs", 0LL).toLongLong();
        item.bitrate = m.value("bitrate", 0).toInt();
        item.sampleRate = m.value("sampleRate", 0).toInt();
        item.channels = m.value("channels", 0).toInt();
        item.provider = m.value("provider").toString();
        item.importedAt = m.value("importedAt").toDateTime();
        item.lastPlayedAt = m.value("lastPlayedAt").toDateTime();
        item.playCount = static_cast<quint64>(m.value("playCount", 0ULL).toULongLong());
        const QVariantMap extraMap = m.value("extra").toMap();
        for (auto it = extraMap.cbegin(); it != extraMap.cend(); ++it)
            item.extra.insert(it.key(), it.value().toString());
        return item;
    }
};

Q_DECLARE_METATYPE(MediaItem)

#endif // MEDIAITEM_H
