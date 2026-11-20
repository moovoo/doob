#include <QtTest/QtTest>

#include <QUrl>

#include "metatransform.h"

namespace {

// The engine is pure: the tests build items by hand and drive
// MetaTransform::evaluate / variablesFor directly.
class MetaTransformTest : public QObject
{
    Q_OBJECT

private slots:
    void macroOnly();
    void literalQuoted();
    void literalBare();
    void literalWithOps();
    void bareLiteralWithParensIsAnError();
    void unterminatedQuotedLiteralIsAnError();
    void unknownMacroIsAnError();
    void emptyExpressionIsAnError();
    void matchGroups();
    void matchNoMatchYieldsEmpty();
    void matchInvalidRegexIsAnError();
    void matchOutofRangeGroupYieldsEmpty();
    void replaceRegexWithBackrefs();
    void replaceLiteral();
    void splitParts();
    void leftRightMid();
    void trimAndStrip();
    void caseOps();
    void pad();
    void orFallback();
    void pipelineStripsNumberAndExtension();
    void pipelineChainsOnCurrent();
    void unknownOpIsAnError();
    void arityMismatchesAreErrors();
    void badIntegersAreErrors();
    void quotedArgsMayContainSeparators();
    void macrosAreCaseInsensitive();
    void fileVariablesOnlyForLocalItems();
    void metadataVariables();
    void numericFieldsEmptyWhenZero();
    void docsListMacrosAndOps();

private:
    static QHash<QString, QString> varsFor(const MediaItem &item,
                                           const QString &current = QString())
    {
        return MetaTransform::variablesFor(item, current);
    }

    static MediaItem localItem(const QString &path, const QString &title = "T")
    {
        MediaItem item;
        item.id = 1;
        item.kind = MediaItem::Local;
        item.source = QUrl::fromLocalFile(path);
        item.title = title;
        item.artist = "Artist";
        item.album = "Album";
        item.albumArtist = "Album Artist";
        item.composer = "Composer";
        item.genre = "Genre";
        item.comment = "Comment";
        item.trackNumber = 3;
        item.trackTotal = 12;
        item.discNumber = 1;
        item.discTotal = 2;
        item.year = 1999;
        return item;
    }
};

void MetaTransformTest::macroOnly()
{
    const TransformResult r =
        MetaTransform::evaluate("%title%", { { "title", "hello" } });
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString("hello"));
}

void MetaTransformTest::literalQuoted()
{
    // A quoted literal needs no variables at all.
    const TransformResult r = MetaTransform::evaluate("\"My album\"", {});
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString("My album"));
}

void MetaTransformTest::literalBare()
{
    const TransformResult r = MetaTransform::evaluate("My album", {});
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString("My album"));
}

void MetaTransformTest::literalWithOps()
{
    QCOMPARE(MetaTransform::evaluate("\"my album\" upper()", {}).value,
             QString("MY ALBUM"));
    QCOMPARE(MetaTransform::evaluate("\" a b \" trim()", {}).value,
             QString("a b"));
}

void MetaTransformTest::bareLiteralWithParensIsAnError()
{
    // A ( would name an op: the value must be quoted.
    const TransformResult r = MetaTransform::evaluate("My album (2006)", {});
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("quoted"));
}

void MetaTransformTest::unterminatedQuotedLiteralIsAnError()
{
    const TransformResult r = MetaTransform::evaluate("\"My album", {});
    QVERIFY(!r.ok);
}

void MetaTransformTest::unknownMacroIsAnError()
{
    const TransformResult r =
        MetaTransform::evaluate("%nope%", { { "title", "hello" } });
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("nope"));
}

void MetaTransformTest::emptyExpressionIsAnError()
{
    const TransformResult r = MetaTransform::evaluate("", {});
    QVERIFY(!r.ok);
}

void MetaTransformTest::matchGroups()
{
    const QHash<QString, QString> vars = { { "filename", "01 - title.mp3" } };
    QCOMPARE(
        MetaTransform::evaluate(
            "%filename% match(\"^(\\\\d+) - (.+)$\", 1)", vars)
            .value,
        QString("01"));
    QCOMPARE(
        MetaTransform::evaluate(
            "%filename% match(\"^(\\\\d+) - (.+)$\", 2)", vars)
            .value,
        QString("title.mp3"));
    // Group 0 is the whole match.
    QCOMPARE(
        MetaTransform::evaluate("%filename% match(\"01\")", vars).value,
        QString("01"));
}

void MetaTransformTest::matchNoMatchYieldsEmpty()
{
    const QHash<QString, QString> vars = { { "title", "hello" } };
    const TransformResult r =
        MetaTransform::evaluate("%title% match(\"xyz\")", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString());
}

void MetaTransformTest::matchInvalidRegexIsAnError()
{
    const TransformResult r =
        MetaTransform::evaluate("%title% match(\"([\")",
                                { { "title", "a" } });
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("regex"));
}

