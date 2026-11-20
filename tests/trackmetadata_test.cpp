#include <QtTest>
#include <QColor>
#include <QFile>
#include <QTemporaryDir>

#include <flacfile.h>
#include <flacpicture.h>

#include "taggedfile.h"
#include "testsupport.h"
#include "trackmetadata.h"

namespace {

// Embeds the picture in a FLAC file (TagLib's PICTURE block, the format
// the native read dispatches on); false on failure.
bool embedFlacPicture(const QString &path, const QByteArray &data,
                      const QString &mime)
{
    TagLib::FLAC::File file(path.toUtf8().constData());
    auto *pic = new TagLib::FLAC::Picture;
    pic->setData(TagLib::ByteVector(data.constData(),
                                    static_cast<int>(data.size())));
    pic->setMimeType(mime.toUtf8().constData());
    // pictureList() returns a copy: addPicture hands the file the
    // ownership (the save persists it).
    file.addPicture(pic);
    return file.save();
}

class TrackMetadataTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void readsTagsFromRealFile();
    void readsStandardAndExtraFields();
    void negativeNumberTagsAreIgnored();
    void returnsEmptyForInvalidFile();
    void returnsEmptyForMissingFile();
    void itemFromMetaTrimsWhitespaceFromAllFields();
    void itemFromMetaFallsBackToSourceForBlankTitle();
    void readsEmbeddedPictureFromFlac();
    void artOnlyReadIsStillEmpty();

private:
    QTemporaryDir m_dir;
};

void TrackMetadataTest::init()
{
    QVERIFY(m_dir.isValid());
}

