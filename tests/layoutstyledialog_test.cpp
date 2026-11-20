#include <QtTest/QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPushButton>
#include <QSet>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "layoutmanager.h"
#include "layoutstyledialog.h"
#include "mainwindow.h"
#include "settings.h"
#include "settingsmanager.h"
#include "stylemanager.h"
#include "widgetcontainer.h"
#include "windowmanager.h"

namespace {

// The entry's name in the item's data (the same role the dialog uses).
const int kNameRole = Qt::UserRole;

// The entries of one pane, in display order (the group headers are
// skipped).
QStringList entryNames(QTreeWidget *tree)
{
    QStringList names;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = tree->topLevelItem(i);
        if (top->childCount() == 0) {
            const QVariant data = top->data(0, kNameRole);
            if (data.isValid())
                names.append(data.toString());
        } else {
            for (int j = 0; j < top->childCount(); ++j) {
                const QVariant data = top->child(j)->data(0, kNameRole);
                if (data.isValid())
                    names.append(data.toString());
            }
        }
    }
    return names;
}

// Select an entry the way a click does: through the view's selection
// model (a bare item->setSelected(true) keeps the previous selection —
// it sets the item's flag without asking the model).
void select(QTreeWidget *tree, QTreeWidgetItem *item)
{
    tree->clearSelection();
    item->setSelected(true);
}

// The entry carrying `name` (the same manual scan the dialog uses —
// QTreeWidget::findItems does not match on a data role on this Qt).
QTreeWidgetItem* findEntry(QTreeWidget *tree, const QString &name)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = tree->topLevelItem(i);
        if (item->data(0, kNameRole).toString() == name)
            return item;
        for (int j = 0; j < item->childCount(); ++j) {
            if (item->child(j)->data(0, kNameRole).toString() == name)
                return item->child(j);
        }
    }
    return nullptr;
}

class LayoutStyleDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testListsPresetsAndUserEntries();
    void testFirstRunPreviewAndUse();
    void testFirstRunUseWithoutStyle();
    void testFirstRunStartEmpty();
    void testPickerSelectionSwitches();
    void testPickerSingleInstance();
    void testPickerManagementButtons();

private:
    void setStore(const QString &layoutCurrent, const QString &styleCurrent);
    void writeLayoutPreset(const QString &name, const QJsonObject &doc);
    void writeStylePreset(const QString &name, const QByteArray &text);
    void writeUserLayout(const QString &name, const QJsonObject &doc);
    void writeUserStyle(const QString &name, const QByteArray &text);

    QTemporaryDir m_data;
    QTemporaryDir m_presets;
};

void LayoutStyleDialogTest::initTestCase()
{
    LayoutManager::getInstance()->show();
}

void LayoutStyleDialogTest::init()
{
    setStore(QString(), QString());
}

void LayoutStyleDialogTest::cleanup()
{
    // The picker is single-instance over the manager: drop any open one
    // (a manual delete — the manager's QPointer goes null; the dialog
    // is a child of the main window, not of qApp).
    MainWindow* window = LayoutManager::getInstance()->window();
    if (window != nullptr) {
        const QList<LayoutStyleDialog*> dialogs =
            window->findChildren<LayoutStyleDialog*>();
        for (LayoutStyleDialog* dialog : dialogs)
            delete dialog;
    }
    qApp->processEvents();

    qunsetenv("DOOB_DATA_DIR");
    qunsetenv("DOOB_PRESETS_DIR");
    Settings::reset();
    StyleManager::reset();
    qApp->setStyleSheet(QString());
    // The currents are SettingsManager values (a process-wide
    // singleton): leave them unset so the first-run detection (and the
    // next test's expectations) start clean.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->resetToDefault("layout.current");
    settings->resetToDefault("style.current");
}

void LayoutStyleDialogTest::setStore(const QString &layoutCurrent,
                                     const QString &styleCurrent)
{
    m_data.remove();
    m_data = QTemporaryDir();
    qputenv("DOOB_DATA_DIR", m_data.path().toUtf8());
    m_presets.remove();
    m_presets = QTemporaryDir();
    qputenv("DOOB_PRESETS_DIR", m_presets.path().toUtf8());
    Settings::reset();

    LayoutManager* lm = LayoutManager::getInstance();
    StyleManager* sm = StyleManager::getInstance();
    lm->setCurrentLayout(layoutCurrent);
    sm->setCurrentStyle(styleCurrent);

    // A clean live state: the empty layout, the empty sheet, no child
    // windows.
    WindowManager::getInstance()->clear();
    QJsonObject empty;
    empty["container"] = QJsonObject();
    lm->applyLayoutDocument(empty, false, QStringLiteral("test reset"));
    sm->setStyleSheet(QString());
}

