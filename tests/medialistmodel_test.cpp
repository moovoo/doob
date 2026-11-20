#include <QtTest>
#include <QSignalSpy>

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QPixmap>
#include <QTemporaryDir>

#include <memory>

#include "mediaitemstore.h"
#include "medialistmodel.h"
#include "testsupport.h"

namespace {

// The payload is {"ids": [...], "rows": [...], "origin": "…"} (a bare id
// array is the older form and has no rows and no origin).
QJsonArray payloadIds(const QMimeData &mime)
{
    const QJsonDocument doc = QJsonDocument::fromJson(mime.data(MediaListModel::mediaMimeType()));
    return doc.isObject() ? doc.object().value("ids").toArray() : doc.array();
}

QList<quint64> parseIds(const QMimeData &mime)
{
    QList<quint64> ids;
    for (const auto &v : payloadIds(mime))
        ids.append(static_cast<quint64>(v.toVariant().toULongLong()));
    return ids;
}

QList<int> parseRows(const QMimeData &mime)
{
    QList<int> rows;
    const QJsonDocument doc = QJsonDocument::fromJson(mime.data(MediaListModel::mediaMimeType()));
    if (!doc.isObject())
        return rows;
    for (const auto &v : doc.object().value("rows").toArray())
        rows.append(v.toInt());
    return rows;
}

QString parseOrigin(const QMimeData &mime)
{
    const QJsonDocument doc = QJsonDocument::fromJson(mime.data(MediaListModel::mediaMimeType()));
    return doc.isObject() ? doc.object().value("origin").toString() : QString();
}

class MediaListModelTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void setItemIdsPopulatesRows();
    void rowOfNamesTheFirstRowOfTheId();
    void flatStructure();
    void headerDataLabelsColumns();
    void columnDisplayExtendedFields();
    void columnKeyLogicalLabelRoundTrip();
    void compareColumnNumericByFieldAndTextCaseInsensitive();
    void formatDurationRoundsToSeconds();
    void setRowStatusMarksRow();
    void setRowStatusMarksExactlyThatRow();
    void rowStatusesShowTheirGlyphAndTooltip();
    void rowStatusForNamesTheState();
    void setItemIdsKeepsStatusRow();
    void mimeDataCarriesIdsRowsOrigin();
    void mimeDataSortsRowsAscending();
    void dragDataFromMimeDataParsesBareIdArray();
    void dragDataFromMimeDataRejectsForeignMime();
    void dropMimeDataEmitsItemsDropped();
    void dropOnEmptyAreaAppendsAtEnd();
    void dropRowClampsToRowCount();
    void unknownMimeDropRejected();
    void localFileDropEmitsFilesDropped();
    void nonLocalUrlDropEmitsUrlsDropped();
    void mixedFileAndUrlDropEmitsBothSignals();
    void fileDropIntoEmptyModelIsAccepted();
    void rootIndexAcceptsDrops();
    void updateItemRepaintsRowInPlace();
    void updateItemRepaintsEveryRowOfDuplicate();
    void artworkColumnShowsDecorationOnly();
    void setItemIdsDropsStaleRowIndex();
    void dropIndexForRowNormalizes();
    void searchableColumnsAreTheDataColumns();

private:
    MediaItem makeItem(quint64 id, const QString &title,
                       const QString &artist = QString(),
                       const QString &album = QString(),
                       int trackNumber = 0, int discNumber = 0);
    void setItems(MediaListModel &model, const QList<MediaItem> &items);

    MediaItemStore* m_store = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void MediaListModelTest::init()
{
    // The model stores ids only and resolves them against the standalone
    // store, so test items must live there; start each test with it empty.
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    m_store->setStoragePath(m_dir->path() + "/doob.db");
    m_store->clear();
}

void MediaListModelTest::cleanup()
{
    m_store->clear();
}

MediaItem MediaListModelTest::makeItem(quint64 id, const QString &title,
                                       const QString &artist, const QString &album,
                                       int trackNumber, int discNumber)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.artist = artist;
    item.album = album;
    item.trackNumber = trackNumber;
    item.discNumber = discNumber;
    item.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/%1.mp3").arg(id));
    return item;
}

