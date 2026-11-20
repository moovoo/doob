#include <QtTest>

#include "listexpander.h"
#include "sourcemanager.h"
#include "trackmetadata.h"

namespace {

using ListEntries = SourceProvider::ListEntries;
using ListVideo = SourceProvider::ListVideo;

// A provider that claims the URLs it is told to and answers a fetchList
// with the configured entries for the configured list URLs (synchronously
// — the contract only requires the callback on the GUI thread).
class ListProvider : public SourceProvider
{
    Q_OBJECT
public:
    explicit ListProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("listtest"); }
    QString title() const override { return QStringLiteral("List Test"); }
    bool accepts(const QUrl &url) const override
    {
        return m_urls.contains(url.toString());
    }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        Q_UNUSED(url);
        cb(TrackMeta());
    }
    void fetchList(const QUrl &url, const ListCallback &cb) override
    {
        const auto it = m_lists.constFind(url.toString());
        cb(it == m_lists.constEnd() ? ListEntries() : it.value());
    }

    QStringList m_urls;
    QHash<QString, ListEntries> m_lists;
};

class ListExpanderTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void discoverReturnsOnlyListUrls();
    void discoverWithoutClaimantsIsEmpty();
    void discoverReportsEachClaimedUrl();
    void applyReplacesListWithCheckedVideos();
    void applyUncheckedListContributesNothing();
    void applyUncheckedPlainUrlIsDropped();
    void applyDedupsAgainstEarlierEntries();
    void applyKeepsListOrder();
    void applyCarriesVideoMetadataInLists();

private:
    ListProvider m_provider{this};
};

void ListExpanderTest::init()
{
    SourceManager::getInstance()->clear();
    m_provider.m_urls.clear();
    m_provider.m_lists.clear();
}

void ListExpanderTest::cleanup()
{
    SourceManager::getInstance()->clear();
}

void ListExpanderTest::discoverReturnsOnlyListUrls()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl plainUrl("https://music.example.com/watch?v=2");
    m_provider.m_urls = { listUrl.toString(), plainUrl.toString() };
    ListEntries list;
    list.title = "The List";
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    ListVideo(QUrl("https://music.example.com/watch?v=3")), };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    QHash<QString, SourceProvider::ListEntries> found;
    bool done = false;
    ListExpander expander;
    // The local file has no claiming provider and never appears.
    expander.discover({ listUrl, plainUrl,
                        QUrl::fromLocalFile("/nonexistent.mp3"), },
                      [](const QUrl &, const ListEntries &) {},
                      [&found, &done](const auto &lists) {
                          found = lists;
                          done = true;
                      });
    QVERIFY(done);
    QCOMPARE(found.size(), 1);
    QVERIFY(found.contains(listUrl.toString()));
    QCOMPARE(found.value(listUrl.toString()).title, QString("The List"));
    QCOMPARE(found.value(listUrl.toString()).videos.size(), 2);
}

void ListExpanderTest::discoverWithoutClaimantsIsEmpty()
{
    QHash<QString, SourceProvider::ListEntries> found;
    bool done = false;
    ListExpander expander;
    expander.discover({ QUrl("https://example.com/audio/stream.mp3") },
                      [](const QUrl &, const ListEntries &) {},
                      [&found, &done](const auto &lists) {
                          found = lists;
                          done = true;
                      });
    QVERIFY(done);
    QVERIFY(found.isEmpty());
}

void ListExpanderTest::discoverReportsEachClaimedUrl()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl plainUrl("https://music.example.com/watch?v=2");
    m_provider.m_urls = { listUrl.toString(), plainUrl.toString() };
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")) };
    m_provider.m_lists[listUrl.toString()] = list;
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    // every claimed url is reported as it answers — with empty entries
    // when it references no collection (the wizard starts the plain
    // source's metadata fetch from there).
    QList<QUrl> reported;
    QHash<QString, bool> nonEmpty;
    ListExpander expander;
    expander.discover({ listUrl, plainUrl },
                      [&reported, &nonEmpty](const QUrl &url,
                                             const ListEntries &entries) {
                          reported.append(url);
                          nonEmpty[url.toString()] = !entries.isEmpty();
                      },
                      [](const auto &) {});
    QCOMPARE(reported.size(), 2);
    QCOMPARE(nonEmpty.value(listUrl.toString()), true);
    QCOMPARE(nonEmpty.value(plainUrl.toString()), false);
}

