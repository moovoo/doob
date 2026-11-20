#include "metadatadialog.h"
#include "dialogplumbing.h"
#include "librarymanager.h"
#include "metadatable.h"
#include "mediaitemstore.h"
#include "metatransformdialog.h"
#include "singlemetadatadialog.h"
#include "uistate.h"

#include <algorithm>

#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

void openMetadataEditor(QWidget *parent, const QList<quint64> &ids)
{
    MetaDataDialog dialog(ids, parent);
    dialog.exec();
}

void openSingleMetadataEditor(QWidget *parent, quint64 id)
{
    SingleMetaDataDialog dialog(id, parent);
    dialog.exec();
}

QStringList MetaDataDialog::columnLabels()
{
    return { QString(), "Source", "Title", "Artist", "Album", "Album Artist",
             "Composer", "Genre", "Comment", "Track #", "Track Total",
             "Disc #", "Disc Total", "Year", };
}

QStringList MetaDataDialog::columnKeys()
{
    return { "select", "source", "title", "artist", "album", "album_artist",
             "composer", "genre", "comment", "track_number", "track_total",
             "disc_number", "disc_total", "year", };
}

bool MetaDataDialog::isNumericColumn(int column)
{
    return column >= TrackNumber && column <= Year;
}

QList<int> MetaDataDialog::editableColumns()
{
    QList<int> columns;
    for (int column = Title; column <= Year; ++column)
        columns.append(column);
    return columns;
}

MetaDataDialog::MetaDataDialog(const QList<quint64> &ids, QWidget *parent)
    : QDialog(parent)
{
    MediaItemStore* store = MediaItemStore::getInstance();

    // One row per item: dedupe (a playlist selection may name the same
    // item several times) and sort into the stable ascending-id order.
    QHash<quint64, bool> seen;
    for (quint64 id : ids) {
        if (seen.contains(id) || !store->contains(id))
            continue;
        seen.insert(id, true);
        m_ids.append(id);
    }
    std::sort(m_ids.begin(), m_ids.end());

    for (int row = 0; row < m_ids.size(); ++row) {
        const MediaItem item = store->item(m_ids.at(row));
        m_originals.append(item);
        m_idToRow.insert(item.id, row);
        if (item.kind == MediaItem::Local)
            m_hasLocalFiles = true;
    }

    setWindowTitle(QStringLiteral("Bulk edit metadata — %1 item(s)").arg(m_ids.size()));

    m_model = new QStandardItemModel(this);
    buildModel();

    m_view = metadata::editableTable(this, m_model);
    m_view->setColumnWidth(Source, 260);
    m_view->setColumnWidth(Comment, 200);

    m_selectAllButton = new QPushButton("Select all", this);
    m_selectAllButton->setObjectName("selectAllButton");
    connect(m_selectAllButton, &QPushButton::clicked, this, [this] {
        setAllChecked(true);
    });
    m_unselectAllButton = new QPushButton("Unselect all", this);
    m_unselectAllButton->setObjectName("unselectAllButton");
    connect(m_unselectAllButton, &QPushButton::clicked, this, [this] {
        setAllChecked(false);
    });

    m_transformButton = new QPushButton("Transform...", this);
    m_transformButton->setObjectName("transformButton");
    m_transformButton->setEnabled(false);
    connect(m_transformButton, &QPushButton::clicked, this,
            &MetaDataDialog::onTransformClicked);

    m_readProgressBar =
        metadata::readButton(this, &m_readFilesButton, "Read from files",
                             "readFilesButton",
                             QStringLiteral("Re-read the tags of these items "
                                            "from their files and show them "
                                            "here (saved only when you apply)"));
    connect(m_readFilesButton, &QPushButton::clicked, this,
            &MetaDataDialog::onReadFilesClicked);

    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");

    QHBoxLayout *actions = new QHBoxLayout;
    actions->addWidget(m_selectAllButton);
    actions->addWidget(m_unselectAllButton);
    actions->addWidget(m_transformButton);
    actions->addWidget(m_readFilesButton);
    actions->addWidget(m_readProgressBar);
    actions->addStretch();
    actions->addWidget(m_status);

    QHBoxLayout *buttons = new QHBoxLayout;
    dialog::dialogButtons(this, buttons, "Cancel", "cancelButton",
                          "Apply", "okButton", &MetaDataDialog::onAccept);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_view);
    layout->addLayout(actions);
    layout->addLayout(buttons);

    m_bold = font();
    m_bold.setBold(true);

    connect(m_model, &QStandardItemModel::itemChanged, this,
            &MetaDataDialog::onCellChanged);
    // The read button follows the manager's busy state (a read is a
    // manager operation, serialized with the imports).
    connect(LibraryManager::getInstance(), &LibraryManager::busyChanged, this,
            [this](bool) { updateReadButton(); });

    updateStatus();
    updateReadButton();
    resize(960, 520);

    // The dialog's own state: its size and position (saved on every
    // hide) and the table's column widths.
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.metadata"));
    m_state->restoreColumns(m_view->horizontalHeader(), columnKeys());
}