void LayoutStyleDialogTest::writeLayoutPreset(const QString &name, const QJsonObject &doc)
{
    QDir().mkpath(m_presets.path() + "/layouts");
    QFile f(m_presets.path() + "/layouts/" + name + ".json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(doc).toJson());
    f.close();
}

void LayoutStyleDialogTest::writeStylePreset(const QString &name, const QByteArray &text)
{
    QDir().mkpath(m_presets.path() + "/styles");
    QFile f(m_presets.path() + "/styles/" + name + ".qss");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
    f.close();
}

void LayoutStyleDialogTest::writeUserLayout(const QString &name, const QJsonObject &doc)
{
    QDir().mkpath(Settings::instance()->layoutsDir());
    QFile f(Settings::instance()->layoutsDir() + "/" + name + ".json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(doc).toJson());
    f.close();
}

void LayoutStyleDialogTest::writeUserStyle(const QString &name, const QByteArray &text)
{
    QDir().mkpath(Settings::instance()->stylesDir());
    QFile f(Settings::instance()->stylesDir() + "/" + name + ".qss");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
    f.close();
}

// The listing: the presets and the user's files in their groups — the
// user entries carry their provenance and modified hints.
void LayoutStyleDialogTest::testListsPresetsAndUserEntries()
{
    LayoutManager* lm = LayoutManager::getInstance();

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    preset["title"] = QString("Classic");
    writeLayoutPreset("classic", preset);
    writeStylePreset("dark", "QPushButton { background: black; }");

    // A user fork of the classic preset, modified (its container
    // differs) — the listing shows both hints. The fork carries its
    // provenance as the document's "origin" key (writeDocument does).
    QJsonObject fork = preset;
    fork["container"] = QJsonObject{{"type", QString("splitter")}};
    fork["origin"] = QString("preset:classic");
    writeUserLayout("classic.modified", fork);
    writeUserStyle("dark.modified",
                   "/* doob-origin: preset:dark */\nQPushButton { background: blue; }");
    // A plain user style (no origin) — no hints.
    writeUserStyle("mine", "QLabel { color: red; }");

    LayoutStyleDialog dialog(LayoutStyleDialog::Mode::Picker, lm->window());
    dialog.show();
    QApplication::processEvents();

    QTreeWidget* layouts = dialog.findChild<QTreeWidget*>("layoutsTree");
    QTreeWidget* styles = dialog.findChild<QTreeWidget*>("stylesTree");
    QVERIFY(layouts);
    QVERIFY(styles);

    // The user's files and the presets in their groups (the store's
    // title-sorted order, the hints on the user entries). The fork
    // keeps the preset's title, so its position relative to the preset
    // is not asserted.
    QSet<QString> layoutEntries;
    for (const QString &name : entryNames(layouts))
        layoutEntries.insert(name);
    QCOMPARE(layoutEntries,
             (QSet<QString>{QString("classic.modified"), QString("classic")}));
    QTreeWidgetItem* forkEntry = findEntry(layouts, "classic.modified");
    QVERIFY(forkEntry);
    QCOMPARE(forkEntry->text(0),
             QString("Classic (from preset classic) (modified)"));
    QVERIFY(findEntry(layouts, "classic"));

    QCOMPARE(entryNames(styles),
             (QStringList{QString("dark"), QString("dark.modified"),
                          QString("mine")}));
    QTreeWidgetItem* darkFork = findEntry(styles, "dark.modified");
    QVERIFY(darkFork);
    QCOMPARE(darkFork->text(0),
             QString("dark.modified (from preset dark) (modified)"));
    QTreeWidgetItem* mine = findEntry(styles, "mine");
    QVERIFY(mine);
    QCOMPARE(mine->text(0), QString("mine"));

    // Delete is enabled for a user-owned selection, refused for the
    // preset.
    QPushButton* deleteLayout = dialog.findChild<QPushButton*>("deleteLayoutButton");
    QPushButton* deleteStyle = dialog.findChild<QPushButton*>("deleteStyleButton");
    QVERIFY(deleteLayout);
    QVERIFY(deleteStyle);
    select(layouts, forkEntry);
    QApplication::processEvents();
    QVERIFY(deleteLayout->isEnabled());
    select(layouts, findEntry(layouts, "classic"));
    QApplication::processEvents();
    QVERIFY(!deleteLayout->isEnabled());

    dialog.close();
    qApp->processEvents();
}

// The first run: a selection previews live (no saves); Use commits the
// selection — a selected layout preset is taken as a copy (the fork
// becomes current, origin recorded), a selected style preset is used
// as-is (no copy until it is edited), the read-only presets stay
// untouched.
void LayoutStyleDialogTest::testFirstRunPreviewAndUse()
{
    LayoutManager* lm = LayoutManager::getInstance();
    StyleManager* sm = StyleManager::getInstance();

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writeLayoutPreset("classic", preset);
    const QByteArray presetStyle("QPushButton { background: black; }");
    writeStylePreset("dark", presetStyle);

    LayoutStyleDialog dialog(LayoutStyleDialog::Mode::FirstRun, lm->window());
    dialog.show();
    QApplication::processEvents();

    // Nothing is selected (no current): "Use" is grayed out.
    QTreeWidget* layouts = dialog.findChild<QTreeWidget*>("layoutsTree");
    QTreeWidget* styles = dialog.findChild<QTreeWidget*>("stylesTree");
    QPushButton* use = dialog.findChild<QPushButton*>("useButton");
    QVERIFY(layouts);
    QVERIFY(styles);
    QVERIFY(use);
    QVERIFY(layouts->selectedItems().isEmpty());
    QVERIFY(styles->selectedItems().isEmpty());
    QVERIFY(!use->isEnabled());

    // The layout preview: the preset's document is applied live. A
    // layout selection suffices for "Use" (the style is optional —
    // without one, Use starts a fresh "default" style).
    select(layouts, findEntry(layouts, "classic"));
    QApplication::processEvents();
    QCOMPARE(lm->window()->mainContainer()->getWidget()->widgetId(),
             QString("tabs"));
    QVERIFY(use->isEnabled());
    // The style preview: the preset's sheet is applied live.
    select(styles, findEntry(styles, "dark"));
    QApplication::processEvents();
    QCOMPARE(sm->stylesheet(), QString("QPushButton { background: black; }"));
    QVERIFY(use->isEnabled());

    // No preview saves: the user's directories stay empty, the currents
    // stay unset.
    QVERIFY(QDir(Settings::instance()->layoutsDir())
                .entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty());
    QVERIFY(QDir(Settings::instance()->stylesDir())
                .entryList({QStringLiteral("*.qss")}, QDir::Files).isEmpty());
    QCOMPARE(lm->currentLayout(), QString());
    QCOMPARE(sm->currentStyle(), QString());

    // Use: the layout preset is taken as a copy (the fork becomes
    // current), the style preset is used as-is.
    use->click();
    QApplication::processEvents();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    QCOMPARE(lm->originOf("classic.modified"), QString("preset:classic"));
    // The copy is a user file with the preset's content.
    QCOMPARE(lm->layoutDocument("classic.modified").value("container").toObject()
                 .value("type").toString(),
             QString("tabs"));
    // The style preset is current as-is: no copy is taken, the preset
    // is untouched.
    QCOMPARE(sm->currentStyle(), QString("dark"));
    QCOMPARE(sm->stylesheet(), QString::fromUtf8(presetStyle));
    QVERIFY(!QFile::exists(Settings::instance()->stylesDir() + "/dark.modified.qss"));
    QFile p(m_presets.path() + "/styles/dark.qss");
    QVERIFY(p.open(QIODevice::ReadOnly));
    QCOMPARE(p.readAll(), presetStyle);
    p.close();
    // The live state is the selected pair (already previewed).
    QCOMPARE(lm->window()->mainContainer()->getWidget()->widgetId(),
             QString("tabs"));
}

// The first run with a layout selection only: "Use" is enabled for the
// layout alone, and the missing style is covered by a fresh empty
// "default" style (it becomes current alongside the forked layout).
void LayoutStyleDialogTest::testFirstRunUseWithoutStyle()
{
    LayoutManager* lm = LayoutManager::getInstance();
    StyleManager* sm = StyleManager::getInstance();

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writeLayoutPreset("classic", preset);
    writeStylePreset("dark", "QPushButton { background: black; }");

    LayoutStyleDialog dialog(LayoutStyleDialog::Mode::FirstRun, lm->window());
    dialog.show();
    QApplication::processEvents();

    QTreeWidget* layouts = dialog.findChild<QTreeWidget*>("layoutsTree");
    QPushButton* use = dialog.findChild<QPushButton*>("useButton");
    QVERIFY(layouts);
    QVERIFY(use);

    select(layouts, findEntry(layouts, "classic"));
    QApplication::processEvents();
    QVERIFY(use->isEnabled());

    use->click();
    QApplication::processEvents();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));

    // The layout preset is taken as a copy (as with the full pair).
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    QCOMPARE(lm->originOf("classic.modified"), QString("preset:classic"));
    // No style was selected: a fresh empty "default" style is created
    // and becomes current.
    QCOMPARE(sm->currentStyle(), QString("default"));
    QVERIFY(QFile::exists(Settings::instance()->stylesDir() + "/default.qss"));
    QCOMPARE(sm->styleText("default"), QString());
    QCOMPARE(sm->stylesheet(), QString());
}

