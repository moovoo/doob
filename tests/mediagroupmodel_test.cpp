#include <QtTest>
#include <QSignalSpy>

#include <QMimeData>
#include <QTemporaryDir>

#include <memory>

#include "mediagroupmodel.h"
#include "medialistmodel.h"
#include "mediaitemstore.h"

namespace {

class MediaGroupModelTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void byArtistTreeStructure();
    void byAlbumTreeStructure();
    void byArtistAlbumTupleTree();
    void tupleNameJoinsParts();
    void groupRowShowsNameAndCount();
    void trackRowShowsNumberAndTitle();
    void groupsSortedByNameCaseInsensitive();
    void unknownGroupSortsLast();
    void tracksUseNaturalOrder();
    void filterKeepsMatchingTracksOnly();
    void filterPrunesEmptyGroups();
    void filterMatchesAnySearchableColumn();
    void idAtIdsUnderAndCountUnder();
    void rowTextOfGroupAndTrackRows();
    void mimeDataExpandsGroupToItsTracks();
    void mimeDataTrackDragNamesItsOwnId();
    void dropForwardsToSource();
    void urlDropForwardsToSource();
    void groupRowIsNotADropTarget();
    void mapToSourceAndMapFromSource();
    void sourceResetRebuildsTree();
    void sourceDataChangedRebuildsTree();
    void setGroupingRebuildsTree();
    void setFilterTextNoOpWhenUnchanged();
    void emptySourceIsEmptyTree();

private:
    MediaItem makeItem(quint64 id, const QString &title,
                       const QString &artist = QString(),
                       const QString &album = QString(),
                       int trackNumber = 0, int discNumber = 0);
    // Registers the items in the store and sets them on the source.
    void setItems(MediaListModel &source, const QList<MediaItem> &items);
    // The ids of every track beneath index, in tree order.
    QList<quint64> allTracks(MediaGroupModel &model);

    MediaItemStore* m_store = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void MediaGroupModelTest::init()
{
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    m_store->setStoragePath(m_dir->path() + "/doob.db");
    m_store->clear();
}

void MediaGroupModelTest::cleanup()
{
    m_store->clear();
}

MediaItem MediaGroupModelTest::makeItem(quint64 id, const QString &title,
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
    item.source = QUrl::fromLocalFile(QStringLiteral("/grouptest/%1.mp3").arg(id));
    return item;
}

void MediaGroupModelTest::setItems(MediaListModel &source, const QList<MediaItem> &items)
{
    for (MediaItem item : items) {
        m_store->remove(item.id);
        QVERIFY(m_store->insert(item));
    }
    QList<quint64> ids;
    for (const MediaItem &item : items)
        ids.append(item.id);
    source.setItemIds(ids);
}

QList<quint64> MediaGroupModelTest::allTracks(MediaGroupModel &model)
{
    QList<quint64> ids;
    for (int row = 0; row < model.rowCount(); ++row)
        ids.append(model.idsUnder(model.index(row, 0)));
    return ids;
}

void MediaGroupModelTest::byArtistTreeStructure()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Bee", "Album 2", 1),
        makeItem(2, "s2", "Ade", "Album 1", 2),
        makeItem(3, "s3", "Ade", "Album 1", 1),
        makeItem(4, "s4", "Bee", "Album 2", 2),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    // Two artists, one album between each, four tracks.
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowCount(model.index(0, 0)), 1); // Ade has one album
    QCOMPARE(model.rowCount(model.index(1, 0)), 1); // Bee has one album
    QCOMPARE(model.rowCount(model.index(0, 0, model.index(0, 0))), 2);
    QCOMPARE(model.columnCount(), 1);
    QCOMPARE(model.columnCount(model.index(0, 0)), 1);

    // The parents chain back to the root.
    const QModelIndex artist = model.index(0, 0);
    const QModelIndex album = model.index(0, 0, artist);
    const QModelIndex track = model.index(1, 0, album);
    QCOMPARE(model.parent(album), artist);
    QCOMPARE(model.parent(track), album);
    QVERIFY(!model.parent(artist).isValid());
    QVERIFY(model.hasChildren(artist));
    QVERIFY(model.hasChildren(album));
    QVERIFY(!model.hasChildren(track));
    QCOMPARE(model.rowCount(track), 0);

    // All four tracks are present, in tree order (the albums' natural
    // track order under the name-sorted artists).
    QCOMPARE(allTracks(model), QList<quint64>({ 3, 2, 1, 4 }));
}