void MediaListModelTest::setItems(MediaListModel &model, const QList<MediaItem> &items)
{
    for (MediaItem item : items) {
        m_store->remove(item.id);
        QVERIFY(m_store->insert(item));
    }
    QList<quint64> ids;
    for (const MediaItem &item : items)
        ids.append(item.id);
    model.setItemIds(ids);
}

void MediaListModelTest::setItemIdsPopulatesRows()
{
    MediaListModel model;
    setItems(model, {
        makeItem(1, "Alpha"),
        makeItem(2, "Beta"),
        makeItem(3, "Gamma"),
    });

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(model.columnCount(), MediaListModel::ColumnCount);
    QCOMPARE(model.columnCount(model.index(0, 0)), 0);
    QCOMPARE(model.idAt(0), 1ULL);
    QCOMPARE(model.idAt(1), 2ULL);
    QCOMPARE(model.idAt(2), 3ULL);
    QCOMPARE(model.idAt(3), 0ULL);
    QCOMPARE(model.idAt(model.index(1, 0)), 2ULL);
    QCOMPARE(model.idAt(QModelIndex()), 0ULL);
    // The row's id rides UserRole (a stable value the view can key on).
    QCOMPARE(model.data(model.index(2, 0), Qt::UserRole).toULongLong(), 3ULL);
    // A dangling id (the store no longer holds it) resolves to empty cells.
    m_store->remove(2);
    QCOMPARE(model.data(model.index(1, MediaListModel::Title), Qt::DisplayRole).toString(),
             QString());
}

void MediaListModelTest::rowOfNamesTheFirstRowOfTheId()
{
    MediaListModel model;
    setItems(model, {
        makeItem(1, "Alpha"),
        makeItem(2, "Beta"),
        makeItem(1, "Alpha copy"),
    });

    QCOMPARE(model.rowOf(1), 0);
    QCOMPARE(model.rowOf(2), 1);
    // An id that is not in the model (and the invalid one) is absent.
    QCOMPARE(model.rowOf(3), -1);
    QCOMPARE(model.rowOf(0), -1);
}

void MediaListModelTest::flatStructure()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });

    // The rows live only at the root level: a row has no parent and no
    // children, and asking for a child of a row yields no index at all.
    const QModelIndex row = model.index(1, 0);
    QVERIFY(row.isValid());
    QVERIFY(!model.parent(row).isValid());
    QVERIFY(!model.hasChildren(row));
    QVERIFY(!model.index(0, 0, row).isValid());
    QVERIFY(model.hasChildren(QModelIndex()));
    QVERIFY(!model.index(2, 0).isValid());
}