void MetaDataDialog::hideEvent(QHideEvent *event)
{
    // The buttons close through reject/accept, which hide the dialog
    // without a closeEvent — the columns are saved on every hide, like
    // the geometry (an unchanged save is a no-op).
    m_state->saveColumns(m_view->horizontalHeader(), columnKeys());
    QDialog::hideEvent(event);
}

void MetaDataDialog::buildModel()
{
    const QStringList labels = columnLabels();
    m_model->setColumnCount(ColumnCount);

    QStandardItem* selectHeader = new QStandardItem("Select");
    selectHeader->setEditable(false);
    selectHeader->setData(Qt::AlignCenter, Qt::TextAlignmentRole);
    m_model->setHorizontalHeaderItem(Select, selectHeader);
    for (int column = 1; column < ColumnCount; ++column)
        m_model->setHorizontalHeaderItem(column, new QStandardItem(labels.at(column)));

    for (int row = 0; row < m_ids.size(); ++row)
    {
        const MediaItem item = m_originals.at(row);

        QStandardItem* select = new QStandardItem();
        select->setEditable(false);
        select->setCheckable(true);
        select->setCheckState(Qt::Unchecked);
        select->setData(Qt::AlignCenter, Qt::TextAlignmentRole);

        QStandardItem* source = new QStandardItem(displayValue(item, Source));
        source->setEditable(false);

        QList<QStandardItem*> rowItems;
        rowItems.append(select);
        rowItems.append(source);
        QStringList rowCells;
        rowCells.append(QString());
        rowCells.append(displayValue(item, Source));
        for (int column = Title; column <= Year; ++column)
        {
            const QString text = displayValue(item, column);
            rowItems.append(new QStandardItem(text));
            rowCells.append(text);
        }
        m_originalCells.insert(row, rowCells);
        m_model->appendRow(rowItems);
    }
}

QStandardItem* MetaDataDialog::cell(int row, int column) const
{
    return m_model->item(row, column);
}

QString MetaDataDialog::displayValue(const MediaItem &item, int column)
{
    switch (column)
    {
    case Source:
        return item.source.toString();
    case Title:
        return item.title;
    case Artist:
        return item.artist;
    case Album:
        return item.album;
    case AlbumArtist:
        return item.albumArtist;
    case Composer:
        return item.composer;
    case Genre:
        return item.genre;
    case Comment:
        return item.comment;
    case TrackNumber:
        return item.trackNumber > 0 ? QString::number(item.trackNumber) : QString();
    case TrackTotal:
        return item.trackTotal > 0 ? QString::number(item.trackTotal) : QString();
    case DiscNumber:
        return item.discNumber > 0 ? QString::number(item.discNumber) : QString();
    case DiscTotal:
        return item.discTotal > 0 ? QString::number(item.discTotal) : QString();
    case Year:
        return item.year > 0 ? QString::number(item.year) : QString();
    default:
        return {};
    }
}

void MetaDataDialog::applyValue(MediaItem &item, int column, const QString &text)
{
    switch (column)
    {
    case Title:
        item.title = text;
        break;
    case Artist:
        item.artist = text;
        break;
    case Album:
        item.album = text;
        break;
    case AlbumArtist:
        item.albumArtist = text;
        break;
    case Composer:
        item.composer = text;
        break;
    case Genre:
        item.genre = text;
        break;
    case Comment:
        item.comment = text;
        break;
    case TrackNumber:
        item.trackNumber = text.trimmed().toInt();
        break;
    case TrackTotal:
        item.trackTotal = text.trimmed().toInt();
        break;
    case DiscNumber:
        item.discNumber = text.trimmed().toInt();
        break;
    case DiscTotal:
        item.discTotal = text.trimmed().toInt();
        break;
    case Year:
        item.year = text.trimmed().toInt();
        break;
    default:
        // Select and Source are never written.
        break;
    }
}

QList<int> MetaDataDialog::checkedRows() const
{
    QList<int> rows;
    for (int row = 0; row < m_ids.size(); ++row)
        if (cell(row, Select)->checkState() == Qt::Checked)
            rows.append(row);
    return rows;
}

void MetaDataDialog::onCellChanged(QStandardItem *item)
{
    if (m_styling)
        return;

    const int row = item->row();
    const int column = item->column();
    if (row < 0 || column == Source)
        return;
    if (column == Select)
    {
        m_transformButton->setEnabled(!checkedRows().isEmpty());
        return;
    }

    const QString original = m_originalCells.value(row).value(column);
    metadata::markDiff(item, original, item->text() != original, m_bold,
                       m_styling);

    updateStatus();
}

