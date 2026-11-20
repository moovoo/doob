#include "urlimportdialog.h"

#include "dialogplumbing.h"
#include "medialistmodel.h"
#include "playlistfile.h"
#include "sourcemanager.h"
#include "uistate.h"

#include <doob/imagefetch.h>

#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {
// The review tree's stable column keys (one per logical column, in
// order): the names its widths are stored under.
const QStringList kTreeColumns = { "title", "artist", "album", "duration" };

// The review row's icon: the image scaled to 48×48 (aspect preserved);
// invalid when the bytes are not an image.
QPixmap rowIconPixmap(const QByteArray &data)
{
    return QPixmap::fromImage(QImage::fromData(data)
        .scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// The user's edit onto the gathered metadata: the non-empty fields win
// (an empty field keeps the gathered value — the "leave empty to keep"
// rule of both the individual and the bulk edit).
void patchMeta(TrackMeta &meta, const TrackMeta &patch)
{
    if (!patch.title.isEmpty())
        meta.title = patch.title;
    if (!patch.artist.isEmpty())
        meta.artist = patch.artist;
    if (!patch.album.isEmpty())
        meta.album = patch.album;
}

// The review row's metadata editor (a single row's edit, the selected
// rows' bulk edit): title, artist and album — the fields the import
// builds the item from. An individual edit is prefilled with the row's
// effective metadata, a bulk edit starts empty; in both, an empty field
// keeps the current value. A small fixed modal (no UiState), shown
// non-blocking (setModal + open) so the review stays testable.
class MetadataEditDialog : public QDialog
{
public:
    MetadataEditDialog(const QString &subject, const TrackMeta &base,
                       QWidget *parent)
        : QDialog(parent)
    {
        setObjectName("metadataEdit");
        setWindowTitle("Edit metadata");
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(subject, this));
        layout->addWidget(new QLabel(
            "Leave a field empty to keep its current value:", this));
        QFormLayout* form = new QFormLayout;
        layout->addLayout(form);
        m_title = new QLineEdit(base.title, this);
        m_title->setObjectName("titleEdit");
        form->addRow("Title:", m_title);
        m_artist = new QLineEdit(base.artist, this);
        m_artist->setObjectName("artistEdit");
        form->addRow("Artist:", m_artist);
        m_album = new QLineEdit(base.album, this);
        m_album->setObjectName("albumEdit");
        form->addRow("Album:", m_album);
        QHBoxLayout* buttons = new QHBoxLayout;
        layout->addLayout(buttons);
        dialog::dialogButtons(this, buttons, "Cancel", "cancelButton",
                              "OK", "okButton", &QDialog::accept, false);
    }

    // The fields the user set (trimmed, the empty ones dropped).
    TrackMeta edits() const
    {
        TrackMeta meta;
        meta.title = m_title->text().trimmed();
        meta.artist = m_artist->text().trimmed();
        meta.album = m_album->text().trimmed();
        return meta;
    }

private:
    QLineEdit* m_title;
    QLineEdit* m_artist;
    QLineEdit* m_album;
};
}

UrlImportDialog::UrlImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Add by URL");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 480);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("stageStack");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_stack);

    buildInputPage();
    buildWorkingPage();
    buildReviewPage();

    // The dialog's own state: its size and position (saved on every
    // hide) and the review tree's column widths.
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.url_import"));
    m_state->restoreColumns(m_tree->header(), kTreeColumns);
}

void UrlImportDialog::hideEvent(QHideEvent *event)
{
    // The buttons close through reject/accept, which hide the dialog
    // without a closeEvent — the columns are saved on every hide, like
    // the geometry (an unchanged save is a no-op).
    m_state->saveColumns(m_tree->header(), kTreeColumns);
    QDialog::hideEvent(event);
}