// The first run's "Start empty": fresh empty layout and style files
// under the name "default" (they become the currents), the live state
// is the empty one, edit mode is forced on.
void LayoutStyleDialogTest::testFirstRunStartEmpty()
{
    LayoutManager* lm = LayoutManager::getInstance();
    StyleManager* sm = StyleManager::getInstance();

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writeLayoutPreset("classic", preset);
    writeStylePreset("dark", "QPushButton { background: black; }");
    // The pre-dialog live state (the from-scratch start clears it).
    QJsonObject live;
    live["container"] = QJsonObject{{"type", QString("splitter")}};
    lm->applyLayoutDocument(live, false, QStringLiteral("pre-dialog"));
    sm->setStyleSheet("QLabel { color: red; }");

    LayoutStyleDialog dialog(LayoutStyleDialog::Mode::FirstRun, lm->window());
    dialog.show();
    QApplication::processEvents();

    dialog.findChild<QPushButton*>("startEmptyButton")->click();
    QApplication::processEvents();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));

    // Fresh empty files under the name "default", current.
    QCOMPARE(lm->currentLayout(), QString("default"));
    QCOMPARE(sm->currentStyle(), QString("default"));
    QVERIFY(QFile::exists(Settings::instance()->layoutsDir() + "/default.json"));
    QVERIFY(QFile::exists(Settings::instance()->stylesDir() + "/default.qss"));
    const QJsonObject doc = lm->layoutDocument("default");
    QVERIFY(doc.value("container").toObject().isEmpty());
    QCOMPARE(sm->styleText("default"), QString());
    // The live state is the empty one, the from-scratch start is edit
    // mode.
    QCOMPARE(lm->window()->mainContainer()->getWidget(), nullptr);
    QCOMPARE(sm->stylesheet(), QString());
    QVERIFY(lm->isEditMode());
}

