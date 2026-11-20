#ifndef URLIMPORTDIALOG_H
#define URLIMPORTDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QHideEvent>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include "listexpander.h"
#include "sourceprovider.h"
#include "trackmetadata.h"

class QLabel;
class QLineEdit;
class UiState;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

// "Add by URL...": a small wizard for importing media by pasted links.
// Non-modal — a list expansion (one async provider run per list) can take
// a while, and the app must stay usable while it runs; the owner shows a
// single instance and raises it on a second trigger.
//
// The stages:
//   1. input   — the pasted lines (one source per line: a file path, a
//                link a source provider understands, a plain audio stream).
//   2. working — the claiming providers are asked for the collection each
//                line references (a list — the slow part for a huge
//                playlist) and, for the lines that reference no
//                collection, for their metadata. Nothing is persisted in
//                this stage, so a cancel is always clean.
//   3. review  — every item the import would create, grouped by source
//                (a list under its title, the plain sources under
//                "Other sources"): checkable rows carrying the title,
//                artist, album and duration the working stage gathered
//                (a list video's from the list's own listing), all
//                checked by default. A row whose metadata is unknown (a
//                failed fetch) shows its url. A claimed source whose
//                collection could not be read is listed under "Failed
//                sources" (its url and the reason, not checkable — a
//                failed expansion is never imported as a single item),
//                and a legitimately empty collection is a section with
//                no rows. A filter (case-insensitive on the row's
//                title/artist, display-only — hidden rows keep their
//                check state and still count for the Add button)
//                narrows the view; Select all / Select none apply to the
//                visible rows only. Before the Add, a row's title,
//                artist and album can be edited from the row's context
//                menu — the row alone, or the selected rows in bulk
//                (empty fields keep the gathered value); the edits ride
//                the confirmed metadata (an import reuses the store's
//                item for a source it already holds, so an edit applies
//                at creation only).
//
// sourcesConfirmed fires when the user confirms the review: the checked
// sources (in batch order, deduped) plus their metadata — what the
// wizard already read (the list's own listing, the fetched metadata of
// the plain sources) with the review's edits applied. The import uses
// it directly (a source with provided metadata is not fetched again).
class UrlImportDialog : public QDialog
{
    Q_OBJECT
public:
    // The source string form to the metadata the wizard gathered (the
    // signal's metatable entry; an alias, since the comma in the
    // template breaks the Q_DECLARE_METATYPE macro).
    using SourceMeta = QHash<QString, TrackMeta>;

    explicit UrlImportDialog(QWidget *parent = nullptr);

    // Parses the pasted lines and starts the wizard (a parse that yields
    // no source stays on the input stage).
    void start(const QStringList &lines);

    // The confirmed sources (set when sourcesConfirmed fires).
    QList<QUrl> confirmedSources() const { return m_confirmed; }

protected:
    // A hide (a button's reject/accept hides without a closeEvent, a
    // close hides after it): the review tree's column widths are saved
    // here, like the geometry.
    void hideEvent(QHideEvent *event) override;

signals:
    void sourcesConfirmed(const QList<QUrl> &sources,
                          const SourceMeta &metadata);

private:
    struct Row
    {
        QUrl url;
        QString title;
        QString artist;
        QString album;
        qint64 durationMs = 0;
        // The row's image: the listing's thumbnail (a URL, downloaded
        // lazily for the review) or the fetched metadata's art (already
        // in hand as bytes) — the review row's icon.
        QString thumbnail;
        QByteArray artData;
        QString artMime;
        // False for a failed collection read: the row is shown (its url
        // and the reason) but not checkable — it is never imported.
        bool importable = true;
    };
    struct Section
    {
        QString title;
        QList<Row> rows;
    };