void MetaTransformTest::matchOutofRangeGroupYieldsEmpty()
{
    const QHash<QString, QString> vars = { { "title", "abc" } };
    const TransformResult r =
        MetaTransform::evaluate("%title% match(\"a\", 3)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString());
}

void MetaTransformTest::replaceRegexWithBackrefs()
{
    const QHash<QString, QString> vars = { { "title", "a   b" } };
    QCOMPARE(
        MetaTransform::evaluate("%title% replaceRegex(\"\\\\s+\", \" \")", vars)
            .value,
        QString("a b"));
    // \1 names the first capture group.
    QCOMPARE(
        MetaTransform::evaluate(
            "%title% replaceRegex(\"(\\\\w) \", \"[\\\\1] \")",
            { { "title", "a b" } })
            .value,
        QString("[a] b"));
}

void MetaTransformTest::replaceLiteral()
{
    const QHash<QString, QString> vars = { { "title", "a-b-c" } };
    QCOMPARE(MetaTransform::evaluate("%title% replace(\"-\", \"\")", vars).value,
             QString("abc"));
    // The argument is literal: regex metacharacters do not work here.
    QCOMPARE(
        MetaTransform::evaluate("%title% replace(\".\", \"x\")",
                                { { "title", "a.b" } })
            .value,
        QString("axb"));
}

void MetaTransformTest::splitParts()
{
    const QHash<QString, QString> vars = { { "filename", "01 - title.mp3" } };
    QCOMPARE(MetaTransform::evaluate("%filename% split(\" - \", 1)", vars).value,
             QString("01"));
    QCOMPARE(MetaTransform::evaluate("%filename% split(\" - \", 2)", vars).value,
             QString("title.mp3"));
    // Negative parts count from the end.
    QCOMPARE(MetaTransform::evaluate("%filename% split(\" - \", -1)", vars).value,
             QString("title.mp3"));
    // Out of range yields an empty string.
    const TransformResult r =
        MetaTransform::evaluate("%filename% split(\" - \", 3)", vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString());
    // A zero part is an error.
    const TransformResult bad =
        MetaTransform::evaluate("%filename% split(\" - \", 0)", vars);
    QVERIFY(!bad.ok);
}

void MetaTransformTest::leftRightMid()
{
    const QHash<QString, QString> vars = { { "title", "abcdef" } };
    QCOMPARE(MetaTransform::evaluate("%title% left(3)", vars).value,
             QString("abc"));
    QCOMPARE(MetaTransform::evaluate("%title% right(2)", vars).value,
             QString("ef"));
    QCOMPARE(MetaTransform::evaluate("%title% mid(2, 3)", vars).value,
             QString("bcd"));
    // No length takes the rest; a start below 1 clamps to the first char.
    QCOMPARE(MetaTransform::evaluate("%title% mid(3)", vars).value,
             QString("cdef"));
    QCOMPARE(MetaTransform::evaluate("%title% mid(0, 2)", vars).value,
             QString("ab"));
}

void MetaTransformTest::trimAndStrip()
{
    const QHash<QString, QString> vars = { { "title", "  a b  " } };
    QCOMPARE(MetaTransform::evaluate("%title% trim()", vars).value,
             QString("a b"));
    QCOMPARE(MetaTransform::evaluate("%title% strip(\" x\")",
                                     { { "title", "xxabx x" } })
                 .value,
             QString("ab"));
}

void MetaTransformTest::caseOps()
{
    const QHash<QString, QString> vars = { { "title", "hello world" } };
    QCOMPARE(MetaTransform::evaluate("%title% upper()", vars).value,
             QString("HELLO WORLD"));
    QCOMPARE(MetaTransform::evaluate("%title% lower()",
                                     { { "title", "Hello World" } })
                 .value,
             QString("hello world"));
    QCOMPARE(MetaTransform::evaluate("%title% titlecase()", vars).value,
             QString("Hello World"));
}

void MetaTransformTest::pad()
{
    const QHash<QString, QString> vars = { { "track", "1" } };
    QCOMPARE(MetaTransform::evaluate("%track% pad(2)", vars).value,
             QString("01"));
    // Never truncates.
    QCOMPARE(MetaTransform::evaluate("%track% pad(1)",
                                     { { "track", "123" } })
                 .value,
             QString("123"));
    QCOMPARE(MetaTransform::evaluate("%track% pad(3, \"#\")", vars).value,
             QString("##1"));
}

void MetaTransformTest::orFallback()
{
    const QHash<QString, QString> empty = { { "title", "" } };
    QCOMPARE(MetaTransform::evaluate("%title% or(\"Untitled\")", empty).value,
             QString("Untitled"));
    const QHash<QString, QString> filled = { { "title", "Song" } };
    QCOMPARE(MetaTransform::evaluate("%title% or(\"Untitled\")", filled).value,
             QString("Song"));
}

void MetaTransformTest::pipelineStripsNumberAndExtension()
{
    // The spec case: "01 - title.mp3" -> "title".
    const QHash<QString, QString> vars = { { "filename", "01 - title.mp3" } };
    const TransformResult r = MetaTransform::evaluate(
        R"(%filename% match("^(\\d+) - (.+)$", 2) match("^(.*)\\.[^.]+$", 1))",
        vars);
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString("title"));
}