// The picker: a selection is a live switch — the previous is saved
// with its (diverged) live state, the target is applied and made
// current. A layout preset target is taken as a copy; a style preset
// target is used as-is (no copy until it is edited).
void LayoutStyleDialogTest::testPickerSelectionSwitches()
{
    LayoutManager* lm = LayoutManager::getInstance();
    StyleManager* sm = StyleManager::getInstance();

    QJsonObject presetA;
    presetA["container"] = QJsonObject{{"type", QString("tabs")}};
    QJsonObject presetB;
    presetB["container"] = QJsonObject{{"type", QString("splitter")}};
    writeLayoutPreset("a", presetA);
    writeLayoutPreset("b", presetB);
    const QByteArray presetStyle("QPushButton { background: black; }");
    writeStylePreset("dark", presetStyle);
    // The currents are user entries; the live state diverges from
    // their files (the save on the switch must write the divergence).
    QJsonObject user;
    user["container"] = QJsonObject{{"type", QString("tabs")}};
    writeUserLayout("mine", user);
    lm->setCurrentLayout("mine");
    QJsonObject live;
    live["container"] = QJsonObject{{"type", QString("splitter")}};
    lm->applyLayoutDocument(live, false, QStringLiteral("pre-dialog"));
    writeUserStyle("mine", "QLabel { color: red; }");
    sm->setCurrentStyle("mine");
    sm->setStyleSheet("QLabel { color: green; }");

    LayoutStyleDialog dialog(LayoutStyleDialog::Mode::Picker, lm->window());
    dialog.show();
    QApplication::processEvents();

    QTreeWidget* layouts = dialog.findChild<QTreeWidget*>("layoutsTree");
    QTreeWidget* styles = dialog.findChild<QTreeWidget*>("stylesTree");
    select(layouts, findEntry(layouts, "b"));
    QApplication::processEvents();

    // The layout switch: the preset is taken as a copy (the fork is
    // applied and current), the previous is saved with its (diverged)
    // live state.
    QCOMPARE(lm->currentLayout(), QString("b.modified"));
    QCOMPARE(lm->window()->mainContainer()->getWidget()->widgetId(),
             QString("splitter"));
    const QString fork = Settings::instance()->layoutsDir()
        + "/b.modified.json";
    QVERIFY(QFile::exists(fork));
    QCOMPARE(lm->originOf("b.modified"), QString("preset:b"));
    QFile f(Settings::instance()->layoutsDir() + "/mine.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    QCOMPARE(saved.value("container").toObject().value("type").toString(),
             QString("splitter"));

    // The style switch: the preset is used as-is (its text applied,
    // the preset itself current, no copy), the previous is saved with
    // its (diverged) live sheet.
    select(styles, findEntry(styles, "dark"));
    QApplication::processEvents();
    QCOMPARE(sm->currentStyle(), QString("dark"));
    QCOMPARE(sm->stylesheet(), QString::fromUtf8(presetStyle));
    QVERIFY(!QFile::exists(Settings::instance()->stylesDir()
                               + "/dark.modified.qss"));
    QFile sf(Settings::instance()->stylesDir() + "/mine.qss");
    QVERIFY(sf.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(sf.readAll()), QString("QLabel { color: green; }"));
    sf.close();

    dialog.close();
    qApp->processEvents();
}

