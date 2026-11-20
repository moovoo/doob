#include "singlemetadatadialog.h"
#include "dialogplumbing.h"
#include "librarymanager.h"
#include "mediaitemstore.h"
#include "metadatable.h"
#include "uistate.h"

#include <algorithm>

#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace {

// "3/10" and "3" share the shape; the display joins them again.
QString numberTotal(int number, int total)
{
    if (number > 0 && total > 0)
        return QStringLiteral("%1/%2").arg(number).arg(total);
    if (number > 0)
        return QString::number(number);
    if (total > 0)
        return QStringLiteral("/%1").arg(total);
    return {};
}

} // namespace

QList<SingleMetaDataDialog::TagRow> SingleMetaDataDialog::tagRows(const MediaItem &item)
{
    QList<TagRow> rows;
    auto add = [&rows](const QString &key, const QString &value) {
        if (!value.isEmpty())
            rows.append(TagRow{ key, value });
    };
    add(QStringLiteral("TITLE"), item.title);
    add(QStringLiteral("ARTIST"), item.artist);
    add(QStringLiteral("ALBUM"), item.album);
    add(QStringLiteral("ALBUMARTIST"), item.albumArtist);
    add(QStringLiteral("COMPOSER"), item.composer);
    add(QStringLiteral("GENRE"), item.genre);
    add(QStringLiteral("COMMENT"), item.comment);
    add(QStringLiteral("TRACKNUMBER"), numberTotal(item.trackNumber, item.trackTotal));
    add(QStringLiteral("DISCNUMBER"), numberTotal(item.discNumber, item.discTotal));
    add(QStringLiteral("DATE"), item.year > 0 ? QString::number(item.year) : QString());
    // The extra map iterates in key order: the custom tags are sorted.
    for (auto it = item.extra.constBegin(); it != item.extra.constEnd(); ++it)
        rows.append(TagRow{ it.key(), it.value() });
    return rows;
}

SingleMetaDataDialog::SingleMetaDataDialog(quint64 id, QWidget *parent)
    : QDialog(parent)
    , m_id(id)
    , m_original(MediaItemStore::getInstance()->item(id))
{
    const QString name = m_original.title.isEmpty()
        ? m_original.source.fileName()
        : m_original.title;
    setWindowTitle(QStringLiteral("Edit metadata — %1").arg(name));

    m_model = new QStandardItemModel(this);
    buildModel();

    m_view = metadata::editableTable(this, m_model);
    m_view->setColumnWidth(Key, 160);

    m_addButton = new QPushButton("Add tag", this);
    m_addButton->setObjectName("addTagButton");
    m_removeButton = new QPushButton("Remove tag", this);
    m_removeButton->setObjectName("removeTagButton");
    m_removeButton->setEnabled(false);
    connect(m_addButton, &QPushButton::clicked, this, &SingleMetaDataDialog::onAddRow);
    connect(m_removeButton, &QPushButton::clicked, this, &SingleMetaDataDialog::onRemoveRow);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SingleMetaDataDialog::updateButtons);

    m_readProgressBar =
        metadata::readButton(this, &m_readButton, "Read from file",
                             "readFileButton",
                             QStringLiteral("Re-read the tags from the file and "
                                            "show them here (saved only when "
                                            "you save)"));
    connect(m_readButton, &QPushButton::clicked, this, &SingleMetaDataDialog::onReadClicked);

    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");

    QHBoxLayout *actions = new QHBoxLayout;
    actions->addWidget(m_addButton);
    actions->addWidget(m_removeButton);
    actions->addWidget(m_readButton);
    actions->addWidget(m_readProgressBar);
    actions->addStretch();
    actions->addWidget(m_status);

    QHBoxLayout *buttons = new QHBoxLayout;
    dialog::dialogButtons(this, buttons, "Cancel", "cancelButton",
                          "Save", "saveButton", &SingleMetaDataDialog::onSave);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_view);
    layout->addLayout(actions);
    layout->addLayout(buttons);

    m_bold = font();
    m_bold.setBold(true);

    connect(m_model, &QStandardItemModel::itemChanged, this,
            &SingleMetaDataDialog::onCellChanged);
    // The read button follows the manager's busy state (a read is a
    // manager operation, serialized with the imports).
    connect(LibraryManager::getInstance(), &LibraryManager::busyChanged, this,
            [this](bool) { updateReadButton(); });

    updateStatus();
    updateReadButton();
    resize(560, 480);

    // The dialog's own state: its size and position (saved on every
    // hide) and the table's column widths ("tag" and "value").
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.single_metadata"));
    m_state->restoreColumns(m_view->horizontalHeader(), { "tag", "value" });
}

