#include "trackmetadata.h"

#include <attachedpictureframe.h>
#include <fileref.h>
#include <flacfile.h>
#include <flacpicture.h>
#include <id3v2tag.h>
#include <mp4coverart.h>
#include <mp4file.h>
#include <mp4item.h>
#include <mp4tag.h>
#include <mpegfile.h>
#include <tpropertymap.h>
#include <tstring.h>

#include <QByteArray>
#include <QFileInfo>
#include <QStringList>

namespace {

QString toQString(const TagLib::String &s)
{
    // to8Bit(true) returns the string encoded as UTF-8.
    return s.isEmpty() ? QString() : QString::fromStdString(s.to8Bit(true));
}

void setPicture(TrackMeta &meta, const TagLib::ByteVector &data,
                const QString &mime)
{
    if (data.isEmpty())
        return;
    meta.artData = QByteArray(data.data(), static_cast<int>(data.size()));
    meta.artMime = mime;
}

// The first embedded picture, per container: the picture lives outside
// the property map (ID3v2 APIC frames for MP3, FLAC's PICTURE block,
// the MP4 covr atom), so the read is format-specific, dispatched on the
// extension (the supported set — the worker never reads other files).
void readPicture(TrackMeta &meta, const QString &filePath)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    const QByteArray pathUtf8 = filePath.toUtf8();
    if (ext == QLatin1String("flac")) {
        TagLib::FLAC::File file(pathUtf8.constData());
        if (const auto &pictures = file.pictureList(); !pictures.isEmpty())
            setPicture(meta, pictures.front()->data(),
                       toQString(pictures.front()->mimeType()));
        return;
    }
    if (ext == QLatin1String("mp3")) {
        TagLib::MPEG::File file(pathUtf8.constData());
        if (!file.hasID3v2Tag())
            return;
        for (const TagLib::ID3v2::Frame *frame :
             file.ID3v2Tag()->frameList("APIC")) {
            const TagLib::ID3v2::AttachedPictureFrame *pic =
                dynamic_cast<const TagLib::ID3v2::AttachedPictureFrame *>(frame);
            if (pic == nullptr)
                continue;
            setPicture(meta, pic->picture(), toQString(pic->mimeType()));
            return;
        }
        return;
    }
    if (ext == QLatin1String("m4a") || ext == QLatin1String("aac")
        || ext == QLatin1String("mp4")) {
        TagLib::MP4::File file(pathUtf8.constData());
        const TagLib::MP4::Tag *tag =
            static_cast<const TagLib::MP4::Tag *>(file.tag());
        const TagLib::MP4::CoverArtList covers =
            tag->item("covr").toCoverArtList();
        if (covers.isEmpty())
            return;
        const TagLib::MP4::CoverArt &cover = covers.front();
        QString mime = QStringLiteral("image/jpeg"); // the container's default
        switch (cover.format()) {
        case TagLib::MP4::CoverArt::PNG:
            mime = QStringLiteral("image/png");
            break;
        case TagLib::MP4::CoverArt::BMP:
            mime = QStringLiteral("image/bmp");
            break;
        case TagLib::MP4::CoverArt::GIF:
            mime = QStringLiteral("image/gif");
            break;
        default:
            break;
        }
        setPicture(meta, cover.data(), mime);
    }
}

QString joinValues(const TagLib::StringList &values)
{
    QStringList out;
    for (const TagLib::String &v : values) {
        const QString s = toQString(v);
        if (!s.isEmpty())
            out << s;
    }
    return out.join(QStringLiteral("; "));
}

// Splits "3/10" into (3, 10); a bare "3" gives total 0.
void parseNumberTotal(const QString &s, int &number, int &total)
{
    const QStringList parts = s.split(QLatin1Char('/'));
    number = parts.at(0).trimmed().toInt();
    if (parts.size() > 1)
        total = parts.at(1).trimmed().toInt();
}

}

