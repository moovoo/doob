#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "jsonfile.h"

namespace {

class JsonFileTest : public QObject
{
    Q_OBJECT

private slots:
    void writesAndReplaces();
    void failureLeavesTargetUntouched();
};

void JsonFileTest::writesAndReplaces()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/doc.json";

    QVERIFY(atomicWrite(path, QByteArray("{\"a\":1}"), "test"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("{\"a\":1}"));
    f.close();
    // The temp file is gone after a successful write.
    QVERIFY(!QFile::exists(path + ".tmp"));

    // An existing file (a previous session's) is replaced, not refused.
    QVERIFY(atomicWrite(path, QByteArray("{\"a\":2}"), "test"));
    f.open(QIODevice::ReadOnly);
    QCOMPARE(f.readAll(), QByteArray("{\"a\":2}"));
    f.close();
}

void JsonFileTest::failureLeavesTargetUntouched()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A path whose parent is a file: the directory cannot be created.
    const QString blocker = dir.path() + "/blocker";
    {
        QFile b(blocker);
        QVERIFY(b.open(QIODevice::WriteOnly));
        b.write("x");
    }

    const QString pre = dir.path() + "/pre.json";
    QVERIFY(atomicWrite(pre, QByteArray("{\"keep\":true}"), "test"));
    const QString path = blocker + "/doc.json";
    QVERIFY(!atomicWrite(path, QByteArray("{\"a\":1}"), "test"));

    // The earlier file survives the failed write.
    QFile f(pre);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("{\"keep\":true}"));
    f.close();
    QVERIFY(!QFile::exists(path + ".tmp"));
}

}  // namespace

QTEST_MAIN(JsonFileTest)
#include "jsonfile_test.moc"
