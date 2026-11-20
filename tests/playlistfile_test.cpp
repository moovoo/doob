#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "playlistfile.h"

namespace {

class PlaylistFileTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void extensionsAreM3uAndPLS();
    void isPlaylistFileMatchesExtensions();
    void parsesM3U();
    void m3uIgnoresCommentsAndExtinf();
    void m3uResolvesRelativePaths();
    void m3uKeepsAbsoluteAndUrlEntries();
    void m3uCrlfIsTolerated();
    void parsesPLS();
    void plsUsesTitleAsName();
    void plsEntriesInNumericOrder();
    void missingFileFails();
    void emptyFileFails();
    void commentsOnlyFails();
    void writeThenParseRoundtrip();
    void writeFailureOnBadPath();
    void flattenExpandsPlaylistFilesInPlace();
    void flattenDropsUnparseableFiles();
    void resolveSourceArgumentCoversPastedSources();
    void resolveSourceArgumentKeepsMissingEntriesAsPaths();

private:
    QString write(const QString &name, const QString &content) const;

    std::unique_ptr<QTemporaryDir> m_dir;
};

void PlaylistFileTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
}

void PlaylistFileTest::cleanup()
{
    m_dir.reset();
}

QString PlaylistFileTest::write(const QString &name, const QString &content) const
{
    const QString dir = m_dir->path();
    QFile f(dir + "/" + name);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("write: cannot open %s", qPrintable(f.fileName()));
        return {};
    }
    f.write(content.toUtf8());
    return f.fileName();
}

void PlaylistFileTest::extensionsAreM3uAndPLS()
{
    QCOMPARE(playlistFileExtensions(),
             QStringList({ "m3u", "m3u8", "pls" }));
}

void PlaylistFileTest::isPlaylistFileMatchesExtensions()
{
    const QString a = m_dir->path() + "/a.m3u";
    const QString b = m_dir->path() + "/b.M3U8";
    const QString c = m_dir->path() + "/c.pls";
    const QString d = m_dir->path() + "/d.mp3";
    for (const QString &path : { a, b, c })
        QVERIFY(isPlaylistFile(QUrl::fromLocalFile(path)));
    QVERIFY(!isPlaylistFile(QUrl::fromLocalFile(d)));
    QVERIFY(!isPlaylistFile(QUrl("https://example.com/a.m3u")));
}

void PlaylistFileTest::parsesM3U()
{
    const QString path = write("road.m3u",
                               "#EXTM3U\n"
                               "/tmp/a.mp3\n"
                               "/tmp/b.flac\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.name, QString("road"));
    QCOMPARE(pl.entries,
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3"),
                           QUrl::fromLocalFile("/tmp/b.flac") }));
}

void PlaylistFileTest::m3uIgnoresCommentsAndExtinf()
{
    const QString path = write("c.m3u",
                               "#EXTM3U\n"
                               "#EXTINF:123,Song Title\n"
                               "# a comment\n"
                               "/tmp/a.mp3\n"
                               "\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.entries, QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3") }));
}

void PlaylistFileTest::m3uResolvesRelativePaths()
{
    // A relative entry resolves against the playlist file's directory, not
    // the process's working directory.
    const QString dir = m_dir->path() + "/music";
    QVERIFY(QDir().mkpath(dir));
    const QString path = dir + "/road.m3u";
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("sub/a.mp3\nb.mp3\n");
    }
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.entries,
             QList<QUrl>({ QUrl::fromLocalFile(dir + "/sub/a.mp3"),
                           QUrl::fromLocalFile(dir + "/b.mp3") }));
}

void PlaylistFileTest::m3uKeepsAbsoluteAndUrlEntries()
{
    const QString path = write("u.m3u",
                               "/tmp/a.mp3\n"
                               "https://stream.example.com/s.m3u8\n"
                               "file:///tmp/b.mp3\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.entries,
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3"),
                           QUrl("https://stream.example.com/s.m3u8"),
                           QUrl("file:///tmp/b.mp3") }));
}

void PlaylistFileTest::m3uCrlfIsTolerated()
{
    const QString path = write("crlf.m3u", "#EXTM3U\r\n/tmp/a.mp3\r\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.entries, QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3") }));
}

void PlaylistFileTest::parsesPLS()
{
    const QString path = write("road.pls",
                               "[playlist]\n"
                               "File1=/tmp/a.mp3\n"
                               "Title1=A\n"
                               "Length1=100\n"
                               "File2=relative/b.mp3\n"
                               "Title2=B\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.name, QString("road"));
    QCOMPARE(pl.entries,
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/a.mp3"),
                           QUrl::fromLocalFile(m_dir->path() + "/relative/b.mp3") }));
}

void PlaylistFileTest::plsUsesTitleAsName()
{
    const QString path = write("untitled.pls",
                               "[playlist]\n"
                               "Title=My Mix\n"
                               "File1=/tmp/a.mp3\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.name, QString("My Mix"));
}

void PlaylistFileTest::plsEntriesInNumericOrder()
{
    // Numeric, not lexicographic: File2 comes before File10.
    const QString path = write("order.pls",
                               "[playlist]\n"
                               "File1=/tmp/one.mp3\n"
                               "File10=/tmp/ten.mp3\n"
                               "File2=/tmp/two.mp3\n");
    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(path, pl));
    QCOMPARE(pl.entries,
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/one.mp3"),
                           QUrl::fromLocalFile("/tmp/two.mp3"),
                           QUrl::fromLocalFile("/tmp/ten.mp3") }));
}

