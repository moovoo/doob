#include <QtTest/QtTest>
#include <QJsonObject>
#include <QList>

#include "layoutformat.h"

namespace {

class LayoutFormatTest : public QObject
{
    Q_OBJECT

private slots:
    void testCurrentDocumentNeedsNoMigration();
    void testMissingVersionKeyIsVersion1();
    void testExplicitVersionIsRead();
    void testPendingStepsRunInOrder();
    void testFailedStepRejectsTheDocument();
    void testUpToDateDocumentRunsNoStep();
};

void LayoutFormatTest::testCurrentDocumentNeedsNoMigration()
{
    // A document at the current version (the shipped shape) loads as-is:
    // the built-in history is empty, the version stays, nothing moves.
    QJsonObject doc;
    doc["container"] = QJsonObject{{ "type", QString("splitter") }};
    doc["version"] = kLayoutVersion;

    int from = 0;
    QString error;
    QVERIFY(migrateDocument(doc, from, error));
    QCOMPARE(from, kLayoutVersion);
    QCOMPARE(documentVersion(doc), kLayoutVersion);
    QVERIFY(doc.contains("container"));
}

void LayoutFormatTest::testMissingVersionKeyIsVersion1()
{
    // A document without the key (hand-made, not written by a save) is
    // read as version 1.
    QJsonObject doc;
    doc["container"] = QJsonObject{{ "type", QString("tabs") }};
    QCOMPARE(documentVersion(doc), 1);
}

void LayoutFormatTest::testExplicitVersionIsRead()
{
    QJsonObject doc;
    doc["version"] = 3;
    QCOMPARE(documentVersion(doc), 3);
}

// A hand-crafted older document runs the pending steps in order and
// ends at the current version (the steps are the format's history; a
// real one ships with its own test of the previous version's shape).
void LayoutFormatTest::testPendingStepsRunInOrder()
{
    // A v1 document in the (fictional) old shape: the payload key is
    // "container" and the version key absent.
    QJsonObject doc;
    doc["container"] = QJsonObject{{ "type", QString("tabs") }};
    QCOMPARE(documentVersion(doc), 1);

    // Two steps, v1 -> v2 -> v3, each recording that it ran.
    QList<LayoutMigration> migrations;
    migrations.append(LayoutMigration{2, [](QJsonObject &d, QString &e) {
        d["step2"] = true;
        return true;
    },});
    migrations.append(LayoutMigration{3, [](QJsonObject &d, QString &e) {
        if (!d.contains("step2")) {
            e = QStringLiteral("step 2 did not run");
            return false;
        }
        d["step3"] = true;
        return true;
    },});

    int from = 0;
    QString error;
    QVERIFY(migrateDocument(doc, from, error, migrations));
    QCOMPARE(from, 1);
    QVERIFY(doc.contains("step2"));
    QVERIFY(doc.contains("step3"));
    // The document ends at the current version.
    QCOMPARE(documentVersion(doc), kLayoutVersion);
}

// A failed step rejects the document: false comes back, the error names
// the failed step, and the caller discards the (partially migrated)
// document — the file on disk is never touched by the migration.
void LayoutFormatTest::testFailedStepRejectsTheDocument()
{
    QJsonObject doc;
    doc["container"] = QJsonObject{{ "type", QString("tabs") }};

    QList<LayoutMigration> migrations;
    migrations.append(LayoutMigration{2, [](QJsonObject &d, QString &e) {
        e = QStringLiteral("the old key is missing");
        return false;
    },});

    int from = 0;
    QString error;
    QVERIFY(!migrateDocument(doc, from, error, migrations));
    QCOMPARE(from, 1);
    QVERIFY(error.contains(QStringLiteral("version 2")));
    QVERIFY(error.contains(QStringLiteral("the old key is missing")));
}

// A document already at the newest version of the given history runs no
// step: the pending steps are exactly the ones above its version.
void LayoutFormatTest::testUpToDateDocumentRunsNoStep()
{
    // The (fictional) history's newest version is 2; the document is at
    // it, so nothing is pending.
    QJsonObject doc;
    doc["version"] = 2;

    QList<LayoutMigration> migrations;
    migrations.append(LayoutMigration{2, [](QJsonObject &d, QString &e) {
        d["ran"] = true;
        return true;
    },});

    int from = 0;
    QString error;
    QVERIFY(migrateDocument(doc, from, error, migrations));
    QCOMPARE(from, 2);
    QVERIFY(!doc.contains("ran"));
}

}

QTEST_MAIN(LayoutFormatTest)
#include "layoutformat_test.moc"