// The picker is single-instance over the manager: a second trigger
// raises the open dialog, a new one is created after it is closed.
void LayoutStyleDialogTest::testPickerSingleInstance()
{
    LayoutManager* lm = LayoutManager::getInstance();

    // The picker is a child of the main window (top-level windows are
    // not in qApp's child list, so the lookup goes through the window).
    auto findDialog = [lm] {
        return lm->window()->findChild<LayoutStyleDialog*>();
    };

    lm->showLayoutStyleDialog(0);
    qApp->processEvents();
    LayoutStyleDialog* first = findDialog();
    QVERIFY(first);

    lm->showLayoutStyleDialog(1);
    qApp->processEvents();
    QCOMPARE(findDialog(), first);
    QCOMPARE(lm->window()->findChildren<LayoutStyleDialog*>().size(), 1);

    first->close();
    qApp->processEvents();
    QVERIFY(findDialog() == nullptr);

    lm->showLayoutStyleDialog(0);
    qApp->processEvents();
    LayoutStyleDialog* second = findDialog();
    QVERIFY(second);
    QVERIFY(second != first);

    second->close();
    qApp->processEvents();
}

// The picker's management buttons (the test subclass overrides the
// modal prompts): New creates an empty layout, Copy to forks the live
// state, Delete removes a user layout (confirmed) and repopulates;
// Import is always enabled, Export (like Delete) only for a
// user-owned selection.
class TestDialog : public LayoutStyleDialog
{
public:
    using LayoutStyleDialog::LayoutStyleDialog;
    QStringList prompted;
    QStringList deleted;
    QString nextName;

protected:
    QString promptName(const QString &caption) override
    {
        prompted.append(caption);
        return nextName;
    }
    bool confirmDelete(const QString &title) override
    {
        deleted.append(title);
        return true;
    }
};

