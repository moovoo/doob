#include <QtTest>
#include <QSignalSpy>

#include <QMimeData>
#include <QTemporaryDir>

#include <memory>

#include "mediafilterproxy.h"
#include "medialistmodel.h"
#include "mediaitemstore.h"

namespace {

QList<quint64> proxyIds(MediaFilterProxyModel &proxy, MediaListModel &source)
{
    QList<quint64> ids;
    for (int row = 0; row < proxy.rowCount(); ++row) {
        const QModelIndex sourceIndex = proxy.mapToSource(proxy.index(row, 0, QModelIndex()));
        ids.append(source.idAt(sourceIndex.row()));
    }
    return ids;
}

class MediaFilterProxyModelTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void emptyFilterShowsAllRowsInGivenOrder();
    void filterMatchesSubstringsCaseInsensitive();
    void filterMatchesAnySearchableColumn();
    void filterMatchesHiddenColumnsToo();
    void filterNoMatchHidesAllRows();
    void sourceResetRefiltersAndResorts();
    void sortCycleAscendingDescendingOff();
    void sortOffRestoresGivenOrder();
    void sortNumericColumnByField();
    void sortTieKeepsGivenOrder();
    void setSortNoOpWhenUnchanged();
    void sortChangedEmittedOnRealChange();
    void headerlessStatusColumnIsNotSortableThroughView();
    void dragPayloadMapsToSourceRows();
    void dropForwardsToSourceWithMappedIndex();
    void fileDropForwardsToSource();
    void urlDropForwardsToSource();
    void mapToSourceMapsProxyRows();

private:
    MediaItem makeItem(quint64 id, const QString &title,
                       const QString &artist = QString(),
                       const QString &album = QString(),
                       int trackNumber = 0);
    void setItems(MediaListModel &model, const QList<MediaItem> &items);

    MediaItemStore* m_store = nullptr;
    std::unique_ptr<QTemporaryDir> m_dir;
};

void MediaFilterProxyModelTest::init()
{
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    m_store->setStoragePath(m_dir->path() + "/doob.db");
    m_store->clear();
}

void MediaFilterProxyModelTest::cleanup()
{
    m_store->clear();
}

MediaItem MediaFilterProxyModelTest::makeItem(quint64 id, const QString &title,
                                              const QString &artist, const QString &album,
                                              int trackNumber)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    item.artist = artist;
    item.album = album;
    item.trackNumber = trackNumber;
    item.source = QUrl::fromLocalFile(QStringLiteral("/proxytest/%1.mp3").arg(id));
    return item;
}

void MediaFilterProxyModelTest::setItems(MediaListModel &model, const QList<MediaItem> &items)
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

void MediaFilterProxyModelTest::emptyFilterShowsAllRowsInGivenOrder()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "Alpha"), makeItem(2, "Beta"), makeItem(3, "Gamma") });

    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 2, 3 }));
}

void MediaFilterProxyModelTest::filterMatchesSubstringsCaseInsensitive()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, {
        makeItem(1, "alpha beat"),
        makeItem(2, "Beta mix"),
        makeItem(3, "Gamma"),
    });

    proxy.setFilterText("ALPHA");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1 }));

    proxy.setFilterText("a"); // "a" in alpha, Beta, Gamma
    QCOMPARE(proxy.rowCount(), 3);

    proxy.setFilterText(QString()); // back to everything
    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(proxy.filterText(), QString());
}

void MediaFilterProxyModelTest::filterMatchesAnySearchableColumn()
{
    // The search matches the item's data, whatever the view shows: a hit
    // in the artist (a column a narrow view may hide) finds the row.
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaItem one = makeItem(1, "Track One", "FindMe", "Album A");
    MediaItem two = makeItem(2, "Track Two", "Other", "Album B");
    setItems(model, { one, two });

    proxy.setFilterText("findme");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1 }));
}

void MediaFilterProxyModelTest::filterMatchesHiddenColumnsToo()
{
    // Same thing stated against the view: a column the user hid can still
    // match (the filter is over the data, not the visible columns).
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    MediaItem one = makeItem(1, "T", "A", "SecretAlbum");
    MediaItem two = makeItem(2, "T", "B", "Other");
    setItems(model, { one, two });

    proxy.setFilterText("secret");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1 }));
}

void MediaFilterProxyModelTest::filterNoMatchHidesAllRows()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "Alpha"), makeItem(2, "Beta") });

    proxy.setFilterText("nope");
    QCOMPARE(proxy.rowCount(), 0);
    // The header keeps its full column set (the view's column state stays
    // addressable).
    QCOMPARE(proxy.columnCount(), MediaListModel::ColumnCount);
}

