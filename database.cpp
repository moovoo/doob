#include "database.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

namespace {
int s_connectionCounter = 0;
}

Database::Database()
    : m_connectionName(QStringLiteral("doob_db_%1").arg(++s_connectionCounter))
{
}

Database::~Database()
{
    close();
}

bool Database::open(const QString &path, const QString &component,
                    const QList<DatabaseMigration> &migrations)
{
    close();
    QDir().mkpath(QFileInfo(path).absolutePath());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning("Database: cannot open %s: %s",
                 qPrintable(path), qPrintable(m_db.lastError().text()));
        close();
        return false;
    }

    // The version is recorded per component (one row each), so several
    // components may share one file. A missing table or row is version 0.
    const QStringList schemaVersionStatements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "component TEXT NOT NULL PRIMARY KEY, "
            "version INTEGER NOT NULL)"),
    };
    if (!execStatements(m_db, schemaVersionStatements)) {
        close();
        return false;
    }

    int version = 0;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT version FROM schema_version WHERE component = ?"));
        q.bindValue(0, component);
        if (q.exec() && q.next())
            version = q.value(0).toInt();
    }

    // The steps are applied one by one in version order, each in its own
    // transaction together with the version bump: a failed step rolls back
    // completely (the database stays at the previous version) and is
    // retried on the next open.
    for (const DatabaseMigration &migration : migrations) {
        if (migration.version <= version)
            continue;

        if (!m_db.transaction()) {
            qWarning("Database: cannot begin migration to v%d on %s",
                     migration.version, qPrintable(path));
            close();
            return false;
        }

        bool ok = migration.apply(m_db);
        if (ok) {
            QSqlQuery bump(m_db);
            bump.prepare(QStringLiteral(
                "INSERT INTO schema_version (component, version) VALUES (?, ?) "
                "ON CONFLICT (component) "
                "DO UPDATE SET version = excluded.version"));
            bump.bindValue(0, component);
            bump.bindValue(1, migration.version);
            ok = bump.exec();
        }
        if (ok && !m_db.commit())
            ok = false;
        if (!ok) {
            m_db.rollback();
            qWarning("Database: migration to v%d failed on %s",
                     migration.version, qPrintable(path));
            close();
            return false;
        }
        version = migration.version;
    }

    m_schemaVersion = version;
    return true;
}

bool Database::execStatements(QSqlDatabase &db, const QStringList &statements)
{
    for (const QString &statement : statements) {
        QSqlQuery q(db);
        if (!q.exec(statement)) {
            qWarning("Database: statement failed: %s",
                     qPrintable(q.lastError().text()));
            return false;
        }
    }
    return true;
}

bool Database::inTransaction(QSqlDatabase db,
                             const std::function<bool(QSqlDatabase &)> &apply,
                             const QString &what)
{
    if (!db.transaction()) {
        qWarning("%s: cannot begin transaction: %s",
                 qPrintable(what), qPrintable(db.lastError().text()));
        return false;
    }
    const bool ok = apply(db);
    if (!ok) {
        db.rollback();
        return false;  // the lambda reported what failed
    }
    if (!db.commit()) {
        qWarning("%s: cannot commit transaction: %s",
                 qPrintable(what), qPrintable(db.lastError().text()));
        db.rollback();
        return false;
    }
    return true;
}

QString Database::nonNull(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

QStringList Database::tableColumns(const QSqlDatabase &db, const QString &table)
{
    QStringList columns;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA table_info(") + table + ')')) {
        while (q.next())
            columns << q.value(1).toString();
    }
    return columns;
}

void Database::close()
{
    if (!m_db.isValid())
        return;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
    m_schemaVersion = 0;
}

bool Database::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

int Database::schemaVersion() const
{
    return m_schemaVersion;
}
