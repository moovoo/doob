#ifndef LAYOUTFORMAT_H
#define LAYOUTFORMAT_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>

// The versioned layout document format. The document is the JSON object
// the layouts' files hold (the "container", "window" and "windows" keys
// at the root); its version is the "version" key at the same root,
// written by every save. A document outlives the build that wrote it
// (a shipped preset, a user's saved layout), so a format change is a
// numbered migration, not a rewrite: the loader runs the pending steps
// in memory and the migrated document lands in the new format on the
// next save. The pattern mirrors the database's schema migrations
// (database.{h,cpp}): an ordered list of steps, each producing the next
// version.
//
// What versioning covers and what it doesn't: the steps migrate the root
// document's shape; per-widget state keys evolve leniently on their own
// (restoreWidget ignores unknown keys, missing keys take their
// defaults); new widget types are the registry's concern (the strict /
// lenient unknown-type walk). Widget ids are a stable API like plugin
// ids — deprecated and aliased, never renamed.
inline static const int kLayoutVersion = 1;

// One step of the format's history: `version` is the version the step
// produces, `step` transforms a document of the previous version in
// place and returns false (filling `error`) when it cannot.
struct LayoutMigration
{
    int version;
    std::function<bool(QJsonObject&, QString&)> step;
};

// The format's history, oldest first (v1 is the current shape, so the
// list is empty until the first real change; a new step appends here
// and ships with a test that hand-crafts the previous version's
// document).
const QList<LayoutMigration>& layoutMigrations();

// The version of a document (the "version" key; 1 when absent — a
// defensive line, no legacy machinery).
int documentVersion(const QJsonObject& doc);

// Runs the pending steps (every migration with a version above the
// document's) on `doc`, in place, in order. `from` reports the version
// the document started at, so the caller can tell whether anything
// actually ran; the document's "version" key ends at kLayoutVersion
// when the steps succeed. Returns false (with `error` naming the failed
// step) when a step fails — the document may be partially migrated then
// and must be discarded by the caller.
bool migrateDocument(QJsonObject& doc, int &from, QString &error,
                     const QList<LayoutMigration>& migrations = layoutMigrations());

#endif // LAYOUTFORMAT_H