void MetaTransformTest::pipelineChainsOnCurrent()
{
    // A transform can build on the cell's present value.
    const QHash<QString, QString> vars = { { "current", "a-b" }, { "title", "x" } };
    QCOMPARE(MetaTransform::evaluate("%current% replace(\"-\", \" \") trim()", vars)
                 .value,
             QString("a b"));
}

void MetaTransformTest::unknownOpIsAnError()
{
    const TransformResult r =
        MetaTransform::evaluate("%title% foo()", { { "title", "a" } });
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("foo"));
}

void MetaTransformTest::arityMismatchesAreErrors()
{
    const QHash<QString, QString> vars = { { "title", "a b" } };
    QVERIFY(!MetaTransform::evaluate("%title% replace(\"a\")", vars).ok);
    QVERIFY(!MetaTransform::evaluate("%title% trim(\"x\")", vars).ok);
    QVERIFY(!MetaTransform::evaluate("%title% split(\"a\", 1, 2)", vars).ok);
}

void MetaTransformTest::badIntegersAreErrors()
{
    const QHash<QString, QString> vars = { { "title", "abcdef" } };
    const TransformResult r = MetaTransform::evaluate("%title% left(x)", vars);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("not a number"));
}

void MetaTransformTest::quotedArgsMayContainSeparators()
{
    const QHash<QString, QString> vars = { { "title", "a, b" } };
    QCOMPARE(MetaTransform::evaluate("%title% replace(\", \", \" and \")", vars)
                 .value,
             QString("a and b"));
    // Escapes inside quoted strings.
    QCOMPARE(
        MetaTransform::evaluate("%title% replace(\"\\\\n\", \" \")",
                                { { "title", "a\\nb" } })
            .value,
        QString("a b"));
}

void MetaTransformTest::macrosAreCaseInsensitive()
{
    const TransformResult r =
        MetaTransform::evaluate("%TITLE%", { { "title", "hello" } });
    QVERIFY(r.ok);
    QCOMPARE(r.value, QString("hello"));
}

void MetaTransformTest::fileVariablesOnlyForLocalItems()
{
    const MediaItem local = localItem("/home/u/Music/01 - Title.mp3");
    const QHash<QString, QString> vars = varsFor(local);
    QCOMPARE(vars.value("filename"), QString("01 - Title.mp3"));
    QCOMPARE(vars.value("stem"), QString("01 - Title"));
    QCOMPARE(vars.value("ext"), QString("mp3"));
    QCOMPARE(vars.value("dir"), QString("Music"));
    QCOMPARE(vars.value("path"), QString("/home/u/Music/01 - Title.mp3"));

    MediaItem remote = local;
    remote.source = QUrl("https://example.com/stream.m3u8");
    const QHash<QString, QString> remoteVars = varsFor(remote);
    QVERIFY(!remoteVars.contains("filename"));
    QVERIFY(!remoteVars.contains("path"));
    // The metadata fields are present either way.
    QCOMPARE(remoteVars.value("title"), local.title);
}

void MetaTransformTest::metadataVariables()
{
    const MediaItem item = localItem("/home/u/a.mp3");
    const QHash<QString, QString> vars = varsFor(item, "edited");
    QCOMPARE(vars.value("title"), item.title);
    QCOMPARE(vars.value("artist"), item.artist);
    QCOMPARE(vars.value("album"), item.album);
    QCOMPARE(vars.value("albumartist"), item.albumArtist);
    QCOMPARE(vars.value("composer"), item.composer);
    QCOMPARE(vars.value("genre"), item.genre);
    QCOMPARE(vars.value("comment"), item.comment);
    QCOMPARE(vars.value("current"), QString("edited"));
}

void MetaTransformTest::numericFieldsEmptyWhenZero()
{
    MediaItem item = localItem("/home/u/a.mp3");
    item.trackNumber = 0;
    item.year = 0;
    const QHash<QString, QString> vars = varsFor(item);
    QCOMPARE(vars.value("track"), QString());
    QCOMPARE(vars.value("year"), QString());
    QCOMPARE(vars.value("tracktotal"), QString("12"));
}

void MetaTransformTest::docsListMacrosAndOps()
{
    // The Help table is built from these: the macros carry their %
    // wrappers, and the UTF-8 text (the em dashes) survives the
    // char* -> QString conversion.
    const QList<QPair<QString, QString>> macros = MetaTransform::macroDocs();
    QCOMPARE(macros.size(), 18);
    QCOMPARE(macros.at(0).first, QString("%filename%"));
    QCOMPARE(macros.at(0).second,
             QString("the file name with its extension (local items only)"));
    QCOMPARE(macros.at(17).first, QString("%current%"));

    const QList<QPair<QString, QString>> ops = MetaTransform::opDocs();
    QCOMPARE(ops.size(), 14);
    QCOMPARE(ops.at(0).first, QString("match(pattern, [group=0])"));
    QCOMPARE(ops.at(0).second,
             QString("the capture group of the first regex match (0 = the whole match); "
                     "no match gives an empty string"));
}

} // namespace

QTEST_MAIN(MetaTransformTest)
#include "metatransform_test.moc"
