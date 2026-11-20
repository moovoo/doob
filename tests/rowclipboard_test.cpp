#include <QtTest>

#include "rowclipboard.h"

namespace {

RowClipboard* clip() { return RowClipboard::getInstance(); }

class RowClipboardTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsEmpty();
    void copyStoresTheBatch();
    void duplicatesArePreservedInRowOrder();
    void copyReplacesThePreviousBatch();
    void clearEmptiesIt();
};

void RowClipboardTest::init()
{
    RowClipboard::reset();
}

void RowClipboardTest::cleanup()
{
    RowClipboard::reset();
}

void RowClipboardTest::startsEmpty()
{
    QVERIFY(clip()->isEmpty());
    QVERIFY(clip()->items().isEmpty());
}

void RowClipboardTest::copyStoresTheBatch()
{
    clip()->copy({ 7, 9 });
    QVERIFY(!clip()->isEmpty());
    QCOMPARE(clip()->items(), QList<quint64>({ 7, 9 }));
}

void RowClipboardTest::duplicatesArePreservedInRowOrder()
{
    // A playlist may hold the same item in several rows: a row copy keeps
    // every copy, in row order.
    clip()->copy({ 4, 4, 9, 4 });
    QCOMPARE(clip()->items(), QList<quint64>({ 4, 4, 9, 4 }));
}

void RowClipboardTest::copyReplacesThePreviousBatch()
{
    clip()->copy({ 1, 2, 3 });
    clip()->copy({ 8 });
    QCOMPARE(clip()->items(), QList<quint64>({ 8 }));
}

void RowClipboardTest::clearEmptiesIt()
{
    clip()->copy({ 1, 2 });
    clip()->clear();
    QVERIFY(clip()->isEmpty());
}
}

QTEST_MAIN(RowClipboardTest)
#include "rowclipboard_test.moc"