void MediaListModelTest::headerDataLabelsColumns()
{
    MediaListModel model;
    QCOMPARE(model.headerData(MediaListModel::Artwork, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Artwork"));
    QCOMPARE(model.headerData(MediaListModel::Title, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Title"));
    QCOMPARE(model.headerData(MediaListModel::Artist, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Artist"));
    QCOMPARE(model.headerData(MediaListModel::Album, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Album"));
    QCOMPARE(model.headerData(MediaListModel::AlbumArtist, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Album Artist"));
    QCOMPARE(model.headerData(MediaListModel::Track, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Track"));
    QCOMPARE(model.headerData(MediaListModel::Duration, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString("Duration"));
    // The marker column is deliberately headerless.
    QCOMPARE(model.headerData(MediaListModel::Playing, Qt::Horizontal, Qt::DisplayRole).toString(),
             QString());
    // Vertical and other roles are empty.
    QCOMPARE(model.headerData(0, Qt::Vertical, Qt::DisplayRole).toString(), QString());
}

void MediaListModelTest::columnDisplayExtendedFields()
{
    MediaItem full;
    full.id = 1;
    full.title = "Full";
    full.artist = "A";
    full.album = "Album";
    full.albumArtist = "AA";
    full.composer = "C";
    full.genre = "G";
    full.trackNumber = 3;
    full.trackTotal = 10;
    full.discNumber = 1;
    full.discTotal = 2;
    full.year = 1999;
    full.durationMs = 90000;
    full.comment = "A comment";
    full.bitrate = 128;
    full.sampleRate = 44100;
    full.channels = 2;
    full.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/full.mp3"));
    QVERIFY(m_store->insert(full));

    // A second item with a track number but no total: plain "N".
    MediaItem noTotal;
    noTotal.id = 2;
    noTotal.title = "NoTotal";
    noTotal.trackNumber = 3;
    noTotal.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/nototal.mp3"));
    QVERIFY(m_store->insert(noTotal));
    MediaListModel model;
    model.setItemIds({ 1, 2 });

    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Album), QString("Album"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::AlbumArtist), QString("AA"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Composer), QString("C"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Genre), QString("G"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Track), QString("3/10"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Disc), QString("1/2"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Year), QString("1999"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Duration), QString("1:30"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Comment), QString("A comment"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Bitrate), QString("128 kbps"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::SampleRate), QString("44100 Hz"));
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::Channels), QString("2"));
    QCOMPARE(MediaListModel::columnDisplay(noTotal, MediaListModel::Track), QString("3"));

    // data() agrees with columnDisplay (the two are one source of truth).
    QCOMPARE(model.data(model.index(0, MediaListModel::Track), Qt::DisplayRole).toString(),
             QString("3/10"));
    // Absent values (0 / empty) render as empty cells.
    for (const int column : { MediaListModel::Album, MediaListModel::AlbumArtist,
                              MediaListModel::Composer, MediaListModel::Genre,
                              MediaListModel::Disc, MediaListModel::Year,
                              MediaListModel::Comment, MediaListModel::Bitrate,
                              MediaListModel::SampleRate, MediaListModel::Channels, })
        QCOMPARE(model.data(model.index(1, column), Qt::DisplayRole).toString(), QString());
    // Unknown columns yield an empty display.
    QCOMPARE(MediaListModel::columnDisplay(full, MediaListModel::ColumnCount), QString());
}

void MediaListModelTest::columnKeyLogicalLabelRoundTrip()
{
    for (int logical = 0; logical < MediaListModel::ColumnCount; ++logical) {
        const QString key = MediaListModel::columnKey(logical);
        QVERIFY(!key.isEmpty());
        QCOMPARE(MediaListModel::columnLogical(key), logical);
        QVERIFY(!MediaListModel::columnLabel(logical).isEmpty());
    }
    QCOMPARE(MediaListModel::columnLogical("bogus"), -1);
    QCOMPARE(MediaListModel::columnKey(MediaListModel::ColumnCount), QString());
}

void MediaListModelTest::compareColumnNumericByFieldAndTextCaseInsensitive()
{
    MediaItem ten;
    ten.id = 1;
    ten.title = "ten";
    ten.trackNumber = 10;
    ten.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/ten.mp3"));
    MediaItem nine;
    nine.id = 2;
    nine.title = "Nine";
    nine.trackNumber = 9;
    nine.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/nine.mp3"));
    MediaItem two;
    two.id = 3;
    two.title = "b";
    two.trackNumber = 2;
    two.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/two.mp3"));

    // "10" must sort after "9": by the field, not the display string.
    QVERIFY(MediaListModel::compareColumn(two, nine, MediaListModel::Track) < 0);
    QVERIFY(MediaListModel::compareColumn(nine, ten, MediaListModel::Track) < 0);
    QCOMPARE(MediaListModel::compareColumn(ten, nine, MediaListModel::Track), 1);
    // Text is case-insensitive.
    QVERIFY(MediaListModel::compareColumn(two, nine, MediaListModel::Title) < 0); // "b" < "Nine"
    QCOMPARE(MediaListModel::compareColumn(ten, ten, MediaListModel::Title), 0);
}

void MediaListModelTest::formatDurationRoundsToSeconds()
{
    QCOMPARE(MediaListModel::formatDuration(0), QString("0:00"));
    QCOMPARE(MediaListModel::formatDuration(90000), QString("1:30"));
    QCOMPARE(MediaListModel::formatDuration(60000), QString("1:00"));
    QCOMPARE(MediaListModel::formatDuration(59999), QString("0:59"));
    QCOMPARE(MediaListModel::formatDuration(-1), QString("0:00")); // unset
    // withHours: M:SS below the hour, H:MM:SS from it on.
    QCOMPARE(MediaListModel::formatDuration(59999, true), QString("0:59"));
    QCOMPARE(MediaListModel::formatDuration(3600000, true), QString("1:00:00"));
    QCOMPARE(MediaListModel::formatDuration(3661000, true), QString("1:01:01"));
    QCOMPARE(MediaListModel::formatDuration(90061000, true), QString("25:01:01"));
    QCOMPARE(MediaListModel::formatDuration(-1, true), QString("0:00"));
}

void MediaListModelTest::setRowStatusMarksRow()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B"), makeItem(3, "C") });

    model.setRowStatus(1, MediaListModel::RowStatus::Playing);
    QCOMPARE(model.data(model.index(0, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());
    QCOMPARE(model.data(model.index(1, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString(QChar(0x25B6)));
    QCOMPARE(model.data(model.index(2, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());
    QCOMPARE(model.statusRow(), 1);
    QCOMPARE(model.rowStatus(), MediaListModel::RowStatus::Playing);

    model.setRowStatus(-1, MediaListModel::RowStatus::None);
    QCOMPARE(model.data(model.index(1, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());
    QCOMPARE(model.statusRow(), -1);
}

void MediaListModelTest::setRowStatusMarksExactlyThatRow()
{
    // A playlist may hold the same item in several rows: the marker is a
    // row, so it marks exactly the row named — even another copy of the
    // same item.
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });
    model.setItemIds({ 1, 2, 1 });
    model.setRowStatus(2, MediaListModel::RowStatus::Playing);

    QCOMPARE(model.data(model.index(0, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());
    QCOMPARE(model.data(model.index(1, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());
    QCOMPARE(model.data(model.index(2, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString(QChar(0x25B6)));
}

void MediaListModelTest::rowStatusesShowTheirGlyphAndTooltip()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });

    struct Case
    {
        MediaListModel::RowStatus status;
        QChar glyph;
        QString tooltip;
    };
    const QList<Case> cases = {
        { MediaListModel::RowStatus::Playing, QChar(0x25B6), "Playing" },
        { MediaListModel::RowStatus::Paused, QChar(0x23F8), "Paused" },
        { MediaListModel::RowStatus::Downloading, QChar(0x2193), "Downloading…" },
        { MediaListModel::RowStatus::Error, QChar(0x26A0), "Cannot play" },
    };
    for (const Case &c : cases) {
        model.setRowStatus(0, c.status);
        QCOMPARE(model.data(model.index(0, MediaListModel::Playing), Qt::DisplayRole).toString(),
                 QString(c.glyph));
        QCOMPARE(model.data(model.index(0, MediaListModel::Playing), Qt::ToolTipRole).toString(),
                 c.tooltip);
        QCOMPARE(MediaListModel::rowStatusChar(c.status), QString(c.glyph));
        QCOMPARE(MediaListModel::rowStatusText(c.status), c.tooltip);
    }

    model.setRowStatus(0, MediaListModel::RowStatus::None);
    QCOMPARE(model.data(model.index(0, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString());
    QCOMPARE(MediaListModel::rowStatusChar(MediaListModel::RowStatus::None), QString());
}

void MediaListModelTest::rowStatusForNamesTheState()
{
    MediaItem resolving;
    resolving.id = 7;
    resolving.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/r.mp3"));
    MediaItem playing;
    playing.id = 8;
    playing.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/p.mp3"));
    MediaItem failed;
    failed.id = 9;
    failed.source = QUrl::fromLocalFile(QStringLiteral("/modeltest/f.mp3"));
    const MediaItem none;

    // Row 0 (id 0) is none of them.
    QCOMPARE(MediaListModel::rowStatusFor(0, resolving, playing, failed, false),
             MediaListModel::RowStatus::None);
    // A resolution in flight wins over the rest.
    QCOMPARE(MediaListModel::rowStatusFor(7, resolving, playing, failed, false),
             MediaListModel::RowStatus::Downloading);
    // The playing item, in the state the caller passes.
    QCOMPARE(MediaListModel::rowStatusFor(8, resolving, playing, failed, false),
             MediaListModel::RowStatus::Playing);
    QCOMPARE(MediaListModel::rowStatusFor(8, resolving, playing, failed, true),
             MediaListModel::RowStatus::Paused);
    // The last failed play.
    QCOMPARE(MediaListModel::rowStatusFor(9, resolving, playing, failed, false),
             MediaListModel::RowStatus::Error);
    // An item that is none of the three.
    QCOMPARE(MediaListModel::rowStatusFor(1, resolving, playing, failed, false),
             MediaListModel::RowStatus::None);
    // An invalid playing item matches nothing.
    QCOMPARE(MediaListModel::rowStatusFor(8, none, none, failed, false),
             MediaListModel::RowStatus::None);
}

void MediaListModelTest::setItemIdsKeepsStatusRow()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });
    model.setRowStatus(1, MediaListModel::RowStatus::Playing);

    setItems(model, { makeItem(2, "B"), makeItem(3, "C") });

    // The marker is a row, not an item: it stays on row 1 (the owning
    // widget re-derives it after a refresh).
    QCOMPARE(model.data(model.index(1, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString(QChar(0x25B6)));
}

void MediaListModelTest::mimeDataCarriesIdsRowsOrigin()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B"), makeItem(3, "C") });
    model.setOriginId(MediaListModel::playlistOrigin(7));

    QMimeData *md = model.mimeData({ model.index(2, 0), model.index(0, 3), model.index(1, 0) });
    if (md == nullptr) {
        QFAIL("mimeData() returned nullptr");
        return;
    }
    QVERIFY(md->hasFormat(MediaListModel::mediaMimeType()));
    // One id per dragged row (ascending), the rows in row order (what a
    // reorder moves), the origin of the drag.
    QCOMPARE(parseIds(*md), QList<quint64>({ 1, 2, 3 }));
    QCOMPARE(parseRows(*md), QList<int>({ 0, 1, 2 }));
    QCOMPARE(parseOrigin(*md), MediaListModel::playlistOrigin(7));
    delete md;
}

void MediaListModelTest::mimeDataSortsRowsAscending()
{
    // A selection is not guaranteed to be in row order (Ctrl+click): the
    // payload must be, so a reorder moves the rows in row order. A
    // repeated item repeats its id (one entry per row).
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });
    model.setItemIds({ 1, 2, 1 });

    QMimeData *md = model.mimeData({ model.index(2, 0), model.index(0, 0), model.index(1, 0) });
    if (md == nullptr) {
        QFAIL("mimeData() returned nullptr");
        return;
    }
    QCOMPARE(parseIds(*md), QList<quint64>({ 1, 2, 1 }));
    QCOMPARE(parseRows(*md), QList<int>({ 0, 1, 2 }));
    delete md;
}

void MediaListModelTest::dragDataFromMimeDataParsesBareIdArray()
{
    // The older payload is a bare id array: it parses with no rows and no
    // origin (an insertion with no reorder information).
    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7, 8 })).toJson());

    const MediaListModel::DragData drag = MediaListModel::dragDataFromMimeData(&md);
    QCOMPARE(drag.ids, QList<quint64>({ 7, 8 }));
    QVERIFY(drag.rows.isEmpty());
    QVERIFY(drag.origin.isEmpty());

    // A non-media mime parses to empty.
    QMimeData foreign;
    foreign.setText("plain text");
    const MediaListModel::DragData nothing = MediaListModel::dragDataFromMimeData(&foreign);
    QVERIFY(nothing.ids.isEmpty());
}

void MediaListModelTest::dragDataFromMimeDataRejectsForeignMime()
{
    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile("/tmp/a.mp3") });
    const MediaListModel::DragData drag = MediaListModel::dragDataFromMimeData(&md);
    QVERIFY(drag.ids.isEmpty());
}

void MediaListModelTest::dropMimeDataEmitsItemsDropped()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });
    model.setOriginId(MediaListModel::libraryOrigin());

    QSignalSpy spy(&model, &MediaListModel::itemsDropped);

    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7, 8 })).toJson());
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, 1, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<quint64>>(), QList<quint64>({ 7, 8 }));
    // The bare-array payload carries no rows and no origin.
    QCOMPARE(spy.at(0).at(1).value<QList<int>>(), QList<int>());
    QCOMPARE(spy.at(0).at(2).toString(), QString());
    QCOMPARE(spy.at(0).at(3).toInt(), 1);
}

void MediaListModelTest::dropOnEmptyAreaAppendsAtEnd()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });

    QSignalSpy spy(&model, &MediaListModel::itemsDropped);

    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7 })).toJson());
    // A drop on the empty area below the last row arrives with a negative
    // row: it appends at the end.
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, -1, -1, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(3).toInt(), 2);
}

