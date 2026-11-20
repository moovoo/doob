#include <QtTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <memory>

#include "database.h"

namespace {

class DatabaseTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void freshDatabaseRunsAllMigrations();
    void openIsIdempotent();
    void onlyPendingMigrationsAreApplied();
    void missingVersionRecordFallsBackToZero();
    void componentsShareAFileIndependently();
    void failedMigrationRollsBackAndStops();
    void inTransactionCommitsAndRollsBack();

private:
    QStringList stepLog(const QString &path) const;
    int versionOf(const QString &path, const QString &component) const;
    QStringList tables(const QString &path) const;

    std::unique_ptr<QTemporaryDir> m_dir;
};

// v0 is the empty database; v1 creates the base tables (IF NOT EXISTS,
// as the real components' v1 steps, so a legacy file that already carries
// the table is tolerated); v2 adds one more table.
const QList<DatabaseMigration> kMigrations = {
    { 1, [](QSqlDatabase &db) {
        return Database::execStatements(db, {
            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS step_log (entry TEXT NOT NULL)"),
            QStringLiteral("CREATE TABLE IF NOT EXISTS data (id INTEGER PRIMARY KEY)"),
            QStringLiteral("INSERT INTO step_log VALUES ('v1')"),
        });
    }, },
    { 2, [](QSqlDatabase &db) {
        return Database::execStatements(db, {
            QStringLiteral("CREATE TABLE more (id INTEGER PRIMARY KEY)"),
            QStringLiteral("INSERT INTO step_log VALUES ('v2')"),
        });
    }, },
};

void DatabaseTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
}

void DatabaseTest::cleanup()
{
    m_dir.reset();
}

QStringList DatabaseTest::stepLog(const QString &path) const
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "doob_log_check");
    db.setDatabaseName(path);
    if (!db.open())
        return {};
    QSqlQuery q(db);
    QStringList rows;
    if (q.exec(QStringLiteral("SELECT entry FROM step_log ORDER BY rowid"))) {
        while (q.next())
            rows << q.value(0).toString();
    }
    db.close();
    QSqlDatabase::removeDatabase("doob_log_check");
    return rows;
}

int DatabaseTest::versionOf(const QString &path, const QString &component) const
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "doob_version_check");
    db.setDatabaseName(path);
    if (!db.open())
        return -1;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT version FROM schema_version WHERE component = ?"));
    q.bindValue(0, component);
    int version = -1;
    if (q.exec() && q.next())
        version = q.value(0).toInt();
    db.close();
    QSqlDatabase::removeDatabase("doob_version_check");
    return version;
}

QStringList DatabaseTest::tables(const QString &path) const
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "doob_tables_check");
    db.setDatabaseName(path);
    if (!db.open())
        return {};
    QSqlQuery q(db);
    QStringList names;
    if (q.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"))) {
        while (q.next())
            names << q.value(0).toString();
    }
    db.close();
    QSqlDatabase::removeDatabase("doob_tables_check");
    return names;
}

void DatabaseTest::freshDatabaseRunsAllMigrations()
{
    const QString path = m_dir->path() + "/fresh.db";
    Database db;
    QVERIFY(db.open(path, "comp", kMigrations));

    QCOMPARE(db.schemaVersion(), 2);
    QCOMPARE(versionOf(path, "comp"), 2);
    QCOMPARE(stepLog(path), QStringList({ "v1", "v2" }));
    QCOMPARE(tables(path),
             QStringList({ "data", "more", "schema_version", "step_log" }));
}

void DatabaseTest::openIsIdempotent()
{
    const QString path = m_dir->path() + "/idempotent.db";
    Database db;
    QVERIFY(db.open(path, "comp", kMigrations));

    Database again;
    QVERIFY(again.open(path, "comp", kMigrations));

    // Already at the latest version: nothing is re-applied.
    QCOMPARE(again.schemaVersion(), 2);
    QCOMPARE(stepLog(path), QStringList({ "v1", "v2" }));
}

void DatabaseTest::onlyPendingMigrationsAreApplied()
{
    const QString path = m_dir->path() + "/pending.db";
    // A database already at v1 (it was opened with only the first step).
    Database db;
    QVERIFY(db.open(path, "comp", { kMigrations.first() }));
    QCOMPARE(versionOf(path, "comp"), 1);
    QCOMPARE(stepLog(path), QStringList({ "v1" }));

    // The full history then applies only the pending step.
    Database db2;
    QVERIFY(db2.open(path, "comp", kMigrations));
    QCOMPARE(db2.schemaVersion(), 2);
    QCOMPARE(stepLog(path), QStringList({ "v1", "v2" }));
}

void DatabaseTest::missingVersionRecordFallsBackToZero()
{
    // A legacy file: the data table exists, but there is no schema_version
    // record (neither the table nor a row) — it counts as version 0.
    const QString path = m_dir->path() + "/legacy.db";
    {
        QSqlDatabase raw =
            QSqlDatabase::addDatabase("QSQLITE", "doob_legacy_craft");
        raw.setDatabaseName(path);
        QVERIFY(raw.open());
        QSqlQuery q(raw);
        QVERIFY(q.exec(QStringLiteral(
            "CREATE TABLE data (id INTEGER PRIMARY KEY)")));
        raw.close();
        QSqlDatabase::removeDatabase("doob_legacy_craft");
    }

    Database db;
    QVERIFY(db.open(path, "comp", kMigrations));

    // v1's create was a no-op for the existing table; both steps logged.
    QCOMPARE(db.schemaVersion(), 2);
    QCOMPARE(stepLog(path), QStringList({ "v1", "v2" }));
}