void LayoutStyleDialogTest::testPickerManagementButtons()
{
    LayoutManager* lm = LayoutManager::getInstance();
    StyleManager* sm = StyleManager::getInstance();

    QJsonObject preset;
    preset["container"] = QJsonObject{{"type", QString("tabs")}};
    writeLayoutPreset("classic", preset);
    writeStylePreset("dark", "QPushButton { background: black; }");
    writeUserStyle("mine", "QLabel { color: red; }");

    TestDialog dialog(TestDialog::Mode::Picker, lm->window());
    dialog.show();
    QApplication::processEvents();

    QTreeWidget* layouts = dialog.findChild<QTreeWidget*>("layoutsTree");
    QTreeWidget* styles = dialog.findChild<QTreeWidget*>("stylesTree");
    QPushButton* importLayout = dialog.findChild<QPushButton*>("importLayoutButton");
    QPushButton* exportLayout = dialog.findChild<QPushButton*>("exportLayoutButton");
    QPushButton* importStyle = dialog.findChild<QPushButton*>("importStyleButton");
    QPushButton* exportStyle = dialog.findChild<QPushButton*>("exportStyleButton");
    QVERIFY(layouts);
    QVERIFY(styles);
    QVERIFY(importLayout);
    QVERIFY(exportLayout);
    QVERIFY(importStyle);
    QVERIFY(exportStyle);

    // Import is always available; Export is an operation on the
    // user's files (like Delete): grayed out for a preset selection.
    // (The selection is a live switch: the layout preset is taken as
    // a copy, the style preset is used as-is.)
    select(layouts, findEntry(layouts, "classic"));
    QApplication::processEvents();
    QVERIFY(importLayout->isEnabled());
    QVERIFY(!exportLayout->isEnabled());
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    select(styles, findEntry(styles, "dark"));
    QApplication::processEvents();
    QVERIFY(importStyle->isEnabled());
    QVERIFY(!exportStyle->isEnabled());
    QCOMPARE(sm->currentStyle(), QString("dark"));

    // New layout: an empty layout under the prompted name, current.
    dialog.nextName = "fresh";
    dialog.findChild<QPushButton*>("newLayoutButton")->click();
    QApplication::processEvents();
    QVERIFY(QFile::exists(Settings::instance()->layoutsDir() + "/fresh.json"));
    QCOMPARE(lm->currentLayout(), QString("fresh"));
    QVERIFY(findEntry(layouts, "fresh"));
    // The user's selection enables Export (like it enables Delete).
    QVERIFY(exportLayout->isEnabled());

    // Copy to: the live state forked under the prompted name, current.
    QJsonObject live;
    live["container"] = QJsonObject{{"type", QString("splitter")}};
    lm->applyLayoutDocument(live, false, QStringLiteral("pre-copy-to"));
    dialog.nextName = "copy1";
    dialog.findChild<QPushButton*>("copyToLayoutButton")->click();
    QApplication::processEvents();
    QCOMPARE(lm->currentLayout(), QString("copy1"));
    QCOMPARE(lm->layoutDocument("copy1").value("container").toObject()
                 .value("type").toString(),
             QString("splitter"));
    QVERIFY(findEntry(layouts, "copy1"));

    // Delete: the selected user layout is removed (confirmed), the
    // current falls back to the first remaining user file by name.
    select(layouts, findEntry(layouts, "copy1"));
    QApplication::processEvents();
    dialog.findChild<QPushButton*>("deleteLayoutButton")->click();
    QApplication::processEvents();
    QCOMPARE(dialog.deleted, QStringList{QString("copy1")});
    QVERIFY(!QFile::exists(Settings::instance()->layoutsDir() + "/copy1.json"));
    QCOMPARE(lm->currentLayout(), QString("classic.modified"));
    QVERIFY(findEntry(layouts, "copy1") == nullptr);

    // The style side: new (empty sheet) and delete. The user's
    // selection enables Export (it was grayed out for the preset).
    select(styles, findEntry(styles, "mine"));
    QApplication::processEvents();
    QVERIFY(exportStyle->isEnabled());
    dialog.nextName = "plain";
    dialog.findChild<QPushButton*>("newStyleButton")->click();
    QApplication::processEvents();
    QVERIFY(QFile::exists(Settings::instance()->stylesDir() + "/plain.qss"));
    QCOMPARE(sm->currentStyle(), QString("plain"));
    QVERIFY(findEntry(styles, "plain"));

    select(styles, findEntry(styles, "mine"));
    QApplication::processEvents();
    dialog.findChild<QPushButton*>("deleteStyleButton")->click();
    QApplication::processEvents();
    QVERIFY(!QFile::exists(Settings::instance()->stylesDir() + "/mine.qss"));
    QCOMPARE(dialog.prompted,
             (QStringList{QString("New layout"), QString("Copy layout to"),
                          QString("New style")}));
}

}

QTEST_MAIN(LayoutStyleDialogTest)
#include "layoutstyledialog_test.moc"
