#ifndef METATRANSFORM_H
#define METATRANSFORM_H

#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>

#include "mediaitem.h"

// One expression evaluation: ok + the value (when ok) or the error text.
struct TransformResult
{
    bool ok = false;
    QString value;
    QString error;
};

// The metadata transformation engine: a pipeline of string operations
// applied to a macro (a named variable), read left to right —
//
//     %macro% op(args) op(args) ...
//
// Each op consumes the previous result. Pure and widget-free: the UI
// (the transform dialog) builds the per-row variable map and shows the
// results.
namespace MetaTransform
{
    // Evaluates expr against vars (macro name -> value, names are
    // case-insensitive). A macro absent from vars, an unknown op, an
    // arity mismatch, a bad integer or a bad regex all yield ok == false
    // with a readable error (nothing is applied on an error).
    TransformResult evaluate(const QString &expr,
                             const QHash<QString, QString> &vars);

    // The per-row variable map for one item: the file parts (filename,
    // stem, ext, dir, path — only for local items), the original
    // metadata fields, and current (the cell's present value, passed in).
    QHash<QString, QString> variablesFor(const MediaItem &item,
                                         const QString &current);

    // The op reference, in order: (signature, what it does).
    QList<QPair<QString, QString>> opDocs();
    // The macro reference, in order: (macro with %, what it is) — the
    // file parts are only filled for local items; the metadata fields
    // are the open-time snapshot.
    QList<QPair<QString, QString>> macroDocs();
}

#endif // METATRANSFORM_H