void DatabaseTest::componentsShareAFileIndependently()
{
    const QString path = m_dir->path() + "/shared.db";
    const QList<DatabaseMigration> aMigrations = {
        { 1, [](QSqlDatabase &db) {
            return Database::execStatements(db, {
                QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS step_log (entry TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE a_table (id INTEGER PRIMARY KEY)"),
                QStringLiteral("INSERT INTO step_log VALUES ('a1')"),
            });
        }, },
    };
    const QList<DatabaseMigration> bMigrations = {
        { 1, [](QSqlDatabase &db) {
            return Database::execStatements(db, {
                QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS step_log (entry TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE b_table (id INTEGER PRIMARY KEY)"),
                QStringLiteral("INSERT INTO step_log VALUES ('b1')"),
            });
        }, },
    };

    Database a;
    QVERIFY(a.open(path, "a", aMigrations));
    QCOMPARE(versionOf(path, "a"), 1);
    QCOMPARE(versionOf(path, "b"), -1);  // b has no record yet

    // Opening b on the same file migrates only b's schema; a's record and
    // applied steps are untouched.
    Database b;
    QVERIFY(b.open(path, "b", bMigrations));
    QCOMPARE(versionOf(path, "b"), 1);
    QCOMPARE(versionOf(path, "a"), 1);

    // Reopening a re-applies nothing of a's.
    Database a2;
    QVERIFY(a2.open(path, "a", aMigrations));
    QCOMPARE(stepLog(path), QStringList({ "a1", "b1" }));
    QCOMPARE(tables(path),
             QStringList({ "a_table", "b_table", "schema_version", "step_log" }));
}

void DatabaseTest::failedMigrationRollsBackAndStops()
{
    const QList<DatabaseMigration> kFailing = {
        { 1, [](QSqlDatabase &db) {
            return Database::execStatements(db, {
                QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS step_log (entry TEXT NOT NULL)"),
                QStringLiteral("CREATE TABLE data (id INTEGER PRIMARY KEY)"),
                QStringLiteral("INSERT INTO step_log VALUES ('v1')"),
            });
        }, },
        // v2 creates a table and then fails: the whole step must roll
        // back, including the create.
        { 2, [](QSqlDatabase &db) {
            return Database::execStatements(db, {
                QStringLiteral("CREATE TABLE doomed (id INTEGER PRIMARY KEY)"),
                QStringLiteral("INSERT INTO no_such_table VALUES (1)"),
            });
        }, },
    };

    const QString path = m_dir->path() + "/failing.db";
    Database db;
    QVERIFY(!db.open(path, "comp", kFailing));

    // Stuck at v1: the bump did not happen, v2's side effect is gone,
    // v1's work is intact.
    QCOMPARE(versionOf(path, "comp"), 1);
    QCOMPARE(stepLog(path), QStringList({ "v1" }));
    QVERIFY(!tables(path).contains("doomed"));
    QVERIFY(tables(path).contains("data"));

    // The next open retries the same step and fails the same way.
    Database db2;
    QVERIFY(!db2.open(path, "comp", kFailing));
    QCOMPARE(versionOf(path, "comp"), 1);
}

void DatabaseTest::inTransactionCommitsAndRollsBack()
{
    const QString path = m_dir->path() + "/tx.db";
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "doob_tx");
    db.setDatabaseName(path);
    QVERIFY(db.open());
    QSqlQuery setup(db);
    QVERIFY(setup.exec(QStringLiteral("CREATE TABLE t (id INTEGER)")));

    auto count = [&db] {
        QSqlQuery q(db);
        q.exec(QStringLiteral("SELECT COUNT(*) FROM t"));
        return q.next() ? q.value(0).toInt() : -1;
    };

    // A successful apply commits.
    QVERIFY(Database::inTransaction(db, [](QSqlDatabase &db) {
        QSqlQuery q(db);
        return q.exec(QStringLiteral("INSERT INTO t VALUES (1)"));
    }, QStringLiteral("test")));
    QCOMPARE(count(), 1);

    // A failed apply rolls back: the new row is gone, the old one intact.
    QVERIFY(!Database::inTransaction(db, [](QSqlDatabase &db) {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("INSERT INTO t VALUES (2)")))
            return false;
        return q.exec(QStringLiteral("INSERT INTO no_such_table VALUES (1)"));
    }, QStringLiteral("test")));
    QCOMPARE(count(), 1);

    db.close();
    QSqlDatabase::removeDatabase("doob_tx");

    // An unopened connection: the transaction cannot begin, false comes
    // back and nothing is applied.
    QSqlDatabase closed = QSqlDatabase::addDatabase("QSQLITE", "doob_tx_closed");
    closed.setDatabaseName(m_dir->path() + "/never_opened.db");
    bool applied = false;
    QVERIFY(!Database::inTransaction(
        closed, [&applied](QSqlDatabase &db) {
            Q_UNUSED(db);
            applied = true;
            return true;
        }, QStringLiteral("test")));
    QVERIFY(!applied);
    QSqlDatabase::removeDatabase("doob_tx_closed");
}

}


QTEST_MAIN(DatabaseTest)
#include "database_test.moc"