void UrlImportDialog::buildInputPage()
{
    QWidget* page = new QWidget(this);
    page->setObjectName("inputPage");
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(
        "Media URLs, one per line — a file path, a link a source provider "
        "understands, or a plain audio stream:", page));

    m_input = new QPlainTextEdit(page);
    m_input->setObjectName("urlInput");
    m_input->setPlaceholderText("https://example.com/audio/stream.mp3");
    m_input->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_input);

    QHBoxLayout* buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    QPushButton* cancel = new QPushButton("Cancel", page);
    cancel->setObjectName("cancelButton");
    m_inputAdd = new QPushButton("Add", page);
    m_inputAdd->setObjectName("okButton");
    m_inputAdd->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(m_inputAdd);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_inputAdd, &QPushButton::clicked, this, &UrlImportDialog::onInputAddClicked);
    m_stack->addWidget(page);
}

void UrlImportDialog::buildWorkingPage()
{
    QWidget* page = new QWidget(this);
    page->setObjectName("workingPage");
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(
        "Fetching the source's list and metadata — this can take a while "
        "for a long list:", page));

    m_status = new QLabel(page);
    m_status->setObjectName("statusLabel");
    layout->addWidget(m_status);

    m_progress = new QProgressBar(page);
    m_progress->setObjectName("progressBar");
    m_progress->setTextVisible(false);
    layout->addWidget(m_progress);

    QHBoxLayout* buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    QPushButton* cancel = new QPushButton("Cancel", page);
    cancel->setObjectName("workingCancelButton");
    buttons->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, this, &UrlImportDialog::onWorkingCancelClicked);
    m_stack->addWidget(page);
}

void UrlImportDialog::buildReviewPage()
{
    QWidget* page = new QWidget(this);
    page->setObjectName("reviewPage");
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(
        "The import would add the following items. Uncheck what to skip:",
        page));

    // The convenience strip: a filter over the rows (display-only) and
    // select all / select none for the visible rows — for the hundreds-of
    // items workflow: clear the selection, filter, pick the wanted rows.
    QHBoxLayout* tools = new QHBoxLayout;
    layout->addLayout(tools);
    m_filter = new QLineEdit(page);
    m_filter->setObjectName("filterInput");
    m_filter->setPlaceholderText("Filter the items…");
    tools->addWidget(m_filter);
    QPushButton* selectAll = new QPushButton("Select all", page);
    selectAll->setObjectName("selectAllButton");
    QPushButton* selectNone = new QPushButton("Select none", page);
    selectNone->setObjectName("selectNoneButton");
    tools->addWidget(selectAll);
    tools->addWidget(selectNone);

    m_tree = new QTreeWidget(page);
    m_tree->setObjectName("reviewTree");
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({ "Title", "Artist", "Album", "Duration" });
    m_tree->setRootIsDecorated(true);
    // Rows are checkable; the selection (multi) is what the bulk
    // metadata edit targets.
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree);

    QHBoxLayout* buttons = new QHBoxLayout;
    layout->addLayout(buttons);
    m_addButton = new QPushButton("Add", page);
    m_addButton->setObjectName("addButton");
    m_addButton->setDefault(true);
    m_addButton->setEnabled(false);
    QPushButton* cancel = new QPushButton("Cancel", page);
    cancel->setObjectName("reviewCancelButton");
    buttons->addWidget(m_addButton);
    buttons->addWidget(cancel);
    connect(m_addButton, &QPushButton::clicked, this, &UrlImportDialog::onReviewAddClicked);
    connect(cancel, &QPushButton::clicked, this, &UrlImportDialog::onReviewCancelClicked);
    connect(m_tree, &QTreeWidget::itemChanged, this, &UrlImportDialog::onTreeItemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &UrlImportDialog::onTreeContextMenu);
    connect(m_filter, &QLineEdit::textChanged,
            this, &UrlImportDialog::onFilterTextChanged);
    connect(selectAll, &QPushButton::clicked,
            this, &UrlImportDialog::onVisibleSelectAllClicked);
    connect(selectNone, &QPushButton::clicked,
            this, &UrlImportDialog::onVisibleSelectNoneClicked);
    m_stack->addWidget(page);
}

