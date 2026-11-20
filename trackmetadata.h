#ifndef TRACKMETADATA_H
#define TRACKMETADATA_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QUrl>

#include <functional>

#include "mediaitem.h"

// Metadata read from an audio file's tags (via Taglib's format-independent
// property map, which maps ID3v2 frames, Vorbis comments and MP4 atoms to
// the same canonical keys). Tags that do not map to a standard field are
// kept in extra with their key normalized to uppercase. Fields are left
// empty / zero when the file has no readable tags or is not valid audio, so
// callers can fall back to other sources (e.g. the file name).
struct TrackMeta
{
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
    int bitrate = 0;   // kbps
    int sampleRate = 0; // Hz
    int channels = 0;
    QMap<QString, QString> extra;
    // The first embedded picture (ID3v2 APIC, FLAC PICTURE, MP4 covr),
    // when the container carries one: the mime type and the image bytes
    // (a provider fills the same fields with its fetched cover). The art
    // is not part of isEmpty() — it is not metadata, so a read is empty
    // or non-empty with or without its art.
    QString artMime;
    QByteArray artData;

    bool isEmpty() const
    {
        return title.isEmpty() && artist.isEmpty() && album.isEmpty()
            && albumArtist.isEmpty() && composer.isEmpty() && genre.isEmpty()
            && comment.isEmpty() && trackNumber == 0 && trackTotal == 0
            && discNumber == 0 && discTotal == 0 && year == 0
            && durationMs == 0 && bitrate == 0 && sampleRate == 0
            && channels == 0 && extra.isEmpty();
    }
};
Q_DECLARE_METATYPE(TrackMeta)

// Reads tags + duration from the file at filePath. Returns an empty TrackMeta
// if the file does not exist or is not recognized audio.
TrackMeta readTrackMetadata(const QString &filePath);

// Reads the tags of every source (in order) and passes each to report — the
// local-file read loop the import workers share. report is invoked on the
// calling thread (the workers' thread); a source that is not a readable
// local file reports an empty TrackMeta (readTrackMetadata's contract).
void readAllSources(const QList<QUrl> &sources,
                    const std::function<void(const QUrl &, const TrackMeta &)> &report);

// Builds the MediaItem for a freshly read source: the file name (underscores
// as spaces, host + path for non-local sources) stands in for a missing
// title. The string fields are trimmed of surrounding whitespace (tag values
// come from the wild). Shared by the library import and the standalone-item
// creation, so both build items identically. kind defaults to Local (a
// provider-claimed source is built as Plugin, a plain stream URL as Remote).
MediaItem mediaItemFromMeta(quint64 id, const QUrl &source, const TrackMeta &meta,
                            MediaItem::Kind kind = MediaItem::Local);

#endif // TRACKMETADATA_H
