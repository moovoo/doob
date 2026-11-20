#ifndef DATABASE_H
#define DATABASE_H

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <functional>

// One step of a component's schema history: the diff that takes the
// schema from version-1 to version (v0 is the empty database). Lists are
// in version order.
struct DatabaseMigration
{
    int version = 0;
    // Applies the diff on the open connection (inside the step's own
    // transaction); returns false when a statement fails, so the step is
    // rolled back and retried on the next open.
    std::function<bool(QSqlDatabase &)> apply;
};

// A single named SQLite (QSQLITE) connection to one file. Owns the
// connection lifecycle and the schema version of the component that opened
// it.
//
// Schema evolution: open() takes the component name and the ordered list
// of its migrations. The recorded version lives in the schema_version
// table — one row per component, so several components may safely share
// one file (doob.db hosts the library, the playlists and the standalone
// items). A missing table or row is version 0. Every pending step (a
// version above the recorded one) is applied one by one, each in its own
// transaction together with the version bump, so a failed step rolls back
// completely and the database stays at a consistent previous version.
class Database
{
public:
    Database();
    ~Database();

    // Opens the connection at path and migrates the component's schema to
    // the latest version in migrations. Returns false (and leaves the
    // connection closed) if it cannot open or a step fails.
    bool open(const QString &path, const QString &component,
              const QList<DatabaseMigration> &migrations);
    void close();
    bool isOpen() const;
    // The component's schema version after the last open (0 when closed).
    int schemaVersion() const;

    // Runs each statement in order; returns false (having warned) at the
    // first failure. Migration steps are built from this.
    static bool execStatements(QSqlDatabase &db, const QStringList &statements);
    // Runs apply() in one transaction on the connection: begin, apply,
    // commit — a failed apply or commit rolls back. `what` names the
    // operation in the begin/commit warnings (an apply failure is the
    // lambda's to report); returns apply's result (false when the
    // transaction cannot be started or the commit fails). The database is
    // passed by value on purpose: a copy shares the underlying connection.
    static bool inTransaction(QSqlDatabase db,
                              const std::function<bool(QSqlDatabase &)> &apply,
                              const QString &what);
    // A null QString binds as SQL NULL, which violates the NOT NULL
    // columns the components use; empty (missing) metadata is stored as
    // an empty string instead. The guard at every bind site of a string.
    static QString nonNull(const QString &s);
    // The column names of table (empty when it does not exist). The
    // schema-reading query is scoped inside the call: while it is alive it
    // holds a read lock on the table's schema, and schema-modifying DDL on
    // the table would fail with "database table is locked".
    static QStringList tableColumns(const QSqlDatabase &db, const QString &table);

    // A copy shares the same underlying connection (QSqlDatabase is
    // reference counted). Use it to construct QSqlQuery objects.
    QSqlDatabase database() const { return m_db; }

private:
    QSqlDatabase m_db;
    QString m_connectionName;
    int m_schemaVersion = 0;
};

#endif // DATABASE_H
