#include "layoutformat.h"

const QList<LayoutMigration>& layoutMigrations()
{
    // The format's history, oldest first. v1 is the current shape (the
    // "container", "window" and "windows" keys at the document root),
    // so there is no step yet — the first real format change appends a
    // LayoutMigration here (and its test hand-crafts the v1 document it
    // migrates).
    static const QList<LayoutMigration> migrations{};
    return migrations;
}

int documentVersion(const QJsonObject& doc)
{
    // Every save writes the key; a missing one is read as 1.
    return doc.value("version").toInt(1);
}

bool migrateDocument(QJsonObject& doc, int &from, QString &error,
                     const QList<LayoutMigration>& migrations)
{
    from = documentVersion(doc);
    for (const LayoutMigration& migration : migrations) {
        if (migration.version <= from)
            continue;
        QString stepError;
        if (!migration.step(doc, stepError)) {
            error = QStringLiteral("migration to version %1: %2")
                        .arg(migration.version)
                        .arg(stepError);
            return false;
        }
    }
    doc["version"] = kLayoutVersion;
    return true;
}