void MediaGroupModelTest::byAlbumTreeStructure()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Bee", "Album 2", 1),
        makeItem(2, "s2", "Ade", "Album 1", 2),
        makeItem(3, "s3", "Ade", "Album 1", 1),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);

    // One level: the albums (the artists do not appear as a level).
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowCount(model.index(0, 0)), 2); // "Album 1"
    QCOMPARE(model.rowCount(model.index(1, 0)), 1); // "Album 2"
    // A track of the second album: parent is the album row.
    const QModelIndex track = model.index(0, 0, model.index(1, 0));
    QCOMPARE(model.parent(track), model.index(1, 0));
    QCOMPARE(model.idAt(track), 1ULL);
}

void MediaGroupModelTest::byArtistAlbumTupleTree()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al 1", 1),
        makeItem(2, "s2", "Ade", "Al 2", 1),
        makeItem(3, "s3", "Ade", "Al 1", 2), // same tuple as item 1
        makeItem(4, "s4", "Bee", "Al 1", 1), // same album, other artist
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtistAlbum);

    // The (artist, album) pairs, one level: "Ade — Al 1", "Ade — Al 2",
    // "Bee — Al 1" — the album alone does not merge across artists.
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.rowCount(model.index(0, 0)), 2); // Ade — Al 1
    QCOMPARE(model.rowCount(model.index(1, 0)), 1); // Ade — Al 2
    QCOMPARE(model.rowCount(model.index(2, 0)), 1); // Bee — Al 1
    QCOMPARE(model.rowText(model.index(0, 0)), QStringLiteral("Ade — Al 1 (2)"));
    QCOMPARE(model.rowText(model.index(2, 0)), QStringLiteral("Bee — Al 1 (1)"));
    QCOMPARE(allTracks(model), QList<quint64>({ 1, 3, 2, 4 }));
}

void MediaGroupModelTest::tupleNameJoinsParts()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al"),
        makeItem(2, "s2", QString(), "Al"),
        makeItem(3, "s3", "Bee", QString()),
        makeItem(4, "s4"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtistAlbum);

    // The pairs: "Ade — Al", "(Unknown) — Al", "Bee — (Unknown)", and the
    // fully untagged pair is a single "(Unknown)". An unknown part sorts
    // last, like in the artist view: the named pairs first, then the
    // "(Unknown)"-led ones (in name order among themselves — the bare
    // "(Unknown)" is a prefix of "(Unknown) — Al").
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.rowText(model.index(0, 0)), QStringLiteral("Ade — Al (1)"));
    QCOMPARE(model.rowText(model.index(1, 0)), QStringLiteral("Bee — (Unknown) (1)"));
    QCOMPARE(model.rowText(model.index(2, 0)), QStringLiteral("(Unknown) (1)"));
    QCOMPARE(model.rowText(model.index(3, 0)), QStringLiteral("(Unknown) — Al (1)"));
}

void MediaGroupModelTest::groupRowShowsNameAndCount()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al", 1),
        makeItem(2, "s2", "Ade", "Al", 2),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    const QModelIndex artist = model.index(0, 0);
    QCOMPARE(model.rowText(artist), QString("Ade (2)"));
    QCOMPARE(model.countUnder(artist), 2);
    QVERIFY(model.isGroupRow(artist));
    QCOMPARE(model.idAt(artist), 0ULL);
    // The group row carries no item id (UserRole).
    QCOMPARE(model.data(artist, Qt::UserRole).toULongLong(), 0ULL);
}

void MediaGroupModelTest::trackRowShowsNumberAndTitle()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "One", "Ade", "Al", 3),
        makeItem(2, "Two", "Ade", "Al", 0), // untagged: no number prefix
    });
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);

    const QModelIndex album = model.index(0, 0);
    // The natural order puts the untagged track (number 0) first.
    QCOMPARE(model.rowText(model.index(0, 0, album)), QString("Two"));
    QCOMPARE(model.rowText(model.index(1, 0, album)), QString("3. One"));
    QVERIFY(!model.isGroupRow(model.index(0, 0, album)));
    QCOMPARE(model.idAt(model.index(0, 0, album)), 2ULL);
    QCOMPARE(model.idAt(model.index(1, 0, album)), 1ULL);
}