void SingleMetaDataDialog::hideEvent(QHideEvent *event)
{
    // The buttons close through reject/accept, which hide the dialog
    // without a closeEvent — the columns are saved on every hide, like
    // the geometry (an unchanged save is a no-op).
    m_state->saveColumns(m_view->horizontalHeader(), { "tag", "value" });
    QDialog::hideEvent(event);
}

void SingleMetaDataDialog::buildModel()
{
    m_model->setColumnCount(ColumnCount);
    m_model->setHorizontalHeaderItem(Key, new QStandardItem("Tag"));
    m_model->setHorizontalHeaderItem(Value, new QStandardItem("Value"));

    // The fixed source row: read-only in both cells, the row's identity.
    QStandardItem *sourceKey = new QStandardItem("Source");
    sourceKey->setEditable(false);
    QStandardItem *sourceValue = new QStandardItem(m_original.source.toString());
    sourceValue->setEditable(false);
    m_model->appendRow({ sourceKey, sourceValue });

    for (const TagRow &row : tagRows(m_original)) {
        m_model->appendRow({ new QStandardItem(row.key),
                             new QStandardItem(row.value), });
        m_originalValues.insert(row.key, row.value);
    }
}

QStandardItem* SingleMetaDataDialog::cell(int row, int column) const
{
    return m_model->item(row, column);
}

void SingleMetaDataDialog::onCellChanged(QStandardItem *item)
{
    if (m_styling)
        return;
    const int row = item->row();
    if (row <= 0)
        return; // the source row is read-only

    // The diff is against the database's value for the row's key (an empty
    // row is a no-op on Save, hence unchanged).
    const QString key = cell(row, Key)->text().trimmed().toUpper();
    bool changed = false;
    if (!key.isEmpty()) {
        if (item->column() == Key)
            changed = !m_originalValues.contains(key);
        else
            changed = item->text() != m_originalValues.value(key);
    }
    metadata::markDiff(item, m_originalValues.value(key), changed, m_bold,
                       m_styling);

    updateStatus();
}

void SingleMetaDataDialog::onAddRow()
{
    // Both cells editable, both empty: an untouched row is a no-op on
    // Save (an empty key is skipped) and counts as unchanged.
    m_model->appendRow({ new QStandardItem(), new QStandardItem() });
    updateStatus();
}

void SingleMetaDataDialog::onRemoveRow()
{
    QList<int> rows;
    for (const QModelIndex &index : m_view->selectionModel()->selectedRows())
        if (index.row() > 0)
            rows.append(index.row());
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for (int row : rows)
        m_model->removeRow(row);
    updateButtons();
    updateStatus();
}

void SingleMetaDataDialog::updateButtons()
{
    bool removable = false;
    for (const QModelIndex &index : m_view->selectionModel()->selectedRows())
        if (index.row() > 0)
            removable = true;
    m_removeButton->setEnabled(removable);
}

