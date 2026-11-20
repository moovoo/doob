#include "layoutstyledialog.h"

#include "dialogplumbing.h"
#include "layoutmanager.h"
#include "stylemanager.h"
#include "uistate.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <functional>

namespace {

// The entry's name in the item's data (the group headers carry no
// data, so a selection is always an entry).
const int kNameRole = Qt::UserRole;

// The entry carrying `name` (the tree is at most two levels deep: the
// ungrouped default, and group → entries).
QTreeWidgetItem* findEntry(QTreeWidget *tree, const QString &name)
{
    return dialog::treeItemByData(tree, name, kNameRole);
}

struct Row {
    QString name;
    QString title; // the listing's title (the document's title, the name)
    QString display; // the display text (the title plus the user hints)
    bool readOnly;
};

// The listing text of an entry: the title, plus the user entries'
// provenance and modified hints ("(from preset X)" when the file
// records an origin, "(modified)" when its content has diverged from
// it). `layouts` picks the store (the origin and the marker come
// from it).
QString entryTitle(bool layouts, const QString &name,
                   const QString &title, bool readOnly)
{
    if (readOnly)
        return title;
    const QString origin = layouts
        ? LayoutManager::getInstance()->originOf(name)
        : StyleManager::getInstance()->originOf(name);
    const QLatin1String prefix{"preset:"};
    QString shown = title;
    if (origin.startsWith(prefix))
        shown += QStringLiteral(" (from preset %1)").arg(origin.mid(prefix.size()));
    const bool modified = layouts
        ? LayoutManager::getInstance()->isModified(name)
        : StyleManager::getInstance()->isModified(name);
    if (modified)
        shown += QStringLiteral(" (modified)");
    return shown;
}

// One pane's tree: the two groups — "Presets" and the user group —
// headers only (not selectable), hidden when empty. The rows arrive in
// the stores' sorted order (user files, presets).
void fillTree(QTreeWidget *tree, const QList<Row> &rows,
              const QString &userGroupTitle)
{
    tree->clear();

    auto group = [tree](const QString &title) {
        QTreeWidgetItem *header = new QTreeWidgetItem(tree);
        header->setText(0, title);
        header->setFlags(Qt::ItemIsEnabled); // a header, not an entry
        header->setExpanded(true);
        return header;
    };
    auto entry = [tree](QTreeWidgetItem *parent, const Row &row) {
        QTreeWidgetItem *item = new QTreeWidgetItem(parent);
        item->setText(0, row.display);
        item->setData(0, kNameRole, row.name);
    };

    QTreeWidgetItem *presets = nullptr;
    QTreeWidgetItem *users = nullptr;
    for (const Row &row : rows) {
        if (row.readOnly) {
            if (presets == nullptr)
                presets = group(QStringLiteral("Presets"));
            entry(presets, row);
        } else {
            if (users == nullptr)
                users = group(userGroupTitle);
            entry(users, row);
        }
    }
}

// The pane's object names: the tree and the picker's management
// buttons (one set per store — the tests and the QSS key on them).
struct PaneNames {
    const char *tree;
    const char *newButton;
    const char *copyToButton;
    const char *deleteButton;
    const char *importButton;
    const char *exportButton;
};

const PaneNames kLayoutPaneNames{
    "layoutsTree", "newLayoutButton", "copyToLayoutButton",
    "deleteLayoutButton", "importLayoutButton", "exportLayoutButton",
};
const PaneNames kStylePaneNames{
    "stylesTree", "newStyleButton", "copyToStyleButton",
    "deleteStyleButton", "importStyleButton", "exportStyleButton",
};

// One pane's store operations: the two stores (layouts, styles) share
// the shape — the listing (with the provenance/modified hints), the
// current, the user-entry operations (New / Copy to / Delete), the
// file-dialog operations (Import / Export) and the effect of a
// selection (the first run's live preview, the picker's live switch).
// The pane's code works against this; the managers stay behind it.
struct PaneOps {
    // The listing's user-group title ("Your layouts") and the
    // prompts' noun ("layout" — "New layout", "Copy layout to").
    QString userGroupTitle;
    QString noun;
    std::function<QList<Row>()> list;
    std::function<QString()> current;
    std::function<bool(const QString &)> isPreset;
    std::function<bool(const QString &)> create;
    std::function<bool(const QString &)> saveAs;
    std::function<bool(const QString &)> remove;
    std::function<bool()> import;
    std::function<bool()> exportTo;
    std::function<void(const QString &)> preview;
    std::function<void(const QString &)> switchTo;