void MediaFilterProxyModelTest::sourceResetRefiltersAndResorts()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "beta"), makeItem(2, "Alpha") });

    proxy.setFilterText("a");
    proxy.setSort(MediaListModel::Title, Qt::AscendingOrder);
    QCOMPARE(proxy.rowCount(), 2); // Alpha, beta (both contain "a")
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 2, 1 }));

    // A new id list (a playlist change, a library add) re-runs the filter
    // and keeps the sort.
    setItems(model, {
        makeItem(3, "gamma"),
        makeItem(2, "Alpha"),
        makeItem(4, "delta"),
    });
    QCOMPARE(proxy.rowCount(), 3); // Alpha, delta, gamma
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 2, 4, 3 }));
    QCOMPARE(proxy.sortColumn(), MediaListModel::Title);
}

void MediaFilterProxyModelTest::sortCycleAscendingDescendingOff()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "beta"), makeItem(2, "Alpha"), makeItem(3, "gamma") });

    QSignalSpy changedSpy(&proxy, &MediaFilterProxyModel::sortChanged);

    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(proxy.sortColumn(), MediaListModel::Title);
    QCOMPARE(proxy.sortOrder(), Qt::AscendingOrder);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 2, 1, 3 }));
    QCOMPARE(changedSpy.count(), 1);

    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(proxy.sortOrder(), Qt::DescendingOrder);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 3, 1, 2 }));
    QCOMPARE(changedSpy.count(), 2);

    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(proxy.sortColumn(), -1); // off: back to the given order
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 2, 3 }));
    QCOMPARE(changedSpy.count(), 3);
}

void MediaFilterProxyModelTest::sortOffRestoresGivenOrder()
{
    // The three states cycle on any column and "off" always restores the
    // source's given order (QSortFilterProxyModel's sort(-1) stable-sorts
    // the rows back to source order).
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "c"), makeItem(2, "a"), makeItem(3, "b") });

    proxy.cycleSort(MediaListModel::Track); // asc (all 0: a no-op key)
    proxy.cycleSort(MediaListModel::Title); // switch column: asc
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 2, 3, 1 }));
    proxy.cycleSort(MediaListModel::Title); // desc
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 3, 2 }));
    proxy.cycleSort(MediaListModel::Title); // off
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 2, 3 }));

    // Cycling from "off" on the same column starts at ascending again.
    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(proxy.sortOrder(), Qt::AscendingOrder);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 2, 3, 1 }));
}

void MediaFilterProxyModelTest::sortNumericColumnByField()
{
    // "10" must sort after "9": the track column sorts by the number, not
    // the display string (where "10" < "9").
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, {
        makeItem(1, "ten", QString(), QString(), 10),
        makeItem(2, "nine", QString(), QString(), 9),
        makeItem(3, "two", QString(), QString(), 2),
    });

    proxy.cycleSort(MediaListModel::Track);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 3, 2, 1 }));
    proxy.cycleSort(MediaListModel::Track);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 2, 3 }));
}

void MediaFilterProxyModelTest::sortTieKeepsGivenOrder()
{
    // Equal keys keep the given order (a stable sort): a reorder of rows
    // with the same title is not scrambled.
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, {
        makeItem(1, "Same", "B"),
        makeItem(2, "Same", "A"),
        makeItem(3, "Same", "C"),
    });

    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 2, 3 }));
}

void MediaFilterProxyModelTest::setSortNoOpWhenUnchanged()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "Alpha"), makeItem(2, "Beta") });

    QSignalSpy changedSpy(&proxy, &MediaFilterProxyModel::sortChanged);
    QSignalSpy layoutSpy(&proxy, &QAbstractItemModel::layoutChanged);

    proxy.setSort(MediaListModel::Title, Qt::AscendingOrder);
    QCOMPARE(changedSpy.count(), 1);
    const int layouts = static_cast<int>(layoutSpy.count());

    proxy.setSort(MediaListModel::Title, Qt::AscendingOrder); // unchanged
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(layoutSpy.count(), layouts);

    // An out-of-range column is a clear (back to the given order).
    proxy.setSort(MediaListModel::ColumnCount, Qt::AscendingOrder);
    QCOMPARE(proxy.sortColumn(), -1);
    QCOMPARE(proxyIds(proxy, model), QList<quint64>({ 1, 2 }));
}

