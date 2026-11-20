#ifndef LISTEXPANDER_H
#define LISTEXPANDER_H

#include <QHash>
#include <QList>
#include <QSet>
#include <QUrl>

#include <functional>

#include "sourceprovider.h"

// Turns a batch of pasted sources into the batch to import. A pasted
// link may reference a collection (a list): the provider is asked for
// the collection's videos (asynchronously) and the user confirms per
// video in the import wizard's review; a confirmed video replaces its
// list link.
class ListExpander : public QObject
{
    Q_OBJECT
public:
    explicit ListExpander(QObject *parent = nullptr) : QObject(parent) {}

    // Invoked on the GUI thread for every claimed url, as its fetchList
    // answers — with the entries (empty when the url references no
    // collection).
    using EachDiscovered =
        std::function<void(const QUrl &, const SourceProvider::ListEntries &)>;
    // Invoked on the GUI thread exactly once, after every fetch has been
    // started and answered, with the urls (keyed by their string form)
    // that reference a non-empty collection; urls without a claiming
    // provider (local files, plain stream URLs) never appear.
    using Discovered =
        std::function<void(const QHash<QString, SourceProvider::ListEntries> &)>;

    // Asks the claiming provider of every url for the collection the url
    // references.
    void discover(const QList<QUrl> &urls, const EachDiscovered &each,
                  const Discovered &done);

    // The import list for the pasted urls, in batch order, deduped (the
    // first position wins): a list link contributes its checked videos,
    // in list order, a plain url itself when checked. The list link is
    // never a source of its own (a list whose videos are all unchecked
    // contributes nothing); a video the batch already carries is kept
    // once, in its first position.
    static QList<QUrl> apply(const QList<QUrl> &urls,
                             const QHash<QString, SourceProvider::ListEntries> &lists,
                             const QSet<QString> &selected);
};

#endif // LISTEXPANDER_H