void MediaListModelTest::dropRowClampsToRowCount()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });

    QSignalSpy spy(&model, &MediaListModel::itemsDropped);

    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7 })).toJson());
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, 99, 0, QModelIndex()));

    QCOMPARE(spy.at(0).at(3).toInt(), 1);
}

void MediaListModelTest::unknownMimeDropRejected()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });

    QSignalSpy spy(&model, &MediaListModel::itemsDropped);

    QMimeData md;
    md.setText("plain text");
    QVERIFY(!model.dropMimeData(&md, Qt::CopyAction, 0, 0, QModelIndex()));
    QCOMPARE(spy.count(), 0);
}

void MediaListModelTest::localFileDropEmitsFilesDropped()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });

    QSignalSpy spy(&model, &MediaListModel::filesDropped);

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile("/tmp/a.mp3"),
                 QUrl::fromLocalFile("/tmp/b.flac"), });
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, 1, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3"),
                           QUrl::fromLocalFile("/tmp/b.flac") }));
    QCOMPARE(spy.at(0).at(1).toInt(), 1);
}

void MediaListModelTest::nonLocalUrlDropEmitsUrlsDropped()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });

    QSignalSpy filesSpy(&model, &MediaListModel::filesDropped);
    QSignalSpy urlsSpy(&model, &MediaListModel::urlsDropped);

    QMimeData md;
    md.setUrls({ QUrl("https://example.com/a.mp3"),
                 QUrl("https://example.com/b.mp3"), });
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, 0, 0, QModelIndex()));

    QCOMPARE(filesSpy.count(), 0);
    QCOMPARE(urlsSpy.count(), 1);
    QCOMPARE(urlsSpy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl("https://example.com/a.mp3"),
                           QUrl("https://example.com/b.mp3") }));
    QCOMPARE(urlsSpy.at(0).at(1).toInt(), 0);
}