void UrlImportDialog::onInputAddClicked()
{
    start(m_input->toPlainText().split('\n'));
}

void UrlImportDialog::start(const QStringList &lines)
{
    m_sources.clear();
    for (const QString &line : lines) {
        const QString text = line.trimmed();
        if (text.isEmpty())
            continue;
        // The shared argument → source resolution: a URL stays a URL, an
        // existing local path (absolute, or relative to the current
        // directory) becomes a file: URL, a host-like form assumes http.
        m_sources.append(resolveSourceArgument(QDir::currentPath(), text));
    }
    if (m_sources.isEmpty())
        return; // nothing to import: stay on the input stage

    m_lists.clear();
    m_failed.clear();
    m_emptyCollections.clear();
    m_meta.clear();
    m_edits.clear();
    m_sections.clear();
    m_confirmed.clear();
    m_listPending = 0;
    m_metaPending = 0;
    m_metaTotal = 0;
    m_lastFound.clear();
    m_cancelled = false;
    startDiscovery();
}

void UrlImportDialog::startDiscovery()
{
    for (const QUrl &url : m_sources)
        if (SourceManager::getInstance()->providerFor(url) != nullptr)
            ++m_listPending;
    if (m_listPending == 0) {
        // No claiming provider: no list can hide behind any line — import
        // the batch as pasted, no review.
        m_confirmed = m_sources;
        emit sourcesConfirmed(m_confirmed, {});
        accept();
        return;
    }

    m_stack->setCurrentIndex(1);
    m_progress->setRange(0, 0); // indeterminate while the lists are expanded
    updateWorkingStatus();

    // A destroyed dialog (a close mid-fetch) must not be reached by the
    // provider callbacks: a QPointer guard in each.
    QPointer<UrlImportDialog> self(this);
    m_expander.discover(
        m_sources,
        [self](const QUrl &url, const SourceProvider::ListEntries &entries) {
            if (self)
                self->onSourceList(url, entries);
        },
        [self](const QHash<QString, SourceProvider::ListEntries> &) {
            if (self)
                self->onDiscoveryDone();
        });
}

void UrlImportDialog::onSourceList(const QUrl &url,
                                   const SourceProvider::ListEntries &entries)
{
    if (m_cancelled)
        return;
    --m_listPending;
    if (!entries.isEmpty()) {
        m_lists[url.toString()] = entries;
        m_lastFound = QStringLiteral("Found '%1' (%2 videos)")
                          .arg(entries.title, QString::number(entries.videos.size()));
    } else if (!entries.error.isEmpty()) {
        // The collection behind the link could not be read: the link is
        // a container, not a track — it is shown as failed in the
        // review, never imported.
        m_failed[url.toString()] = entries.error;
        m_lastFound = QStringLiteral("Failed to read '%1': %2")
                         .arg(url.toString(), entries.error);
    } else if (entries.isCollection) {
        // A collection that is legitimately empty: nothing to import,
        // an empty section in the review.
        m_emptyCollections[url.toString()] = entries.title;
    } else {
        // No collection behind the link: a plain source — its metadata
        // fills the review row.
        startMetadataFetch(url);
    }
    updateWorkingStatus();
}

void UrlImportDialog::onDiscoveryDone()
{
    if (m_cancelled)
        return;
    if (m_lists.isEmpty() && m_failed.isEmpty()
        && m_emptyCollections.isEmpty()) {
        // No collection anywhere (and no failed read): import the batch
        // as pasted, no review.
        m_confirmed = m_sources;
        emit sourcesConfirmed(m_confirmed, {});
        accept();
        return;
    }
    if (m_metaPending == 0)
        showReview();
}