void MediaGroupModelTest::groupsSortedByNameCaseInsensitive()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "zeta", "a"),
        makeItem(2, "s2", "Alpha", "b"),
        makeItem(3, "s3", "beta", "c"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Alpha (1)"));
    QCOMPARE(model.rowText(model.index(1, 0)), QString("beta (1)"));
    QCOMPARE(model.rowText(model.index(2, 0)), QString("zeta (1)"));
}

void MediaGroupModelTest::unknownGroupSortsLast()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", QString(), "Al"),
        makeItem(2, "s2", "Bee", "Al"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Bee (1)"));
    QCOMPARE(model.rowText(model.index(1, 0)), QStringLiteral("(Unknown) (1)"));

    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);
    QCOMPARE(model.rowCount(), 1); // one album for both
}

void MediaGroupModelTest::tracksUseNaturalOrder()
{
    // The tracks keep the natural album order: disc, track number, title —
    // not the given (ingestion) order.
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "b", "A", "Al", 2, 1),
        makeItem(2, "c", "A", "Al", 1, 2), // second disc
        makeItem(3, "a", "A", "Al", 1, 1),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);

    const QModelIndex album = model.index(0, 0);
    QCOMPARE(model.rowCount(album), 3);
    QCOMPARE(model.idAt(model.index(0, 0, album)), 3ULL); // 1.1 "a"
    QCOMPARE(model.idAt(model.index(1, 0, album)), 1ULL); // 1.2 "b"
    QCOMPARE(model.idAt(model.index(2, 0, album)), 2ULL); // 2.1 "c"
}

void MediaGroupModelTest::filterKeepsMatchingTracksOnly()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "alpha one", "Ade", "Al"),
        makeItem(2, "alpha two", "Ade", "Al"),
        makeItem(3, "beta", "Ade", "Al"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    model.setFilterText("ALPHA");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Ade (2)")); // the count follows the filter
    // Under ByArtist the artist's children are its albums: one album, two
    // matching tracks under it.
    QCOMPARE(model.rowCount(model.index(0, 0)), 1);
    QCOMPARE(model.rowCount(model.index(0, 0, model.index(0, 0))), 2);
    QCOMPARE(allTracks(model), QList<quint64>({ 1, 2 }));

    model.setFilterText(QString());
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Ade (3)"));
}

void MediaGroupModelTest::filterPrunesEmptyGroups()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "keep", "Ade", "Al"),
        makeItem(2, "gone", "Bee", "Al"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    model.setFilterText("keep");
    QCOMPARE(model.rowCount(), 1); // Bee has no matching track: gone
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Ade (1)"));
}

void MediaGroupModelTest::filterMatchesAnySearchableColumn()
{
    // A hit in a tag the row label does not show (the artist, when grouped
    // by album) finds the track.
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "One", "FindMe", "Al"),
        makeItem(2, "Two", "Other", "Al"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);

    model.setFilterText("findme");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(allTracks(model), QList<quint64>({ 1 }));
}

void MediaGroupModelTest::idAtIdsUnderAndCountUnder()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al 1", 1),
        makeItem(2, "s2", "Ade", "Al 1", 2),
        makeItem(3, "s3", "Ade", "Al 2", 1),
        makeItem(4, "s4", "Bee", "Al 3", 1),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    const QModelIndex artist = model.index(0, 0); // Ade
    const QModelIndex album = model.index(0, 0, artist); // Al 1

    // A track row names itself.
    QCOMPARE(model.idAt(model.index(0, 0, album)), 1ULL);
    QCOMPARE(model.idsUnder(model.index(0, 0, album)), QList<quint64>({ 1 }));

    // An album row names its tracks (tree order).
    QCOMPARE(model.idsUnder(album), QList<quint64>({ 1, 2 }));
    QCOMPARE(model.countUnder(album), 2);

    // An artist row names every track beneath it.
    QCOMPARE(model.idsUnder(artist), QList<quint64>({ 1, 2, 3 }));
    QCOMPARE(model.countUnder(artist), 3);

    // An invalid index names nothing.
    QVERIFY(model.idsUnder(QModelIndex()).isEmpty());
    QCOMPARE(model.countUnder(QModelIndex()), 0);
}