void MediaListModelTest::mixedFileAndUrlDropEmitsBothSignals()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });

    QSignalSpy filesSpy(&model, &MediaListModel::filesDropped);
    QSignalSpy urlsSpy(&model, &MediaListModel::urlsDropped);

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile("/tmp/a.mp3"),
                 QUrl("https://example.com/b.mp3"), });
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, 0, 0, QModelIndex()));

    QCOMPARE(filesSpy.count(), 1);
    QCOMPARE(filesSpy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3") }));
    QCOMPARE(urlsSpy.count(), 1);
    QCOMPARE(urlsSpy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl("https://example.com/b.mp3") }));
}

void MediaListModelTest::fileDropIntoEmptyModelIsAccepted()
{
    MediaListModel model;

    QSignalSpy spy(&model, &MediaListModel::filesDropped);

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile("/tmp/a.mp3") });
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, 0, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toInt(), 0);
}

void MediaListModelTest::rootIndexAcceptsDrops()
{
    MediaListModel model;
    setItems(model, { makeItem(1, "A") });

    // The root (invalid) index must accept drops so the view accepts drag
    // moves over the empty area; otherwise a real drag shows a forbidden
    // cursor and can never be dropped.
    QVERIFY(model.flags(QModelIndex()) & Qt::ItemIsDropEnabled);
    const QModelIndex cell = model.index(0, 0);
    QVERIFY(cell.flags() & Qt::ItemIsDragEnabled);
    QVERIFY(cell.flags() & Qt::ItemIsDropEnabled);
    QVERIFY(cell.flags() & Qt::ItemIsSelectable);
}