    void buildInputPage();
    void buildWorkingPage();
    void buildReviewPage();
    void onInputAddClicked();
    void onWorkingCancelClicked();
    void onReviewAddClicked();
    void onReviewCancelClicked();
    void onTreeItemChanged(QTreeWidgetItem *item, int column);
    // The review row's context menu: the row's metadata edit (the row's
    // own, or the selected rows' when the row is part of a
    // multi-selection).
    void onTreeContextMenu(const QPoint &pos);
    // The edit dialog's applied fields onto the rows (the non-empty
    // ones; the row's display follows).
    void editRows(const QList<QTreeWidgetItem*> &rows);
    void onFilterTextChanged(const QString &text);
    void onVisibleSelectAllClicked();
    void onVisibleSelectNoneClicked();
    void startDiscovery();
    void onSourceList(const QUrl &url, const SourceProvider::ListEntries &entries);
    void onDiscoveryDone();
    void startMetadataFetch(const QUrl &url);
    void onMetadataFetched(const QUrl &url, const TrackMeta &meta);
    void updateWorkingStatus();
    void showReview();
    void buildSections();
    // The review row of a list video (its display metadata, the url when
    // the listing carried none).
    Row rowForVideo(const SourceProvider::ListVideo &video) const;
    // The review row of a plain source (its fetched metadata, the file
    // name for a pasted path, the url when the fetch failed).
    Row rowForPlainSource(const QUrl &source) const;
    void populateReview();
    // The display-only filter: hides the rows whose title/artist do not
    // match (case-insensitive substring), and a section whose rows are
    // all hidden. Check states are untouched — hidden checked rows still
    // count for the Add button.
    void applyFilter();
    // Sets the check state of the rows currently visible (a section head
    // reflects all of its rows, visible or not).
    void setVisibleRowsChecked(Qt::CheckState state);
    QSet<QString> checkedKeys() const;
    // The checked sources (ListExpander::apply over the checked rows).
    QList<QUrl> checkedSources() const;
    // The metadata the wizard gathered for the checked sources (the list
    // listing's display fields, the fetched metadata of the plain
    // sources): the import uses it as-is.
    QHash<QString, TrackMeta> sourceMeta() const;
    void updateAddButton();
    // The section head's state for the checked/total mix of its rows.
    static Qt::CheckState headState(int checked, int total);

    UiState* m_state = nullptr;
    QStackedWidget* m_stack;
    // The input stage.
    QPlainTextEdit* m_input;
    QPushButton* m_inputAdd;
    // The working stage.
    QLabel* m_status;
    QProgressBar* m_progress;
    // The review stage.
    QLineEdit* m_filter;
    QTreeWidget* m_tree;
    QPushButton* m_addButton;
    bool m_populating = false;
    // Bumped on every review populate: an in-flight thumbnail download
    // must not touch the items a re-populate replaced (QTreeWidgetItem
    // is not a QObject — no QPointer for it).
    int m_iconGeneration = 0;

    // The wizard's state.
    QList<QUrl> m_sources;   // the parsed batch, in paste order
    QHash<QString, SourceProvider::ListEntries> m_lists;  // discovered, keyed by the pasted url
    QHash<QString, QString> m_failed;  // claimed url -> the read's failure reason
    QHash<QString, QString> m_emptyCollections;  // claimed url -> the collection's title
    QHash<QString, TrackMeta> m_meta;  // fetched metadata of the plain claimed sources
    // The review's metadata edits, keyed like sourceMeta(): the fields
    // the user set (empty ones dropped — they keep the gathered value).
    QHash<QString, TrackMeta> m_edits;
    QList<Section> m_sections;
    QList<QUrl> m_confirmed;

    ListExpander m_expander;
    int m_listPending = 0;  // fetchList calls in flight
    int m_metaPending = 0;  // fetchMetadata calls in flight
    int m_metaTotal = 0;
    QString m_lastFound;    // the last list the working status reported
    bool m_cancelled = false;
};

Q_DECLARE_METATYPE(UrlImportDialog::SourceMeta)

#endif // URLIMPORTDIALOG_H