void SingleMetaDataDialog::updateStatus()
{
    QSet<QString> present;
    int changed = 0;
    for (int row = 1; row < m_model->rowCount(); ++row) {
        const QString key = cell(row, Key)->text().trimmed().toUpper();
        if (key.isEmpty())
            continue; // an empty row is a no-op on Save
        present.insert(key);
        if (!m_originalValues.contains(key)
            || cell(row, Value)->text() != m_originalValues.value(key))
            ++changed;
    }
    // A tag the open-time item had and the table no longer shows (a
    // removed row, a read that dropped it) is a change Save would make.
    for (auto it = m_originalValues.constBegin();
         it != m_originalValues.constEnd(); ++it)
        if (!present.contains(it.key()))
            ++changed;
    m_status->setText(QStringLiteral("%1 tag(s) changed").arg(changed));
}

void SingleMetaDataDialog::updateReadButton()
{
    m_readButton->setEnabled(m_original.kind == MediaItem::Local
                             && !LibraryManager::getInstance()->busy());
}

void SingleMetaDataDialog::onReadClicked()
{
    if (!metadata::beginRead(m_readButton, m_readProgressBar))
        return;

    // The dialog may be closed while the read runs: the callbacks guard
    // against a destroyed dialog.
    QPointer<SingleMetaDataDialog> guard(this);
    const quint64 id = m_id;
    LibraryManager::getInstance()->readMetadata(
        { id },
        [guard](int resolved, int total) {
            if (!guard)
                return;
            guard->m_readProgressBar->setMaximum(total);
            guard->m_readProgressBar->setValue(resolved);
        },
        [guard, id](const QHash<quint64, TrackMeta> &results) {
            if (!guard)
                return;
            if (results.contains(id))
                guard->applyReadResult(results.value(id));
            guard->m_readProgressBar->setVisible(false);
            guard->updateReadButton();
        });
}

void SingleMetaDataDialog::applyReadResult(const TrackMeta &meta)
{
    m_readMeta = meta;
    const MediaItem read =
        mediaItemFromMeta(m_id, m_original.source, meta, m_original.kind);

    // The table becomes the file's tags (the source row stays). The diff
    // baseline is per key, so no baseline update is needed: a mark shows
    // what Save would change relative to the database, and a tag the file
    // does not have is gone from the table (the status counts it).
    while (m_model->rowCount() > 1)
        m_model->removeRow(1);
    for (const TagRow &row : tagRows(read)) {
        m_model->appendRow({ new QStandardItem(row.key),
                             new QStandardItem(row.value), });
        const int r = m_model->rowCount() - 1;
        const QString original = m_originalValues.value(row.key);
        if (row.value != original)
            metadata::markDiff(cell(r, Value), original, true, m_bold,
                               m_styling);
    }
    updateButtons();
    updateStatus();
}

void SingleMetaDataDialog::onSave()
{
    QStringList problems;
    const MediaItem updated = buildUpdatedItem(problems);
    if (!problems.isEmpty()) {
        showWarning(problems.join('\n'));
        return;
    }
    MediaItemStore* store = MediaItemStore::getInstance();
    if (!sameMetadata(updated, m_original)
        && !store->updateItem(updated)) {
        showWarning(QStringLiteral("Writing the item to the database failed"));
        return;
    }
    // The read's art (a re-read that carried a picture): replace the
    // stored art. A re-read without a picture keeps the old art (art is
    // never cleared by a resync, like the statistics).
    if (!m_readMeta.artData.isEmpty()
        && !store->setArtwork(m_id, m_readMeta.artMime, m_readMeta.artData)) {
        showWarning(QStringLiteral("Writing the artwork to the database failed"));
        return;
    }
    accept();
}