void MediaListModelTest::updateItemRepaintsRowInPlace()
{
    MediaListModel model;
    setItems(model, {
        makeItem(1, "Alpha"),
        makeItem(2, "Beta"),
        makeItem(3, "Gamma"),
    });
    model.setRowStatus(1, MediaListModel::RowStatus::Playing);

    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    // The canonical store changed the item (as a library resync does):
    // replace it in the store, then ask the model to repaint the row.
    MediaItem updated = makeItem(2, "Beta (live)");
    updated.artist = "Beta Artist";
    m_store->remove(2);
    m_store->insert(updated);
    QVERIFY(model.updateItem(updated));

    // The row repaints in place: no reset (the other rows and the playing
    // mark are untouched), dataChanged covers the whole row.
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(1, MediaListModel::Title), Qt::DisplayRole).toString(),
             QString("Beta (live)"));
    QCOMPARE(model.data(model.index(1, MediaListModel::Artist), Qt::DisplayRole).toString(),
             QString("Beta Artist"));
    QCOMPARE(model.data(model.index(1, MediaListModel::Playing), Qt::DisplayRole).toString(),
             QString(QChar(0x25B6)));
    QCOMPARE(changedSpy.count(), 1);
    const QModelIndex topLeft = changedSpy.at(0).at(0).value<QModelIndex>();
    const QModelIndex bottomRight = changedSpy.at(0).at(1).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 1);
    QCOMPARE(bottomRight.row(), 1);
    QCOMPARE(bottomRight.column(), MediaListModel::ColumnCount - 1);

    // Unknown ids are not in the model.
    QVERIFY(!model.updateItem(makeItem(99, "Nobody")));
}