void UrlImportDialog::startMetadataFetch(const QUrl &url)
{
    if (m_cancelled)
        return;
    SourceProvider* provider = SourceManager::getInstance()->providerFor(url);
    if (provider == nullptr)
        return;
    ++m_metaPending;
    ++m_metaTotal;
    QPointer<UrlImportDialog> self(this);
    provider->fetchMetadata(url, [self, url](const TrackMeta &meta) {
        if (self)
            self->onMetadataFetched(url, meta);
    });
    updateWorkingStatus();
}

void UrlImportDialog::onMetadataFetched(const QUrl &url, const TrackMeta &meta)
{
    if (m_cancelled)
        return;
    --m_metaPending;
    if (!meta.isEmpty())
        m_meta[url.toString()] = meta;
    updateWorkingStatus();
    // The review is shown by onDiscoveryDone: the discovery's done fires
    // after every metadata callback has landed (a provider may answer
    // synchronously, so the count alone cannot decide completion there),
    // and a batch with no list behind its claimed urls fast-paths to the
    // confirmation without a review (the fetched metadata is discarded
    // with it — the import reads it again).
}

void UrlImportDialog::updateWorkingStatus()
{
    QString text;
    if (m_metaTotal > 0 && m_metaPending > 0) {
        m_progress->setRange(0, m_metaTotal);
        m_progress->setValue(m_metaTotal - m_metaPending);
        text = QStringLiteral("Reading metadata %1 of %2…")
                   .arg(m_metaTotal - m_metaPending)
                   .arg(m_metaTotal);
    } else if (m_listPending > 0) {
        m_progress->setRange(0, 0);
        if (m_lastFound.isEmpty())
            text = QStringLiteral("Expanding %1 source(s)…").arg(m_listPending);
        else
            text = m_lastFound + QStringLiteral(" — ")
                 + QStringLiteral("%1 source(s) left").arg(m_listPending);
    } else {
        m_progress->setRange(0, 0);
        text = m_lastFound;
    }
    m_status->setText(text);
}

void UrlImportDialog::showReview()
{
    buildSections();
    populateReview();
    m_stack->setCurrentIndex(2);
    updateAddButton();
}

void UrlImportDialog::buildSections()
{
    m_sections.clear();
    Section other;
    other.title = QStringLiteral("Other sources");
    for (const QUrl &source : m_sources) {
        const QString key = source.toString();
        const auto it = m_lists.constFind(key);
        if (it != m_lists.constEnd()) {
            Section section;
            section.title = it.value().title.isEmpty()
                ? key : it.value().title;
            for (const SourceProvider::ListVideo &video : it.value().videos)
                section.rows.append(rowForVideo(video));
            m_sections.append(section);
            continue;
        }
        const auto emptyIt = m_emptyCollections.constFind(key);
        if (emptyIt != m_emptyCollections.constEnd()) {
            // A collection with no tracks: a section, no rows.
            Section section;
            section.title = emptyIt.value().isEmpty()
                ? key : emptyIt.value();
            m_sections.append(section);
            continue;
        }
        if (m_failed.constFind(key) == m_failed.constEnd())
            other.rows.append(rowForPlainSource(source));
        // A failed read is not a source to import: it is listed under
        // "Failed sources" (below), not here.
    }
    if (!other.rows.isEmpty())
        m_sections.append(other);
    Section failed;
    failed.title = QStringLiteral("Failed sources");
    for (const QUrl &source : m_sources) {
        const auto it = m_failed.constFind(source.toString());
        if (it == m_failed.constEnd())
            continue;
        Row row;
        row.url = source;
        row.title = source.toString();
        row.artist = QStringLiteral("failed: %1").arg(it.value());
        row.importable = false;
        failed.rows.append(row);
    }
    if (!failed.rows.isEmpty())
        m_sections.append(failed);
}

UrlImportDialog::Row UrlImportDialog::rowForVideo(
    const SourceProvider::ListVideo &video) const
{
    Row row;
    row.url = video.url;
    row.title = video.title.isEmpty() ? video.url.toString() : video.title;
    row.artist = video.artist;
    row.album = video.album;
    row.durationMs = video.durationMs;
    row.thumbnail = video.thumbnail;
    return row;
}