void MediaGroupModelTest::rowTextOfGroupAndTrackRows()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al", 1) });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    const QModelIndex artist = model.index(0, 0);
    const QModelIndex album = model.index(0, 0, artist);
    QCOMPARE(model.rowText(artist), QString("Ade (1)"));
    QCOMPARE(model.rowText(album), QString("Al (1)"));
    QCOMPARE(model.rowText(model.index(0, 0, album)), QString("1. s1"));
    QVERIFY(model.rowText(QModelIndex()).isEmpty());
}

void MediaGroupModelTest::mimeDataExpandsGroupToItsTracks()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al 1", 1),
        makeItem(2, "s2", "Ade", "Al 1", 2),
        makeItem(3, "s3", "Ade", "Al 2", 1),
        makeItem(4, "s4", "Bee", "Al 3", 1),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);
    source.setOriginId(MediaListModel::libraryOrigin());

    // Dragging an artist row means the artist's whole content.
    QMimeData *md = model.mimeData({ model.index(0, 0) });
    if (md == nullptr) {
        QFAIL("mimeData() returned nullptr");
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(md->data(MediaListModel::mediaMimeType()));
    QList<quint64> ids;
    for (const auto &v : doc.object().value("ids").toArray())
        ids.append(static_cast<quint64>(v.toVariant().toULongLong()));
    QCOMPARE(ids, QList<quint64>({ 1, 2, 3 })); // tree order
    // A group drag is an insertion wherever it lands: no rows to reorder.
    QVERIFY(doc.object().value("rows").toArray().isEmpty());
    QCOMPARE(doc.object().value("origin").toString(), MediaListModel::libraryOrigin());
    delete md;

    // Dragging an album row means the album's tracks.
    QMimeData *albumMd = model.mimeData({ model.index(1, 0, model.index(0, 0)) });
    QVERIFY(albumMd != nullptr);
    const QJsonDocument albumDoc =
        QJsonDocument::fromJson(albumMd->data(MediaListModel::mediaMimeType()));
    QList<quint64> albumIds;
    for (const auto &v : albumDoc.object().value("ids").toArray())
        albumIds.append(static_cast<quint64>(v.toVariant().toULongLong()));
    QCOMPARE(albumIds, QList<quint64>({ 3 })); // "Al 2"
    delete albumMd;
}

void MediaGroupModelTest::mimeDataTrackDragNamesItsOwnId()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al", 1) });
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);
    source.setOriginId(MediaListModel::playlistOrigin(9));

    QMimeData *md = model.mimeData({ model.index(0, 0, model.index(0, 0)) });
    QVERIFY(md != nullptr);
    const QJsonDocument doc = QJsonDocument::fromJson(md->data(MediaListModel::mediaMimeType()));
    QCOMPARE(doc.object().value("ids").toArray().size(), 1);
    QCOMPARE(doc.object().value("origin").toString(), MediaListModel::playlistOrigin(9));
    delete md;
}

void MediaGroupModelTest::dropForwardsToSource()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al") });

    QSignalSpy spy(&model, &MediaGroupModel::itemsDropped);

    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 7, 8 })).toJson());
    // The tree has no meaningful drop position: the source is told to
    // append at the end.
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, -1, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<quint64>>(), QList<quint64>({ 7, 8 }));
    QCOMPARE(spy.at(0).at(3).toInt(), 1);
}

void MediaGroupModelTest::urlDropForwardsToSource()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al") });

    QSignalSpy spy(&model, &MediaGroupModel::urlsDropped);

    QMimeData md;
    md.setUrls({ QUrl("https://example.com/a.mp3") });
    QVERIFY(model.dropMimeData(&md, Qt::CopyAction, -1, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl("https://example.com/a.mp3") }));
    QCOMPARE(spy.at(0).at(1).toInt(), 1); // the tree appends at the end
}