void PlaylistFileTest::missingFileFails()
{
    PlaylistFile pl;
    pl.name = "untouched";
    QVERIFY(!parsePlaylistFile(m_dir->path() + "/nope.m3u", pl));
    QCOMPARE(pl.name, QString("untouched"));
    QVERIFY(pl.entries.isEmpty());
}

void PlaylistFileTest::emptyFileFails()
{
    const QString path = write("empty.m3u", "");
    PlaylistFile pl;
    QVERIFY(!parsePlaylistFile(path, pl));
}

void PlaylistFileTest::commentsOnlyFails()
{
    const QString path = write("comments.m3u", "#EXTM3U\n# nothing else\n");
    PlaylistFile pl;
    QVERIFY(!parsePlaylistFile(path, pl));
}

void PlaylistFileTest::writeThenParseRoundtrip()
{
    const QString out = m_dir->path() + "/round.m3u";
    const QList<QUrl> entries = {
        QUrl::fromLocalFile("/tmp/a.mp3"),
        QUrl::fromLocalFile("/tmp/b.flac"),
        QUrl("https://stream.example.com/s.m3u8"),
    };
    QVERIFY(writePlaylistFile(out, entries));

    PlaylistFile pl;
    QVERIFY(parsePlaylistFile(out, pl));
    QCOMPARE(pl.name, QString("round"));
    QCOMPARE(pl.entries, entries);
}

void PlaylistFileTest::writeFailureOnBadPath()
{
    QVERIFY(!writePlaylistFile(m_dir->path() + "/no/such/dir/out.m3u",
                               { QUrl::fromLocalFile("/tmp/a.mp3") }));
}

void PlaylistFileTest::flattenExpandsPlaylistFilesInPlace()
{
    const QString pl = write("mix.m3u", "/tmp/a.mp3\n/tmp/b.mp3\n");
    const QList<QUrl> drop = { QUrl::fromLocalFile("/tmp/x.mp3"),
                               QUrl::fromLocalFile(pl),
                               QUrl::fromLocalFile("/tmp/y.mp3"), };

    QCOMPARE(flattenDropFiles(drop),
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/x.mp3"),
                           QUrl::fromLocalFile("/tmp/a.mp3"),
                           QUrl::fromLocalFile("/tmp/b.mp3"),
                           QUrl::fromLocalFile("/tmp/y.mp3") }));
}

void PlaylistFileTest::flattenDropsUnparseableFiles()
{
    const QString empty = write("empty.m3u", "#EXTM3U\n");
    const QList<QUrl> drop = { QUrl::fromLocalFile(empty),
                               QUrl::fromLocalFile("/tmp/x.mp3"), };

    QCOMPARE(flattenDropFiles(drop),
             QList<QUrl>({ QUrl::fromLocalFile("/tmp/x.mp3") }));
}

void PlaylistFileTest::resolveSourceArgumentCoversPastedSources()
{
    const QString base = m_dir->path();
    // A pasted URL (one carrying "://") is kept as pasted.
    const QUrl stream = QUrl(QStringLiteral("https://example.com/s.m3u8"));
    QCOMPARE(resolveSourceArgument(base, stream.toString()), stream);
    // An existing path (absolute, or relative to baseDir) is a file.
    const QString file = base + "/track.flac";
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QCOMPARE(resolveSourceArgument(base, file), QUrl::fromLocalFile(file));
    QCOMPARE(resolveSourceArgument(base, QStringLiteral("track.flac")),
             QUrl::fromLocalFile(file));
    // A missing path with the user-input fallback takes the host-like
    // resolution (http is assumed).
    QCOMPARE(resolveSourceArgument(base, QStringLiteral("example.com/stream.mp3")),
             QUrl(QStringLiteral("http://example.com/stream.mp3")));
}

void PlaylistFileTest::resolveSourceArgumentKeepsMissingEntriesAsPaths()
{
    const QString base = m_dir->path();
    // A missing relative entry with the local-file fallback stays a
    // broken file reference — fromUserInput would guess an http:// URL
    // for it.
    QCOMPARE(resolveSourceArgument(base, QStringLiteral("missing/track.mp3"),
                                   SourceFallback::LocalFile),
             QUrl::fromLocalFile(base + "/missing/track.mp3"));
    // A pasted URL and an existing path are unaffected by the fallback.
    const QUrl stream = QUrl(QStringLiteral("https://example.com/s.m3u8"));
    QCOMPARE(resolveSourceArgument(base, stream.toString(),
                                   SourceFallback::LocalFile), stream);
    const QString file = base + "/track.flac";
    {
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    QCOMPARE(resolveSourceArgument(base, QStringLiteral("track.flac"),
                                   SourceFallback::LocalFile),
             QUrl::fromLocalFile(file));
}

}

QTEST_MAIN(PlaylistFileTest)
#include "playlistfile_test.moc"