void MetaDataDialog::setAllChecked(bool checked)
{
    for (int row = 0; row < m_ids.size(); ++row)
        cell(row, Select)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

void MetaDataDialog::onTransformClicked()
{
    if (checkedRows().isEmpty())
        return;
    openTransformDialog();
}

void MetaDataDialog::openTransformDialog()
{
    MetaTransformDialog dialog(m_model, m_ids, checkedRows(), this);
    dialog.exec();
}

void MetaDataDialog::showWarning(const QString &message)
{
    QMessageBox::warning(this, QStringLiteral("Edit metadata"), message);
}

void MetaDataDialog::updateReadButton()
{
    m_readFilesButton->setEnabled(m_hasLocalFiles
                                  && !LibraryManager::getInstance()->busy());
}

void MetaDataDialog::onReadFilesClicked()
{
    if (!metadata::beginRead(m_readFilesButton, m_readProgressBar))
        return;

    // The dialog may be closed while the read runs: the callbacks guard
    // against a destroyed dialog.
    QPointer<MetaDataDialog> guard(this);
    LibraryManager::getInstance()->readMetadata(
        m_ids,
        [guard](int resolved, int total) {
            if (!guard)
                return;
            guard->m_readProgressBar->setMaximum(total);
            guard->m_readProgressBar->setValue(resolved);
        },
        [guard](const QHash<quint64, TrackMeta> &results) {
            if (!guard)
                return;
            guard->applyReadResults(results);
        });
}

void MetaDataDialog::applyReadResults(const QHash<quint64, TrackMeta> &results)
{
    m_readProgressBar->setVisible(false);
    updateReadButton();

    for (auto it = results.constBegin(); it != results.constEnd(); ++it) {
        // The read's art has no cell: it is kept per id and stored on
        // Accept (an id a later read fails for keeps its earlier result).
        m_readResults.insert(it.key(), it.value());
        const int row = m_idToRow.value(it.key(), -1);
        if (row < 0)
            continue;
        const MediaItem original = m_originals.at(row);
        const MediaItem read = mediaItemFromMeta(original.id, original.source,
                                                 it.value(), original.kind);
        // The read values become cell text: the cell-changed slot marks
        // the diff against the open-time snapshot, as for a manual edit.
        for (int column = Title; column <= Year; ++column) {
            const QString text = displayValue(read, column);
            QStandardItem *item = cell(row, column);
            if (text != item->text())
                item->setText(text);
        }
    }
}

void MetaDataDialog::updateStatus()
{
    int changed = 0;
    for (int row = 0; row < m_ids.size(); ++row)
    {
        bool rowChanged = false;
        for (int column = Title; column <= Year; ++column)
            if (cell(row, column)->text() != m_originalCells.value(row).value(column))
                rowChanged = true;
        if (rowChanged)
            ++changed;
    }
    m_status->setText(QStringLiteral("%1 of %2 item(s) changed")
                          .arg(changed)
                          .arg(m_ids.size()));
}

void MetaDataDialog::onAccept()
{
    MediaItemStore* store = MediaItemStore::getInstance();

    // Validate the number cells first: an unparseable number blocks the
    // whole commit (the dialog stays open for a fix).
    QStringList problems;
    for (int row = 0; row < m_ids.size(); ++row) {
        for (int column = TrackNumber; column <= Year; ++column) {
            const QString text = cell(row, column)->text().trimmed();
            if (text.isEmpty())
                continue;
            bool ok = false;
            text.toInt(&ok);
            if (!ok)
                problems.append(QStringLiteral("row %1 (%2): %3 is not a number: %4")
                                    .arg(row + 1)
                                    .arg(cell(row, Title)->text())
                                    .arg(columnLabels().at(column))
                                    .arg(text));
        }
    }
    if (!problems.isEmpty()) {
        showWarning(problems.join('\n'));
        return;
    }

    int failed = 0;
    for (int row = 0; row < m_ids.size(); ++row)
    {
        MediaItem updated = m_originals.at(row);
        bool changed = false;
        for (int column = Title; column <= Year; ++column) {
            const QString text = cell(row, column)->text();
            if (text == m_originalCells.value(row).value(column))
                continue;
            applyValue(updated, column, text);
            changed = true;
        }
        if (changed && !store->updateItem(updated))
            ++failed;
        // The read's art (a re-read that carried a picture): replace the
        // stored art, even when the row's cells were left untouched. A
        // re-read without a picture keeps the old art (art is never
        // cleared by a resync, like the statistics).
        const TrackMeta read = m_readResults.value(m_ids.at(row));
        if (!read.artData.isEmpty()
            && !store->setArtwork(m_ids.at(row), read.artMime, read.artData))
            ++failed;
    }
    if (failed > 0) {
        showWarning(QStringLiteral("Writing %1 item(s) to the database failed")
                        .arg(failed));
        return;
    }

    accept();
}