// Reads tags + duration from the file at filePath. Returns an empty TrackMeta
// if the file does not exist or is not recognized audio.
TrackMeta readTrackMetadata(const QString &filePath)
{
    TrackMeta meta;

    const QByteArray pathUtf8 = filePath.toUtf8();
    TagLib::FileRef file(pathUtf8.constData());
    if (file.isNull())
        return meta;

    if (const TagLib::AudioProperties *props = file.audioProperties()) {
        meta.durationMs = props->lengthInMilliseconds();
        meta.bitrate = props->bitrate();
        meta.sampleRate = props->sampleRate();
        meta.channels = props->channels();
    }

    // The unified property map is format-independent: ID3v2 frames, Vorbis
    // comments and MP4 atoms all map to the same canonical keys
    // (TITLE, ALBUMARTIST, TRACKNUMBER, ...).
    const TagLib::PropertyMap properties = file.properties();
    for (const auto &prop : properties) {
        const QString key = toQString(prop.first).toUpper();
        const QString value = joinValues(prop.second);
        if (value.isEmpty())
            continue;
        if (key == QLatin1String("TITLE")) {
            meta.title = value;
        } else if (key == QLatin1String("ARTIST")) {
            meta.artist = value;
        } else if (key == QLatin1String("ALBUM")) {
            meta.album = value;
        } else if (key == QLatin1String("ALBUMARTIST")) {
            meta.albumArtist = value;
        } else if (key == QLatin1String("COMPOSER")) {
            meta.composer = value;
        } else if (key == QLatin1String("GENRE")) {
            meta.genre = value;
        } else if (key == QLatin1String("COMMENT")
                   || key == QLatin1String("DESCRIPTION")) {
            // ID3v2/MP4 call the field COMMENT, Vorbis DESCRIPTION.
            meta.comment = value;
        } else if (key == QLatin1String("DATE")) {
            meta.year = value.left(4).toInt();
        } else if (key == QLatin1String("TRACKNUMBER")) {
            int number = 0;
            int total = 0;
            parseNumberTotal(value, number, total);
            if (number > 0)
                meta.trackNumber = number;
            meta.trackTotal = total;
        } else if (key == QLatin1String("DISCNUMBER")) {
            int number = 0;
            int total = 0;
            parseNumberTotal(value, number, total);
            if (number > 0)
                meta.discNumber = number;
            meta.discTotal = total;
        } else {
            meta.extra.insert(key, value);
        }
    }

    readPicture(meta, filePath);
    return meta;
}

void readAllSources(const QList<QUrl> &sources,
                    const std::function<void(const QUrl &, const TrackMeta &)> &report)
{
    for (const QUrl &source : sources)
        report(source, readTrackMetadata(source.toLocalFile()));
}

MediaItem mediaItemFromMeta(quint64 id, const QUrl &source, const TrackMeta &meta,
                            MediaItem::Kind kind)
{
    // Tag values come from the wild: strip surrounding whitespace from
    // every field at this one choke point, so every import path (files,
    // providers, resync, the playlist import) stores clean fields.
    TrackMeta clean = meta;
    clean.title = clean.title.trimmed();
    clean.artist = clean.artist.trimmed();
    clean.album = clean.album.trimmed();
    clean.albumArtist = clean.albumArtist.trimmed();
    clean.composer = clean.composer.trimmed();
    clean.genre = clean.genre.trimmed();
    clean.comment = clean.comment.trimmed();
    for (auto it = clean.extra.begin(); it != clean.extra.end(); ++it)
        it.value() = it.value().trimmed();

    MediaItem item;
    item.id = id;
    item.kind = kind;
    item.source = source;

    // Prefer real tags; fall back to the file name (local) or the URL's
    // host + path (a non-local source has no file name) for the title.
    const QString fallbackTitle = source.isLocalFile()
        ? QFileInfo(source.toLocalFile()).completeBaseName().replace('_', ' ')
        : source.host() + source.path();
    item.title = clean.title.isEmpty() ? fallbackTitle : clean.title;
    item.artist = clean.artist;
    item.album = clean.album;
    item.albumArtist = clean.albumArtist;
    item.composer = clean.composer;
    item.genre = clean.genre;
    item.comment = clean.comment;
    item.trackNumber = meta.trackNumber;
    item.trackTotal = meta.trackTotal;
    item.discNumber = meta.discNumber;
    item.discTotal = meta.discTotal;
    item.year = meta.year;
    item.durationMs = meta.durationMs;
    item.bitrate = meta.bitrate;
    item.sampleRate = meta.sampleRate;
    item.channels = meta.channels;
    item.extra = clean.extra;
    return item;
}