void MediaGroupModelTest::groupRowIsNotADropTarget()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al") });
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);

    const QModelIndex album = model.index(0, 0);
    QVERIFY(album.flags() & Qt::ItemIsSelectable);
    QVERIFY(album.flags() & Qt::ItemIsDragEnabled);
    QVERIFY(!(album.flags() & Qt::ItemIsDropEnabled));

    const QModelIndex track = model.index(0, 0, album);
    QVERIFY(track.flags() & Qt::ItemIsDropEnabled);

    // The root still accepts drops (the empty area).
    QVERIFY(model.flags(QModelIndex()) & Qt::ItemIsDropEnabled);
}

void MediaGroupModelTest::mapToSourceAndMapFromSource()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al", 1),
        makeItem(2, "s2", "Ade", "Al", 2),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    const QModelIndex artist = model.index(0, 0);
    const QModelIndex album = model.index(0, 0, artist);
    const QModelIndex track = model.index(1, 0, album); // id 2

    // A track maps to its own source row; a group to its first track's.
    QCOMPARE(model.mapToSource(track).row(), 1);
    QCOMPARE(model.mapToSource(album).row(), 0);
    QCOMPARE(model.mapToSource(artist).row(), 0);

    // And back: a source row maps to the track's row in the tree.
    QCOMPARE(model.mapFromSource(source.index(1, 0)), track);
    QCOMPARE(model.mapFromSource(source.index(0, 0)),
             model.index(0, 0, album));
}

void MediaGroupModelTest::sourceResetRebuildsTree()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al") });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);
    QCOMPARE(model.rowCount(), 1);

    // A new id list (a library add) rebuilds the tree.
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al"),
        makeItem(2, "s2", "Bee", "Al"),
    });
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Ade (1)"));
    QCOMPARE(model.rowText(model.index(1, 0)), QString("Bee (1)"));
}

void MediaGroupModelTest::sourceDataChangedRebuildsTree()
{
    // A resync re-tagged an item: the group structure (names, counts) is
    // tag-derived, so the tree rebuilds from the data change.
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al"),
        makeItem(2, "s2", "Ade", "Al"),
    });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);
    QCOMPARE(model.rowText(model.index(0, 0)), QString("Ade (2)"));

    MediaItem updated = makeItem(2, "s2", "Bee", "Al");
    m_store->remove(2);
    m_store->insert(updated);
    QVERIFY(source.updateItem(updated));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowText(model.index(1, 0)), QString("Bee (1)"));
}

void MediaGroupModelTest::setGroupingRebuildsTree()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, {
        makeItem(1, "s1", "Ade", "Al 1"),
        makeItem(2, "s2", "Ade", "Al 2"),
    });

    model.setGrouping(MediaGroupModel::Grouping::ByArtist);
    QCOMPARE(model.rowCount(), 1); // one artist
    model.setGrouping(MediaGroupModel::Grouping::ByAlbum);
    QCOMPARE(model.rowCount(), 2); // two albums
    model.setGrouping(MediaGroupModel::Grouping::ByArtistAlbum);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowText(model.index(0, 0)), QStringLiteral("Ade — Al 1 (1)"));
}

void MediaGroupModelTest::setFilterTextNoOpWhenUnchanged()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    setItems(source, { makeItem(1, "s1", "Ade", "Al") });
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    model.setFilterText("a");
    QCOMPARE(resetSpy.count(), 1);
    model.setFilterText("a"); // unchanged: no reset
    QCOMPARE(resetSpy.count(), 1);
}

void MediaGroupModelTest::emptySourceIsEmptyTree()
{
    MediaListModel source;
    MediaGroupModel model;
    model.setSourceModel(&source);
    model.setGrouping(MediaGroupModel::Grouping::ByArtist);

    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.hasChildren(QModelIndex()));
    // An empty drag is well-formed (an empty id list).
    QMimeData *md = model.mimeData({});
    QVERIFY(md != nullptr);
    const QJsonDocument doc = QJsonDocument::fromJson(md->data(MediaListModel::mediaMimeType()));
    QVERIFY(doc.object().value("ids").toArray().isEmpty());
    delete md;
}
}

QTEST_MAIN(MediaGroupModelTest)
#include "mediagroupmodel_test.moc"