UrlImportDialog::Row UrlImportDialog::rowForPlainSource(const QUrl &source) const
{
    Row row;
    row.url = source;
    if (source.isLocalFile()) {
        // A pasted path: the file name is the display name (the same
        // derivation the item building uses).
        row.title = QFileInfo(source.toLocalFile())
                        .completeBaseName()
                        .replace('_', ' ');
        return row;
    }
    const TrackMeta meta = m_meta.value(source.toString());
    if (meta.isEmpty()) {
        // A failed fetch: the url is all the row has.
        row.title = source.toString();
        return row;
    }
    row.title = meta.title.isEmpty() ? source.toString() : meta.title;
    row.artist = meta.artist;
    row.album = meta.album;
    row.durationMs = meta.durationMs;
    // The fetched meta's art is already in hand (the read downloaded
    // it): the review row's icon, no further fetch.
    row.artData = meta.artData;
    row.artMime = meta.artMime;
    return row;
}

void UrlImportDialog::populateReview()
{
    m_populating = true;
    m_iconGeneration++;
    const int generation = m_iconGeneration;
    m_tree->clear();
    for (const Section &section : m_sections) {
        QTreeWidgetItem* head = new QTreeWidgetItem(m_tree);
        head->setText(0, QStringLiteral("%1  (%2)")
                            .arg(section.title)
                            .arg(section.rows.size()));
        head->setFlags(head->flags() | Qt::ItemIsUserCheckable);
        head->setCheckState(0, Qt::Checked);
        for (const Row &row : section.rows) {
            QTreeWidgetItem* item = new QTreeWidgetItem(head);
            item->setData(0, Qt::UserRole, row.url.toString());
            item->setText(0, row.title);
            item->setText(1, row.artist);
            item->setText(2, row.album);
            // An unknown duration stays blank (the shared formatter
            // would show 0:00).
            item->setText(3, row.durationMs > 0
                ? MediaListModel::formatDuration(row.durationMs, true)
                : QString());
            // The row's icon: the fetched art (already in hand) or the
            // listing's thumbnail (a download on the shared image path —
            // the item may die before it lands: a re-populate, a closed
            // dialog).
            if (!row.artData.isEmpty()) {
                item->setIcon(0, rowIconPixmap(row.artData));
            } else if (!row.thumbnail.isEmpty()) {
                // The item may die before the download lands (a
                // re-populate, a closed dialog): the generation is the
                // populate's, the guard the dialog's life.
                QPointer<UrlImportDialog> guard = this;
                ImageFetch::getInstance()->fetch(
                    QUrl(row.thumbnail),
                    [this, guard, generation, item](
                        const QByteArray &data, const QString &) {
                        if (guard == nullptr
                            || generation != m_iconGeneration)
                            return;
                        if (!data.isEmpty())
                            item->setIcon(0, rowIconPixmap(data));
                    });
            }
            if (row.importable) {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, Qt::Checked);
            } else {
                // A failed read: shown, not checkable — it can never be
                // part of the confirmed import. The item's default
                // flags are user-checkable, so the flag has to be
                // cleared (the propagation paths guard on it).
                item->setFlags(item->flags()
                    & ~Qt::ItemIsUserCheckable);
                item->setCheckState(0, Qt::Unchecked);
            }
        }
    }
    m_tree->expandAll();
    m_populating = false;
    applyFilter();
}

void UrlImportDialog::applyFilter()
{
    const QString text = m_filter->text();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = m_tree->topLevelItem(i);
        bool anyVisible = false;
        for (int j = 0; j < head->childCount(); ++j) {
            QTreeWidgetItem* row = head->child(j);
            const bool matches = text.isEmpty()
                || row->text(0).contains(text, Qt::CaseInsensitive)
                || row->text(1).contains(text, Qt::CaseInsensitive);
            row->setHidden(!matches);
            anyVisible |= matches;
        }
        head->setHidden(!anyVisible);
    }
}