void MediaListModelTest::updateItemRepaintsEveryRowOfDuplicate()
{
    // The same item in several rows (playlists may repeat items): when
    // the store's data changes (a resync), every row of the item must be
    // repainted — the view only redraws what dataChanged covers.
    MediaListModel model;
    setItems(model, {
        makeItem(1, "Alpha"),
        makeItem(2, "Beta"),
        makeItem(2, "Beta"),
        makeItem(3, "Gamma"),
    });

    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    MediaItem updated = makeItem(2, "Beta (live)");
    updated.artist = "Beta Artist";
    m_store->remove(2);
    m_store->insert(updated);
    QVERIFY(model.updateItem(updated));

    // Both rows of the duplicate are covered, not just the first.
    QCOMPARE(changedSpy.count(), 2);
    const QList<int> changedRows = {
        changedSpy.at(0).at(0).value<QModelIndex>().row(),
        changedSpy.at(1).at(0).value<QModelIndex>().row(),
    };
    QCOMPARE(changedRows, QList<int>({ 1, 2 }));

    // And the model serves the new data for both rows.
    QCOMPARE(model.data(model.index(1, MediaListModel::Title), Qt::DisplayRole).toString(),
             QString("Beta (live)"));
    QCOMPARE(model.data(model.index(2, MediaListModel::Title), Qt::DisplayRole).toString(),
             QString("Beta (live)"));
}