    // The listing's title of `name` (the delete confirmation's text;
    // the name when the listing has none for it).
    QString title(const QString &name) const
    {
        for (const Row &row : list())
            if (row.name == name)
                return row.title;
        return name;
    }
};

// The store behind `pane` (the pane's operations, above).
PaneOps paneOps(LayoutStyleDialog::Store pane)
{
    PaneOps ops;
    if (pane == LayoutStyleDialog::Store::Layout) {
        LayoutManager *lm = LayoutManager::getInstance();
        ops.userGroupTitle = QStringLiteral("Your layouts");
        ops.noun = QStringLiteral("layout");
        ops.list = [lm] {
            QList<Row> rows;
            for (const LayoutManager::LayoutInfo &info : lm->layouts())
                rows.append(Row{info.name, info.title,
                                 entryTitle(true, info.name, info.title,
                                            info.readOnly),
                                 info.readOnly,});
            return rows;
        };
        ops.current = [lm] { return lm->currentLayout(); };
        ops.isPreset = [lm](const QString &name) { return lm->isPreset(name); };
        ops.create = [lm](const QString &name) { return lm->newLayout(name); };
        ops.saveAs = [lm](const QString &name) { return lm->saveLayoutAs(name); };
        ops.remove = [lm](const QString &name) { return lm->deleteLayout(name); };
        ops.import = [lm] { return lm->importLayout(); };
        ops.exportTo = [lm] { return lm->exportLayout(); };
        ops.preview = [lm](const QString &name) {
            // The preview is live (no saves): the selected document is
            // applied.
            lm->applyLayoutDocument(lm->layoutDocument(name), false, name);
        };
        ops.switchTo = [lm](const QString &name) {
            // The live switch: the store saves the previous layout (a
            // preset target is taken as a copy).
            lm->switchLayout(name);
        };
    } else {
        StyleManager *sm = StyleManager::getInstance();
        LayoutManager *lm = LayoutManager::getInstance();
        ops.userGroupTitle = QStringLiteral("Your styles");
        ops.noun = QStringLiteral("style");
        ops.list = [sm] {
            QList<Row> rows;
            for (const StyleManager::Info &info : sm->styles())
                rows.append(Row{info.name, info.title,
                                 entryTitle(false, info.name, info.title,
                                            info.readOnly),
                                 info.readOnly,});
            return rows;
        };
        ops.current = [sm] { return sm->currentStyle(); };
        ops.isPreset = [sm](const QString &name) { return sm->isPreset(name); };
        ops.create = [sm](const QString &name) { return sm->newStyle(name); };
        ops.saveAs = [sm](const QString &name) { return sm->saveStyleAs(name); };
        ops.remove = [sm](const QString &name) { return sm->deleteStyle(name); };
        // The style's file-dialog runners live with the layout's
        // (LayoutManager::importStyle / exportStyle).
        ops.import = [lm] { return lm->importStyle(); };
        ops.exportTo = [lm] { return lm->exportStyle(); };
        ops.preview = [sm](const QString &name) {
            // The preview is live (no saves): the selected text is
            // applied (the provenance comment of a user file stays in
            // the file).
            sm->setStyleSheet(sm->liveText(name));
        };
        ops.switchTo = [sm](const QString &name) {
            // The live switch: the store saves the previous style.
            sm->switchStyle(name);
        };
    }
    return ops;
}

// A pane: the tree, and (in Picker mode) the management row — the
// store's operations on the user's entries. The pane's title is the
// tab's label (the panes are tabs), so the pane itself carries no
// header label.
QTreeWidget* buildPane(QWidget *pane, const PaneNames &names,
                       QPushButton **deleteButton, QPushButton **exportButton)
{
    QVBoxLayout *box = new QVBoxLayout(pane);
    box->setContentsMargins(0, 0, 0, 0);

    QTreeWidget *tree = new QTreeWidget;
    tree->setObjectName(QString::fromLatin1(names.tree));
    tree->setHeaderHidden(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    box->addWidget(tree);

    if (deleteButton != nullptr) {
        QHBoxLayout *buttons = new QHBoxLayout;
        box->addLayout(buttons);
        auto addButton = [buttons, pane](const QString &text,
                                         const char *objectName) {
            QPushButton *button = new QPushButton(text, pane);
            button->setObjectName(QString::fromLatin1(objectName));
            buttons->addWidget(button);
            return button;
        };
        buttons->addStretch(1);
        addButton(QStringLiteral("New"), names.newButton);
        addButton(QStringLiteral("Copy to"), names.copyToButton);
        *deleteButton = addButton(QStringLiteral("Delete"), names.deleteButton);
        addButton(QStringLiteral("Import"), names.importButton);
        *exportButton = addButton(QStringLiteral("Export"), names.exportButton);
    }
    return tree;
}

} // namespace

LayoutStyleDialog::LayoutStyleDialog(Mode mode, QWidget *parent)
    : QDialog(parent)
    , m_mode(mode)
{
    setWindowTitle(mode == Mode::FirstRun
        ? QStringLiteral("Choose a layout and a style")
        : QStringLiteral("Layouts and styles"));

    QVBoxLayout *root = new QVBoxLayout(this);

    // The two panes are tabs (the layout's tree + its management row,
    // the style's tree + its management row). The tab's label is the
    // pane's title.
    QTabWidget *tabs = new QTabWidget(this);
    tabs->setObjectName("storeTabs");
    QWidget *layoutPane = new QWidget(this);
    QWidget *stylePane = new QWidget(this);
    m_layouts = buildPane(layoutPane, kLayoutPaneNames,
                          m_mode == Mode::Picker ? &m_deleteLayout : nullptr,
                          m_mode == Mode::Picker ? &m_exportLayout : nullptr);
    m_styles = buildPane(stylePane, kStylePaneNames,
                         m_mode == Mode::Picker ? &m_deleteStyle : nullptr,
                         m_mode == Mode::Picker ? &m_exportStyle : nullptr);
    tabs->addTab(layoutPane, QStringLiteral("Layout"));
    tabs->addTab(stylePane, QStringLiteral("Style"));
    m_tabs = tabs;
    root->addWidget(tabs);

    if (m_mode == Mode::FirstRun) {
        // The first run's choices: commit the selection (a selected
        // layout preset is taken as a copy; a style preset is used
        // as-is — its copy appears only when it is edited), or start
        // from scratch — fresh empty files under the name "default".
        // There is no Cancel: the start is resolved one way or the
        // other.
        QHBoxLayout *actions = new QHBoxLayout;
        root->addLayout(actions);
        actions->addStretch(1);
        QPushButton *startEmpty = new QPushButton(QStringLiteral("Start empty"), this);
        startEmpty->setObjectName("startEmptyButton");
        m_use = new QPushButton(QStringLiteral("Use"), this);
        m_use->setObjectName("useButton");
        actions->addWidget(startEmpty);
        actions->addWidget(m_use);
        connect(startEmpty, &QPushButton::clicked, this, &LayoutStyleDialog::onStartEmptyClicked);
        connect(m_use, &QPushButton::clicked, this, &LayoutStyleDialog::onUseClicked);
    } else {
        // The picker's management buttons: New/Copy to/Delete act on
        // the selection, Import/Export go through the manager's file
        // dialogs. The two panes share the handlers — the pane's
        // store is the argument.
        struct Button {
            const char *objectName;
            Store pane;
            void (LayoutStyleDialog::*handler)(Store);
        };
        const std::array<Button, 10> buttons = { {
            {"newLayoutButton", Store::Layout, &LayoutStyleDialog::onNewClicked},
            {"copyToLayoutButton", Store::Layout, &LayoutStyleDialog::onCopyToClicked},
            {"deleteLayoutButton", Store::Layout, &LayoutStyleDialog::onDeleteClicked},
            {"importLayoutButton", Store::Layout, &LayoutStyleDialog::onImportClicked},
            {"exportLayoutButton", Store::Layout, &LayoutStyleDialog::onExportClicked},
            {"newStyleButton", Store::Style, &LayoutStyleDialog::onNewClicked},
            {"copyToStyleButton", Store::Style, &LayoutStyleDialog::onCopyToClicked},
            {"deleteStyleButton", Store::Style, &LayoutStyleDialog::onDeleteClicked},
            {"importStyleButton", Store::Style, &LayoutStyleDialog::onImportClicked},
            {"exportStyleButton", Store::Style, &LayoutStyleDialog::onExportClicked},
        } ,
        };
        for (const Button &button : buttons) {
            const Store pane = button.pane;
            const auto handler = button.handler;
            connect(findChild<QPushButton*>(button.objectName),
                    &QPushButton::clicked, this,
                    [this, pane, handler] { (this->*handler)(pane); });
        }
    }

    connect(m_layouts, &QTreeWidget::itemSelectionChanged, this,
            [this] { onSelectionChanged(Store::Layout); });
    connect(m_styles, &QTreeWidget::itemSelectionChanged, this,
            [this] { onSelectionChanged(Store::Style); });

    populate();

    // The dialog's own state: its size and position (saved on every
    // hide). The selection is the stores' business (the currents).
    m_state =
        dialog::dialogUiState(this, QStringLiteral("dialogs.layout_style"));
}

void LayoutStyleDialog::showTab(Store store)
{
    // Picker mode only (FirstRun has no tabs — it is a single view
    // with the shared action row).
    if (m_tabs == nullptr)
        return;
    m_tabs->setCurrentIndex(store == Store::Layout ? 0 : 1);
}

void LayoutStyleDialog::populate()
{
    struct Pane {
        QTreeWidget *tree;
        PaneOps ops;
    };
    const std::array<Pane, 2> panes = { {
        {m_layouts, paneOps(Store::Layout)},
        {m_styles, paneOps(Store::Style)},
    } ,
    };
    for (const Pane &pane : panes) {
        QSignalBlocker block(pane.tree);
        fillTree(pane.tree, pane.ops.list(), pane.ops.userGroupTitle);
        // Keep the selection on the current entry (an unset current
        // selects nothing — the "Use" button stays grayed out).
        selectName(pane.tree, pane.ops.current());
    }
    updateUseState();
    updateManageState();
}

void LayoutStyleDialog::updateUseState()
{
    // "Use" commits the selection: enabled for a layout selection (the
    // style is optional — without one, "Use" starts a fresh "default"
    // style; the dialog with no entries at all stays on "Start
    // empty").
    if (m_use != nullptr)
        m_use->setEnabled(!selectedName(m_layouts).isEmpty());
}

void LayoutStyleDialog::updateManageState()
{
    // Delete and Export are operations on the user's files: enabled
    // only for a user-owned selection (a preset is read-only; import
    // is always available).
    struct Pane {
        QTreeWidget *tree;
        QPushButton *deleteButton;
        QPushButton *exportButton;
        Store store;
    };
    const std::array<Pane, 2> panes = { {
        {m_layouts, m_deleteLayout, m_exportLayout, Store::Layout},
        {m_styles, m_deleteStyle, m_exportStyle, Store::Style},
    } ,
    };
    for (const Pane &pane : panes) {
        if (pane.deleteButton == nullptr)
            continue; // FirstRun: no management row
        const PaneOps ops = paneOps(pane.store);
        const QString name = selectedName(pane.tree);
        const bool userOwned = !name.isEmpty() && !ops.isPreset(name);
        pane.deleteButton->setEnabled(userOwned);
        pane.exportButton->setEnabled(userOwned);
    }
}

QString LayoutStyleDialog::selectedName(QTreeWidget *tree) const
{
    const QList<QTreeWidgetItem*> selected = tree->selectedItems();
    if (selected.isEmpty())
        return {};
    const QVariant data = selected.first()->data(0, kNameRole);
    return data.isValid() ? data.toString() : QString();
}

void LayoutStyleDialog::selectName(QTreeWidget *tree, const QString &name)
{
    QTreeWidgetItem *item = findEntry(tree, name);
    if (item != nullptr) {
        tree->clearSelection();
        item->setSelected(true);
    }
}

void LayoutStyleDialog::onNewClicked(Store pane)
{
    const PaneOps ops = paneOps(pane);
    const QString name = promptName(QStringLiteral("New %1").arg(ops.noun));
    if (name.isEmpty())
        return;
    if (ops.create(name))
        populate();
}

void LayoutStyleDialog::onCopyToClicked(Store pane)
{
    const PaneOps ops = paneOps(pane);
    const QString name = promptName(QStringLiteral("Copy %1 to").arg(ops.noun));
    if (name.isEmpty())
        return;
    if (ops.saveAs(name))
        populate();
}

void LayoutStyleDialog::onDeleteClicked(Store pane)
{
    const PaneOps ops = paneOps(pane);
    QTreeWidget *tree = pane == Store::Layout ? m_layouts : m_styles;
    const QString name = selectedName(tree);
    if (name.isEmpty() || ops.isPreset(name))
        return;
    if (!confirmDelete(ops.title(name)))
        return;
    if (ops.remove(name))
        populate();
}

void LayoutStyleDialog::onImportClicked(Store pane)
{
    // The manager runs the file dialog: the import lands in the
    // user's directory under the picked file's name and becomes
    // current — the listing follows.
    if (paneOps(pane).import())
        populate();
}

void LayoutStyleDialog::onExportClicked(Store pane)
{
    // The manager runs the file dialog: the live entry (the selected
    // entry is the current) under the picked file. The store is
    // untouched — no repopulation.
    paneOps(pane).exportTo();
}

void LayoutStyleDialog::onSelectionChanged(Store pane)
{
    QTreeWidget *tree = pane == Store::Layout ? m_layouts : m_styles;
    const QString name = selectedName(tree);
    updateUseState();
    updateManageState();
    if (name.isEmpty())
        return;
    const PaneOps ops = paneOps(pane);
    if (m_mode == Mode::FirstRun)
        ops.preview(name);
    else
        ops.switchTo(name);
}

void LayoutStyleDialog::onUseClicked()
{
    // The live state is already the selected pair (the previews): the
    // currents follow. A selected layout preset is committed as a copy
    // (the fork becomes current — the preview is discarded, so nothing
    // is saved); a selected style preset is used as-is (the copy
    // appears only when it is edited — the style's copy-on-write); a
    // user entry is taken as-is.
    LayoutManager *lm = LayoutManager::getInstance();
    StyleManager *sm = StyleManager::getInstance();
    const QString layoutName = selectedName(m_layouts);
    if (lm->isPreset(layoutName))
        lm->forkLayout(layoutName);
    else
        lm->setCurrentLayout(layoutName);
    if (!selectedName(m_styles).isEmpty())
        sm->setCurrentStyle(selectedName(m_styles));
    else
        sm->newStyle(QStringLiteral("default"));
    accept();
}

void LayoutStyleDialog::onStartEmptyClicked()
{
    // The from-scratch start: fresh empty layout and style files under
    // the conventional name "default" (they become the currents), edit
    // mode on.
    LayoutManager *lm = LayoutManager::getInstance();
    StyleManager *sm = StyleManager::getInstance();
    lm->newLayout(QStringLiteral("default"));
    sm->newStyle(QStringLiteral("default"));
    lm->setEditMode(true);
    accept();
}

QString LayoutStyleDialog::promptName(const QString &caption)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, caption, QStringLiteral("Name:"),
                                               QLineEdit::Normal, QString(), &ok);
    return ok ? name : QString();
}

bool LayoutStyleDialog::confirmDelete(const QString &title)
{
    return QMessageBox::question(
               this, QStringLiteral("Delete"),
               QStringLiteral("Delete “%1”?").arg(title),
               QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}