void TrackMetadataTest::readsTagsFromRealFile()
{
    const QUrl url = makeTaggedFile(m_dir.path(), "song.flac",
                                    "Tagged Title", "Tagged Artist", "Tagged Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const TrackMeta meta = readTrackMetadata(url.toLocalFile());

    QCOMPARE(meta.title, QString("Tagged Title"));
    QCOMPARE(meta.artist, QString("Tagged Artist"));
    QCOMPARE(meta.album, QString("Tagged Album"));
    QVERIFY(meta.durationMs > 500 && meta.durationMs < 2000);
    QVERIFY(meta.bitrate > 0);
    QVERIFY(meta.sampleRate > 0);
    QVERIFY(meta.channels > 0);

    // The only non-standard tag is the encoder tag the ffmpeg muxer writes;
    // no user tag may leak into extra.
    for (const QString &key : meta.extra.keys())
        QCOMPARE(key, QString("ENCODER"));
}

void TrackMetadataTest::readsStandardAndExtraFields()
{
    const QUrl url = makeTaggedFileWith(m_dir.path(), "full.flac",
        { { "TITLE", "Full Title" }, { "ARTIST", "Full Artist" },
          { "ALBUM", "Full Album" }, { "ALBUMARTIST", "Full Album Artist" },
          { "COMPOSER", "Full Composer" }, { "GENRE", "Jazz" },
          { "COMMENT", "A comment" }, { "DATE", "1999" },
          { "TRACKNUMBER", "3/10" }, { "DISCNUMBER", "1/2" },
          { "BPM", "128" }, { "LYRICIST", "Full Lyricist" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const TrackMeta meta = readTrackMetadata(url.toLocalFile());

    QCOMPARE(meta.title, QString("Full Title"));
    QCOMPARE(meta.artist, QString("Full Artist"));
    QCOMPARE(meta.album, QString("Full Album"));
    QCOMPARE(meta.albumArtist, QString("Full Album Artist"));
    QCOMPARE(meta.composer, QString("Full Composer"));
    QCOMPARE(meta.genre, QString("Jazz"));
    QCOMPARE(meta.comment, QString("A comment"));
    QCOMPARE(meta.year, 1999);
    QCOMPARE(meta.trackNumber, 3);
    QCOMPARE(meta.trackTotal, 10);
    QCOMPARE(meta.discNumber, 1);
    QCOMPARE(meta.discTotal, 2);
    QVERIFY(meta.durationMs > 500 && meta.durationMs < 2000);

    // Non-standard tags are kept in extra (plus the encoder tag the ffmpeg
    // muxer writes).
    QCOMPARE(meta.extra.value("BPM"), QString("128"));
    QCOMPARE(meta.extra.value("LYRICIST"), QString("Full Lyricist"));
    QCOMPARE(meta.extra.size(), 3);
    QVERIFY(!meta.extra.contains("TITLE"));
    QVERIFY(!meta.extra.contains("TRACKNUMBER"));
}

void TrackMetadataTest::negativeNumberTagsAreIgnored()
{
    // A negative track/disc number is malformed data: it must be dropped,
    // not stored (TRACKNUMBER is already guarded; DISCNUMBER must match).
    const QUrl url = makeTaggedFileWith(m_dir.path(), "neg.flac",
        { { "TITLE", "Neg" }, { "TRACKNUMBER", "-1/10" },
          { "DISCNUMBER", "-1/2" }, });
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const TrackMeta meta = readTrackMetadata(url.toLocalFile());

    QCOMPARE(meta.trackNumber, 0);
    QCOMPARE(meta.trackTotal, 10);
    QCOMPARE(meta.discNumber, 0);
    QCOMPARE(meta.discTotal, 2);
}

void TrackMetadataTest::returnsEmptyForInvalidFile()
{
    const QString path = m_dir.path() + "/garbage.flac";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("\x00\x01\x02");
    f.close();

    const TrackMeta meta = readTrackMetadata(path);
    QVERIFY(meta.isEmpty());
}

void TrackMetadataTest::returnsEmptyForMissingFile()
{
    const TrackMeta meta = readTrackMetadata(m_dir.path() + "/nope.flac");
    QVERIFY(meta.isEmpty());
}

void TrackMetadataTest::itemFromMetaTrimsWhitespaceFromAllFields()
{
    // Tag values come from the wild: every string field (including the
    // extra tags) is trimmed of surrounding whitespace, at this one choke
    // point, for every import path.
    TrackMeta meta;
    meta.title = QStringLiteral("  Padded Title  ");
    meta.artist = QStringLiteral("\tPadded Artist\n");
    meta.album = QStringLiteral(" \t Padded Album ");
    meta.albumArtist = QStringLiteral("  AA  ");
    meta.composer = QStringLiteral("  CC  ");
    meta.genre = QStringLiteral("  Jazz  ");
    meta.comment = QStringLiteral("  A comment  ");
    meta.extra.insert(QStringLiteral("BPM"), QStringLiteral("  128  "));
    meta.year = 1999;
    meta.durationMs = 133000;

    const MediaItem item = mediaItemFromMeta(
        1, QUrl(QStringLiteral("https://music.example.com/watch?v=1")), meta,
        MediaItem::Plugin);

    QCOMPARE(item.title, QString("Padded Title"));
    QCOMPARE(item.artist, QString("Padded Artist"));
    QCOMPARE(item.album, QString("Padded Album"));
    QCOMPARE(item.albumArtist, QString("AA"));
    QCOMPARE(item.composer, QString("CC"));
    QCOMPARE(item.genre, QString("Jazz"));
    QCOMPARE(item.comment, QString("A comment"));
    QCOMPARE(item.extra.value("BPM"), QString("128"));
    QCOMPARE(item.year, 1999);
    QCOMPARE(item.durationMs, 133000);
}

void TrackMetadataTest::itemFromMetaFallsBackToSourceForBlankTitle()
{
    // A title that is whitespace-only is empty after the trim: the
    // fallback (host + path for a non-local source) applies.
    TrackMeta meta;
    meta.title = QStringLiteral("   ");
    meta.artist = QStringLiteral("  Solo  ");

    const MediaItem item = mediaItemFromMeta(
        1, QUrl(QStringLiteral("https://music.example.com/watch?v=9")), meta);

    // The fallback is the host + path (the query is not part of it).
    QCOMPARE(item.title, QString("music.example.com/watch"));
    QCOMPARE(item.artist, QString("Solo"));
}

void TrackMetadataTest::readsEmbeddedPictureFromFlac()
{
    const QUrl url = makeTaggedFile(m_dir.path(), "picture.flac",
                                    "Pictureed", "Pic Artist", "Pic Album");
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const QByteArray jpeg = test::solidImageBytes(QColor(200, 30, 30), 8, "JPG");
    QVERIFY(!jpeg.isEmpty());
    QVERIFY(embedFlacPicture(url.toLocalFile(), jpeg, "image/jpeg"));

    const TrackMeta meta = readTrackMetadata(url.toLocalFile());
    QCOMPARE(meta.title, QString("Pictureed"));
    QCOMPARE(meta.artist, QString("Pic Artist"));
    QCOMPARE(meta.artMime, QString("image/jpeg"));
    QCOMPARE(meta.artData, jpeg);
}

void TrackMetadataTest::artOnlyReadIsStillEmpty()
{
    // The art is not metadata: embedding a picture does not change
    // isEmpty() (the art rides alongside the read).
    const QUrl url = makeTaggedFileWith(m_dir.path(), "artonly.flac", {});
    if (url.isEmpty())
        QSKIP("ffmpeg not available to generate a tagged file");

    const TrackMeta before = readTrackMetadata(url.toLocalFile());

    const QByteArray jpeg = test::solidImageBytes(QColor(200, 30, 30), 8, "JPG");
    QVERIFY(!jpeg.isEmpty());
    QVERIFY(embedFlacPicture(url.toLocalFile(), jpeg, "image/jpeg"));

    const TrackMeta meta = readTrackMetadata(url.toLocalFile());
    QCOMPARE(meta.isEmpty(), before.isEmpty());
    QCOMPARE(meta.artMime, QString("image/jpeg"));
    QCOMPARE(meta.artData, jpeg);
}
}


QTEST_MAIN(TrackMetadataTest)
#include "trackmetadata_test.moc"