void UrlImportDialog::setVisibleRowsChecked(Qt::CheckState state)
{
    m_populating = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = m_tree->topLevelItem(i);
        if (head->isHidden())
            continue;
        for (int j = 0; j < head->childCount(); ++j) {
            QTreeWidgetItem* row = head->child(j);
            // The failed rows are not checkable: a select-all must not
            // touch them.
            if (!row->isHidden()
                && (row->flags() & Qt::ItemIsUserCheckable) != 0)
                row->setCheckState(0, state);
        }
        // The head reflects all of its rows, visible or not.
        int checked = 0;
        for (int j = 0; j < head->childCount(); ++j)
            if (head->child(j)->checkState(0) == Qt::Checked)
                ++checked;
        head->setCheckState(0, headState(checked, head->childCount()));
    }
    m_populating = false;
    updateAddButton();
}

void UrlImportDialog::onFilterTextChanged(const QString &text)
{
    Q_UNUSED(text);
    applyFilter();
}

void UrlImportDialog::onVisibleSelectAllClicked()
{
    setVisibleRowsChecked(Qt::Checked);
}

void UrlImportDialog::onVisibleSelectNoneClicked()
{
    setVisibleRowsChecked(Qt::Unchecked);
}

QSet<QString> UrlImportDialog::checkedKeys() const
{
    QSet<QString> keys;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = m_tree->topLevelItem(i);
        for (int j = 0; j < head->childCount(); ++j) {
            QTreeWidgetItem* item = head->child(j);
            // The failed rows are not checkable (and never counted).
            if ((item->flags() & Qt::ItemIsUserCheckable) != 0
                && item->checkState(0) == Qt::Checked)
                keys.insert(item->data(0, Qt::UserRole).toString());
        }
    }
    return keys;
}

QList<QUrl> UrlImportDialog::checkedSources() const
{
    return ListExpander::apply(m_sources, m_lists, checkedKeys());
}

QHash<QString, TrackMeta> UrlImportDialog::sourceMeta() const
{
    QHash<QString, TrackMeta> out;
    for (auto it = m_lists.cbegin(); it != m_lists.cend(); ++it) {
        for (const SourceProvider::ListVideo &video : it.value().videos) {
            if (video.title.isEmpty() && video.artist.isEmpty()
                && video.album.isEmpty() && video.durationMs == 0)
                continue;
            TrackMeta meta;
            meta.title = video.title;
            meta.artist = video.artist;
            meta.album = video.album;
            meta.durationMs = video.durationMs;
            out.insert(video.url.toString(), meta);
        }
    }
    for (auto it = m_meta.cbegin(); it != m_meta.cend(); ++it)
        out.insert(it.key(), it.value());
    // The review's metadata edits (a row's own, the selected rows'
    // bulk): the fields the user set win over what the wizard read.
    for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it)
        patchMeta(out[it.key()], it.value());
    return out;
}

void UrlImportDialog::updateAddButton()
{
    const qsizetype count = checkedSources().size();
    m_addButton->setEnabled(count > 0);
    m_addButton->setText(count > 0
        ? QStringLiteral("Add %1 item%2").arg(count).arg(count == 1 ? "" : "s")
        : QStringLiteral("Add"));
}

void UrlImportDialog::onTreeItemChanged(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (m_populating)
        return;
    if (item->parent() == nullptr) {
        // QTreeWidget does not cascade a parent's check state to its
        // children: a section head toggles the whole list (the partial
        // state is only ever set from the rows' side and never
        // propagated).
        const Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked) {
            m_populating = true;
            for (int j = 0; j < item->childCount(); ++j) {
                QTreeWidgetItem* row = item->child(j);
                if ((row->flags() & Qt::ItemIsUserCheckable) != 0)
                    row->setCheckState(0, state);
            }
            m_populating = false;
        }
    } else {
        // ... and the head reflects the mixed state of its rows.
        QTreeWidgetItem* head = item->parent();
        int checked = 0;
        for (int j = 0; j < head->childCount(); ++j)
            if (head->child(j)->checkState(0) == Qt::Checked)
                ++checked;
        head->setCheckState(0, headState(checked, head->childCount()));
    }
    updateAddButton();
}