void ListExpanderTest::applyReplacesListWithCheckedVideos()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    const QUrl otherUrl("https://music.example.com/watch?v=9");
    QHash<QString, ListEntries> lists;
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    ListVideo(QUrl("https://music.example.com/watch?v=3")), };
    lists[listUrl.toString()] = list;

    const QList<QUrl> out = ListExpander::apply(
        { listUrl, otherUrl }, lists,
        { "https://music.example.com/watch?v=1",
          "https://music.example.com/watch?v=3",
          otherUrl.toString(), });
    QCOMPARE(out, QList<QUrl>({ QUrl("https://music.example.com/watch?v=1"),
                                QUrl("https://music.example.com/watch?v=3"),
                                otherUrl }));
}

void ListExpanderTest::applyUncheckedListContributesNothing()
{
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    QHash<QString, ListEntries> lists;
    ListEntries list;
    list.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")),
                    ListVideo(QUrl("https://music.example.com/watch?v=3")), };
    lists[listUrl.toString()] = list;

    // Nothing checked: the list link is not a source of its own, so the
    // batch imports nothing.
    const QList<QUrl> out = ListExpander::apply({ listUrl }, lists, {});
    QVERIFY(out.isEmpty());
}

void ListExpanderTest::applyUncheckedPlainUrlIsDropped()
{
    const QUrl plain("https://music.example.com/watch?v=9");
    const QList<QUrl> out =
        ListExpander::apply({ plain }, {}, {});
    QVERIFY(out.isEmpty());
    const QList<QUrl> outChecked =
        ListExpander::apply({ plain }, {}, { plain.toString() });
    QCOMPARE(outChecked, QList<QUrl>({ plain }));
}

void ListExpanderTest::applyDedupsAgainstEarlierEntries()
{
    const QUrl v1("https://music.example.com/watch?v=1");
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    QHash<QString, ListEntries> lists;
    ListEntries list;
    list.videos = { ListVideo(v1), ListVideo(QUrl("https://music.example.com/watch?v=3")) };
    lists[listUrl.toString()] = list;

    // v1 is carried by the batch already (first position wins).
    const QList<QUrl> out =
        ListExpander::apply({ v1, listUrl }, lists,
                            { v1.toString(),
                              "https://music.example.com/watch?v=1",
                              "https://music.example.com/watch?v=3", });
    QCOMPARE(out, QList<QUrl>({ v1, QUrl("https://music.example.com/watch?v=3") }));
}

void ListExpanderTest::applyKeepsListOrder()
{
    const QUrl listA("https://music.example.com/watch?v=1&list=A");
    const QUrl listB("https://music.example.com/watch?v=2&list=B");
    QHash<QString, ListEntries> lists;
    ListEntries a;
    a.videos = { ListVideo(QUrl("https://music.example.com/watch?v=1")),
                 ListVideo(QUrl("https://music.example.com/watch?v=5")), };
    lists[listA.toString()] = a;
    ListEntries b;
    b.videos = { ListVideo(QUrl("https://music.example.com/watch?v=2")),
                 ListVideo(QUrl("https://music.example.com/watch?v=6")), };
    lists[listB.toString()] = b;

    const QList<QUrl> out = ListExpander::apply(
        { listA, listB }, lists,
        { "https://music.example.com/watch?v=1",
          "https://music.example.com/watch?v=5",
          "https://music.example.com/watch?v=2",
          "https://music.example.com/watch?v=6", });
    QCOMPARE(out, QList<QUrl>({ QUrl("https://music.example.com/watch?v=1"),
                                QUrl("https://music.example.com/watch?v=5"),
                                QUrl("https://music.example.com/watch?v=2"),
                                QUrl("https://music.example.com/watch?v=6") }));
}

void ListExpanderTest::applyCarriesVideoMetadataInLists()
{
    // The review's rows read the display metadata from the list's own
    // listing: the apply keeps the urls, the metadata rides in the
    // entries (the wizard's review builds from them).
    const QUrl listUrl("https://music.example.com/watch?v=1&list=L1");
    QHash<QString, ListEntries> lists;
    ListEntries list;
    ListVideo video(QUrl("https://music.example.com/watch?v=1"));
    video.title = "Track One";
    video.artist = "The Band";
    video.durationMs = 133000;
    list.videos = { video };
    lists[listUrl.toString()] = list;

    const QList<QUrl> out = ListExpander::apply(
        { listUrl }, lists, { video.url.toString() });
    QCOMPARE(out, QList<QUrl>({ video.url }));
    QCOMPARE(lists.value(listUrl.toString()).videos.first().title,
             QString("Track One"));
}

} // namespace

QTEST_MAIN(ListExpanderTest)
#include "listexpander_test.moc"