MediaItem SingleMetaDataDialog::buildUpdatedItem(QStringList &problems) const
{
    MediaItem updated = m_original;
    // The table is the single source of truth: a removed row deletes its
    // tag, so the metadata fields start out empty (the id, source, kind
    // and the statistics ride along from the snapshot) and every row
    // present re-asserts its field.
    updated.title = QString();
    updated.artist = QString();
    updated.album = QString();
    updated.albumArtist = QString();
    updated.composer = QString();
    updated.genre = QString();
    updated.comment = QString();
    updated.trackNumber = 0;
    updated.trackTotal = 0;
    updated.discNumber = 0;
    updated.discTotal = 0;
    updated.year = 0;
    updated.extra.clear();
    QSet<QString> seen;
    for (int row = 1; row < m_model->rowCount(); ++row) {
        // Keys are normalized as the import does (uppercase).
        const QString key = cell(row, Key)->text().trimmed().toUpper();
        if (key.isEmpty())
            continue; // an empty row (or a renamed-away tag): a no-op
        if (seen.contains(key)) {
            problems.append(QStringLiteral("duplicate tag key: %1").arg(key));
            continue;
        }
        seen.insert(key);
        applyRow(updated, key, cell(row, Value)->text(), problems);
    }
    return updated;
}

void SingleMetaDataDialog::applyRow(MediaItem &item, const QString &key,
                                    const QString &value, QStringList &problems)
{
    if (key == QLatin1String("TITLE")) {
        item.title = value;
    } else if (key == QLatin1String("ARTIST")) {
        item.artist = value;
    } else if (key == QLatin1String("ALBUM")) {
        item.album = value;
    } else if (key == QLatin1String("ALBUMARTIST")) {
        item.albumArtist = value;
    } else if (key == QLatin1String("COMPOSER")) {
        item.composer = value;
    } else if (key == QLatin1String("GENRE")) {
        item.genre = value;
    } else if (key == QLatin1String("COMMENT")) {
        item.comment = value;
    } else if (key == QLatin1String("TRACKNUMBER")) {
        parseNumberTotal(value, item.trackNumber, item.trackTotal, problems, key);
    } else if (key == QLatin1String("DISCNUMBER")) {
        parseNumberTotal(value, item.discNumber, item.discTotal, problems, key);
    } else if (key == QLatin1String("DATE")) {
        if (!value.isEmpty()) {
            bool ok = false;
            const int year = value.trimmed().toInt(&ok);
            if (!ok) {
                problems.append(QStringLiteral("DATE: %1 is not a year").arg(value));
                return;
            }
            item.year = year;
        } else {
            item.year = 0;
        }
    } else {
        // A custom tag: an empty value deletes it (removing a key that is
        // not there is a no-op, so a fresh empty-valued row costs nothing).
        if (value.isEmpty())
            item.extra.remove(key);
        else
            item.extra.insert(key, value);
    }
}

bool SingleMetaDataDialog::parseNumberTotal(const QString &text, int &number,
                                            int &total, QStringList &problems,
                                            const QString &key)
{
    number = 0;
    total = 0;
    if (text.isEmpty())
        return true;
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() > 2) {
        problems.append(QStringLiteral("%1: %2 is not a number (expected N or N/M)")
                            .arg(key, text));
        return false;
    }
    bool ok = true;
    if (!parts.at(0).trimmed().isEmpty())
        number = parts.at(0).trimmed().toInt(&ok);
    if (ok && parts.size() > 1 && !parts.at(1).trimmed().isEmpty())
        total = parts.at(1).trimmed().toInt(&ok);
    if (!ok) {
        problems.append(QStringLiteral("%1: %2 is not a number (expected N or N/M)")
                            .arg(key, text));
        number = 0;
        total = 0;
        return false;
    }
    return true;
}

bool SingleMetaDataDialog::sameMetadata(const MediaItem &a, const MediaItem &b)
{
    return a.title == b.title && a.artist == b.artist && a.album == b.album
        && a.albumArtist == b.albumArtist && a.composer == b.composer
        && a.genre == b.genre && a.comment == b.comment
        && a.trackNumber == b.trackNumber && a.trackTotal == b.trackTotal
        && a.discNumber == b.discNumber && a.discTotal == b.discTotal
        && a.year == b.year && a.extra == b.extra;
}

void SingleMetaDataDialog::showWarning(const QString &message)
{
    QMessageBox::warning(this, QStringLiteral("Edit metadata"), message);
}