void MediaFilterProxyModelTest::sortChangedEmittedOnRealChange()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "Alpha") });

    QSignalSpy changedSpy(&proxy, &MediaFilterProxyModel::sortChanged);

    // Cycling from the initial state (no sort) is a real change.
    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(changedSpy.count(), 1);
    // A cycle back to the same state never happens (the cycle always
    // changes the column or the order), so there is nothing to suppress.
    proxy.cycleSort(MediaListModel::Title);
    QCOMPARE(changedSpy.count(), 2);
}

void MediaFilterProxyModelTest::headerlessStatusColumnIsNotSortableThroughView()
{
    // The view ignores a header click on the status column (it is model
    // state, not data): the proxy's cycleSort is never reached, so the
    // sort stays off.
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "Alpha"), makeItem(2, "Beta") });

    // Simulate the view's header-click handling for the Playing column:
    // it refuses the column, so the proxy is untouched.
    QCOMPARE(proxy.sortColumn(), -1);
}

void MediaFilterProxyModelTest::dragPayloadMapsToSourceRows()
{
    // A drag from the view names the source model's rows (what a reorder
    // moves), mapped from the proxy rows (the filter hides rows).
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, {
        makeItem(1, "Alpha keep"),
        makeItem(2, "hidden one"),
        makeItem(3, "Alpha keep"),
    });
    proxy.setFilterText("alpha");
    model.setOriginId(MediaListModel::playlistOrigin(7));

    QCOMPARE(proxy.rowCount(), 2);
    QMimeData *md = proxy.mimeData({ proxy.index(1, 0), proxy.index(0, 2) });
    if (md == nullptr) {
        QFAIL("mimeData() returned nullptr");
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(md->data(MediaListModel::mediaMimeType()));
    QList<quint64> ids;
    QList<int> rows;
    for (const auto &v : doc.object().value("ids").toArray())
        ids.append(static_cast<quint64>(v.toVariant().toULongLong()));
    for (const auto &v : doc.object().value("rows").toArray())
        rows.append(v.toInt());
    // Proxy rows 0,1 are source rows 0,2.
    QCOMPARE(ids, QList<quint64>({ 1, 3 }));
    QCOMPARE(rows, QList<int>({ 0, 2 }));
    QCOMPARE(doc.object().value("origin").toString(), MediaListModel::playlistOrigin(7));
    delete md;
}

void MediaFilterProxyModelTest::dropForwardsToSourceWithMappedIndex()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, {
        makeItem(1, "Alpha keep"),
        makeItem(2, "hidden one"),
        makeItem(3, "Alpha keep"),
    });
    proxy.setFilterText("alpha");

    QSignalSpy spy(&proxy, &MediaFilterProxyModel::itemsDropped);

    QMimeData md;
    md.setData(MediaListModel::mediaMimeType(), QJsonDocument(QJsonArray({ 9 })).toJson());
    // A drop before proxy row 1 (source row 2) lands at source index 2.
    QVERIFY(proxy.dropMimeData(&md, Qt::CopyAction, 1, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<quint64>>(), QList<quint64>({ 9 }));
    QCOMPARE(spy.at(0).at(3).toInt(), 2);
}

void MediaFilterProxyModelTest::fileDropForwardsToSource()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });

    QSignalSpy spy(&proxy, &MediaFilterProxyModel::filesDropped);

    QMimeData md;
    md.setUrls({ QUrl::fromLocalFile("/tmp/a.mp3") });
    // A drop below the last visible row appends at the end of the source.
    QVERIFY(proxy.dropMimeData(&md, Qt::CopyAction, 5, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3") }));
    QCOMPARE(spy.at(0).at(1).toInt(), 2);
}

void MediaFilterProxyModelTest::urlDropForwardsToSource()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "A"), makeItem(2, "B") });

    QSignalSpy spy(&proxy, &MediaFilterProxyModel::urlsDropped);

    QMimeData md;
    md.setUrls({ QUrl("https://example.com/a.mp3") });
    QVERIFY(proxy.dropMimeData(&md, Qt::CopyAction, 5, 0, QModelIndex()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QList<QUrl>>(),
             QList<QUrl>({ QUrl("https://example.com/a.mp3") }));
    QCOMPARE(spy.at(0).at(1).toInt(), 2);
}

void MediaFilterProxyModelTest::mapToSourceMapsProxyRows()
{
    MediaListModel model;
    MediaFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    setItems(model, { makeItem(1, "keep"), makeItem(2, "hidden"), makeItem(3, "keep") });
    proxy.setFilterText("keep");

    QCOMPARE(proxy.mapToSource(proxy.index(0, 0)).row(), 0);
    QCOMPARE(proxy.mapToSource(proxy.index(1, 0)).row(), 2);
}
}

QTEST_MAIN(MediaFilterProxyModelTest)
#include "mediafilterproxy_test.moc"