void MediaListModelTest::artworkColumnShowsDecorationOnly()
{
    // A unique id: the model's pixmap cache is process-wide, keyed by
    // item id (a small id would collide with a cached entry of an
    // earlier test).
    MediaItem item = makeItem(9001, "Covered");
    MediaListModel model;
    setItems(model, { item });

    // No art: no display text, no decoration (the other columns are
    // untouched).
    QCOMPARE(model.data(model.index(0, MediaListModel::Artwork),
                        Qt::DisplayRole).toString(),
             QString());
    QVERIFY(model.data(model.index(0, MediaListModel::Artwork),
                       Qt::DecorationRole).value<QPixmap>().isNull());
    QCOMPARE(model.data(model.index(0, MediaListModel::Title),
                        Qt::DisplayRole).toString(),
             QString("Covered"));

    // The art lands (an import's art): the decoration appears.
    const QColor cover(200, 30, 30);
    QVERIFY(m_store->setArtwork(item.id, "image/png", test::solidImageBytes(cover)));

    const QPixmap pixmap =
        model.data(model.index(0, MediaListModel::Artwork),
                   Qt::DecorationRole).value<QPixmap>();
    QVERIFY(!pixmap.isNull());
    QCOMPARE(pixmap.toImage().pixelColor(0, 0).rgb(), cover.rgb());

    // A replacement repaints the row in place (a dataChanged covering
    // the artwork cell) and the row serves the new pixmap.
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    const QColor replaced(30, 30, 200);
    QVERIFY(m_store->setArtwork(item.id, "image/png", test::solidImageBytes(replaced)));
    QCOMPARE(changedSpy.count(), 1);
    const QModelIndex topLeft = changedSpy.at(0).at(0).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 0);
    const QPixmap newPixmap =
        model.data(model.index(0, MediaListModel::Artwork),
                   Qt::DecorationRole).value<QPixmap>();
    QCOMPARE(newPixmap.toImage().pixelColor(0, 0).rgb(), replaced.rgb());

    // A removal clears the decoration.
    QVERIFY(m_store->setArtwork(item.id, "image/png", QByteArray()));
    QVERIFY(model.data(model.index(0, MediaListModel::Artwork),
                       Qt::DecorationRole).value<QPixmap>().isNull());
}

void MediaListModelTest::setItemIdsDropsStaleRowIndex()
{
    // The lazy id→rows index is dropped by setItemIds: an update after a
    // reset must not repaint a stale row of the old id list.
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });

    MediaItem updated = makeItem(2, "B (live)");
    m_store->remove(2);
    m_store->insert(updated);
    QVERIFY(model.updateItem(updated)); // builds the index

    setItems(model, { makeItem(1, "A") });

    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(!model.updateItem(makeItem(2, "B (live)"))); // id 2 is gone
    QCOMPARE(changedSpy.count(), 0);
}

void MediaListModelTest::dropIndexForRowNormalizes()
{
    const QModelIndex noParent;
    QCOMPARE(MediaListModel::dropIndexForRow(1, noParent, 3), 1);
    QCOMPARE(MediaListModel::dropIndexForRow(-1, noParent, 3), 3); // empty area: append
    QCOMPARE(MediaListModel::dropIndexForRow(99, noParent, 3), 3); // clamp to the end
    QCOMPARE(MediaListModel::dropIndexForRow(0, noParent, 0), 0);  // empty model
    // A drop in the middle of a row arrives with a negative row and the
    // row itself as parent: insert at that row.
    MediaListModel model;
    setItems(model, { makeItem(1, "A"), makeItem(2, "B"), makeItem(3, "C") });
    QCOMPARE(MediaListModel::dropIndexForRow(-1, model.index(1, 0), 3), 1);
}

void MediaListModelTest::searchableColumnsAreTheDataColumns()
{
    // Every data column; the status marker is model state, the artwork a
    // pixmap — neither is searchable text.
    const QList<int> columns = MediaListModel::searchableColumns();
    QCOMPARE(columns.size(), MediaListModel::Channels);
    QCOMPARE(columns.first(), MediaListModel::Title);
    QCOMPARE(columns.last(), MediaListModel::Channels);
    QVERIFY(!columns.contains(MediaListModel::Playing));
    QVERIFY(!columns.contains(MediaListModel::Artwork));
}
}

QTEST_MAIN(MediaListModelTest)
#include "medialistmodel_test.moc"