void UrlImportDialog::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    // A section head (no parent) and a failed row (not checkable) are
    // not editable.
    if (item == nullptr || item->parent() == nullptr
        || (item->flags() & Qt::ItemIsUserCheckable) == 0)
        return;
    // The row's selection (heads and failed rows out) when the row is
    // part of one: that is the bulk edit; otherwise the row alone.
    QList<QTreeWidgetItem*> rows;
    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    if (selected.contains(item)) {
        for (QTreeWidgetItem* s : selected)
            if (s->parent() != nullptr
                && (s->flags() & Qt::ItemIsUserCheckable) != 0)
                rows.append(s);
    }
    if (rows.size() < 2)
        rows.append(item);
    // One editor at a time.
    if (findChild<QDialog*>(QStringLiteral("metadataEdit")) != nullptr)
        return;
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* action = menu->addAction(
        rows.size() > 1
            ? QStringLiteral("Edit metadata of %1 selected…").arg(rows.size())
            : QStringLiteral("Edit metadata…"));
    connect(action, &QAction::triggered, this, [this, menu, rows] {
        // A programmatic trigger (a test) does not close the menu the
        // way a mouse click does.
        menu->hide();
        editRows(rows);
    });
    menu->popup(m_tree->viewport()->mapToGlobal(pos));
}

void UrlImportDialog::editRows(const QList<QTreeWidgetItem*> &rows)
{
    TrackMeta base;
    QString subject;
    if (rows.size() == 1) {
        // A solo edit is prefilled with the row's effective metadata
        // (what the wizard read plus the edits applied so far).
        const QString key = rows.first()->data(0, Qt::UserRole).toString();
        base = sourceMeta().value(key);
        subject = base.title.isEmpty() ? rows.first()->text(0) : base.title;
    } else {
        // A bulk edit starts empty: an empty field keeps the row's value.
        subject = QStringLiteral("Editing %1 items").arg(rows.size());
    }
    MetadataEditDialog* edit = new MetadataEditDialog(subject, base, this);
    connect(edit, &QDialog::finished, this, [this, edit, rows](int result) {
        edit->deleteLater();
        if (result != QDialog::Accepted)
            return;
        const TrackMeta patch = edit->edits();
        if (patch.title.isEmpty() && patch.artist.isEmpty()
            && patch.album.isEmpty())
            return; // every field kept: nothing to apply
        m_populating = true;
        for (QTreeWidgetItem* row : rows) {
            const QString key = row->data(0, Qt::UserRole).toString();
            patchMeta(m_edits[key], patch);
            const TrackMeta meta = sourceMeta().value(key);
            // The title column keeps its url/file-name fallback for a
            // row with no title at all.
            if (!meta.title.isEmpty())
                row->setText(0, meta.title);
            row->setText(1, meta.artist);
            row->setText(2, meta.album);
        }
        m_populating = false;
    });
    // Modal to the wizard, non-blocking: the review stays testable
    // (the result lands in the finished slot above).
    edit->setModal(true);
    edit->open();
}

void UrlImportDialog::onReviewAddClicked()
{
    m_confirmed = checkedSources();
    if (m_confirmed.isEmpty())
        return;
    emit sourcesConfirmed(m_confirmed, sourceMeta());
    accept();
}

void UrlImportDialog::onReviewCancelClicked()
{
    reject();
}

void UrlImportDialog::onWorkingCancelClicked()
{
    m_cancelled = true;
    reject();
}

Qt::CheckState UrlImportDialog::headState(int checked, int total)
{
    if (checked == total)
        return Qt::Checked;
    if (checked == 0)
        return Qt::Unchecked;
    return Qt::PartiallyChecked;
}
