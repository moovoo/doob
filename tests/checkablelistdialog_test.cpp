#include <QtTest>

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>

#include <memory>

#include "checkablelistdialog.h"
#include "settingsmanager.h"

namespace {

QList<QPair<QString, QString>> testCatalog()
{
    return {
        {QStringLiteral("Previous"), QStringLiteral("previous")},
        {QStringLiteral("Play / Pause"), QStringLiteral("playpause")},
        {QStringLiteral("Stop"), QStringLiteral("stop")},
        {QStringLiteral("Next"), QStringLiteral("next")},
    };
}

// The dialog is generic; the test drives it with a playback-button-like
// catalog and fixed title/hint.
constexpr const char* kTitle = "Test items";
constexpr const char* kHint = "Checked items are visible; use the arrows to rearrange them.";

class CheckableListDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void visibleItemsComeFirstAndAreChecked();
    void cannotUncheckLastVisibleItem();
    void moveButtonsReorderItems();
    void moveButtonsEnablementFollowsSelection();
    void resetRestoresDefaultSet();
    void selectedOrderMatchesCheckedItems();
    void okButtonAcceptsDialog();
    void cancelButtonRejectsDialog();
    void stretcherButtonsOnlyWithStretchers();
    void insertStretcherAppendsAtEndWithoutSelection();
    void insertStretcherLandsAfterTheSelection();
    void insertStretcherIsCappedAtGaps();
    void removeDeletesOnlyStretchers();
    void stretchersMoveWithTheSelection();
    void sequenceCarriesCheckedItemsAndStretchers();
    void resetDropsStretchers();
    void triStatePopulatesOrderAndStates();
    void triStateSequenceCarriesEveryRow();
    void triStateStatesReturnsRowStates();
    void triStateCannotMakeLastVisibleLineNever();
    void triStateResetRestoresDefaults();
    void binaryDialogStatesAreEmpty();
    void triStateRowsCarryTheTristateFlag();
    void sizeAndPositionArePersistentWithAStateKey();

private:
    QListWidget* list(CheckableListDialog& dlg) const;
    CheckableListDialog stretcherDialog(const QStringList& order) const;

    // A keyed dialog's UiState wiring is write-through to the
    // SettingsManager: keep that store private to this test, so the
    // test does not touch the user's settings.json.
    std::unique_ptr<QTemporaryDir> m_dir;
};

QListWidget* CheckableListDialogTest::list(CheckableListDialog& dlg) const
{
    return dlg.findChild<QListWidget*>("itemList");
}

// The same catalog, stretcher mode on.
CheckableListDialog CheckableListDialogTest::stretcherDialog(const QStringList& order) const
{
    return CheckableListDialog(QString::fromUtf8(kTitle), QString::fromUtf8(kHint),
                               testCatalog(), order, nullptr, true);
}

void CheckableListDialogTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();
}

void CheckableListDialogTest::cleanup()
{
    SettingsManager::reset();
}

void CheckableListDialogTest::visibleItemsComeFirstAndAreChecked()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"next", "previous"});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    QCOMPARE(dlg.windowTitle(), QString::fromUtf8(kTitle));
    QCOMPARE(l->count(), 4);

    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("next"));
    QVERIFY(l->item(0)->checkState() == Qt::Checked);
    QCOMPARE(l->item(1)->data(Qt::UserRole).toString(), QString("previous"));
    QVERIFY(l->item(1)->checkState() == Qt::Checked);
    QCOMPARE(l->item(2)->data(Qt::UserRole).toString(), QString("playpause"));
    QVERIFY(l->item(2)->checkState() == Qt::Unchecked);
    QCOMPARE(l->item(3)->data(Qt::UserRole).toString(), QString("stop"));
    QVERIFY(l->item(3)->checkState() == Qt::Unchecked);
}

void CheckableListDialogTest::cannotUncheckLastVisibleItem()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous"});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    l->item(0)->setCheckState(Qt::Unchecked);

    QVERIFY(l->item(0)->checkState() == Qt::Checked);
}

void CheckableListDialogTest::moveButtonsReorderItems()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous", "playpause", "stop", "next"});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);
    QPushButton* up = dlg.findChild<QPushButton*>("moveUpButton");
    QPushButton* down = dlg.findChild<QPushButton*>("moveDownButton");
    QVERIFY(up != nullptr);
    QVERIFY(down != nullptr);

    // Move "next" (row 3) to the top: next, previous, playpause, stop.
    l->setCurrentRow(3);
    up->click();
    up->click();
    up->click();
    QCOMPARE(l->currentRow(), 0);
    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("next"));
    QCOMPARE(l->item(1)->data(Qt::UserRole).toString(), QString("previous"));
    QCOMPARE(l->item(2)->data(Qt::UserRole).toString(), QString("playpause"));
    QCOMPARE(l->item(3)->data(Qt::UserRole).toString(), QString("stop"));

    // ...and back to the bottom.
    down->click();
    down->click();
    down->click();
    QCOMPARE(l->currentRow(), 3);
    QCOMPARE(l->item(3)->data(Qt::UserRole).toString(), QString("next"));
}

void CheckableListDialogTest::moveButtonsEnablementFollowsSelection()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);
    QPushButton* up = dlg.findChild<QPushButton*>("moveUpButton");
    QPushButton* down = dlg.findChild<QPushButton*>("moveDownButton");
    QVERIFY(up != nullptr);
    QVERIFY(down != nullptr);

    l->setCurrentRow(0);
    QVERIFY(!up->isEnabled());
    QVERIFY(down->isEnabled());

    l->setCurrentRow(3);
    QVERIFY(up->isEnabled());
    QVERIFY(!down->isEnabled());
}

void CheckableListDialogTest::resetRestoresDefaultSet()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"stop"});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);
    QPushButton* reset = dlg.findChild<QPushButton*>("resetButton");
    QVERIFY(reset != nullptr);

    reset->click();

    QCOMPARE(l->count(), 4);
    for (int i = 0; i < l->count(); ++i)
        QVERIFY(l->item(i)->checkState() == Qt::Checked);
    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("previous"));
    QCOMPARE(l->item(3)->data(Qt::UserRole).toString(), QString("next"));
}

void CheckableListDialogTest::selectedOrderMatchesCheckedItems()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous", "playpause", "stop", "next"});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    l->item(2)->setCheckState(Qt::Unchecked); // hide "stop"

    QCOMPARE(dlg.selectedOrder(), (QStringList{"previous", "playpause", "next"}));
}

void CheckableListDialogTest::okButtonAcceptsDialog()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous"});
    QPushButton* ok = dlg.findChild<QPushButton*>("okButton");
    QVERIFY(ok != nullptr);

    ok->click();

    QCOMPARE(dlg.result(), static_cast<int>(QDialog::Accepted));
}

void CheckableListDialogTest::cancelButtonRejectsDialog()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous"});
    QPushButton* cancel = dlg.findChild<QPushButton*>("cancelButton");
    QVERIFY(cancel != nullptr);

    cancel->click();

    QCOMPARE(dlg.result(), static_cast<int>(QDialog::Rejected));
}

void CheckableListDialogTest::stretcherButtonsOnlyWithStretchers()
{
    // The stretcher mode is opt-in: the default dialog is unchanged.
    CheckableListDialog plain(kTitle, kHint, testCatalog(), {"previous"});
    QVERIFY(plain.findChild<QPushButton*>("insertStretcherButton") == nullptr);
    QVERIFY(plain.findChild<QPushButton*>("removeButton") == nullptr);

    CheckableListDialog dlg = stretcherDialog({"previous"});
    QPushButton* insert = dlg.findChild<QPushButton*>("insertStretcherButton");
    QPushButton* remove = dlg.findChild<QPushButton*>("removeButton");
    QVERIFY(insert != nullptr);
    QVERIFY(remove != nullptr);
}

void CheckableListDialogTest::insertStretcherAppendsAtEndWithoutSelection()
{
    CheckableListDialog dlg = stretcherDialog({"previous"});
    QListWidget* l = list(dlg);
    QPushButton* insert = dlg.findChild<QPushButton*>("insertStretcherButton");
    QVERIFY(l != nullptr);
    QVERIFY(insert != nullptr);

    insert->click();

    QCOMPARE(l->count(), 5); // the 4 buttons + the stretcher
    QCOMPARE(l->item(4)->data(Qt::UserRole).toString(), QString("stretch"));
    // A stretcher is not checkable: its row is its presence.
    QVERIFY((l->item(4)->flags() & Qt::ItemIsUserCheckable) == 0);
    // Only the checked buttons ride in the sequence (the hidden ones
    // stay out, the stretcher stays in).
    QCOMPARE(dlg.sequence(), (QStringList{"previous", "stretch"}));
}

void CheckableListDialogTest::insertStretcherLandsAfterTheSelection()
{
    CheckableListDialog dlg = stretcherDialog({"previous", "stop"});
    QListWidget* l = list(dlg);
    QPushButton* insert = dlg.findChild<QPushButton*>("insertStretcherButton");
    QVERIFY(l != nullptr);
    QVERIFY(insert != nullptr);

    l->setCurrentRow(1); // "stop"
    insert->click();

    QCOMPARE(l->count(), 5);
    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("previous"));
    QCOMPARE(l->item(1)->data(Qt::UserRole).toString(), QString("stop"));
    QCOMPARE(l->item(2)->data(Qt::UserRole).toString(), QString("stretch"));
    QCOMPARE(l->item(3)->data(Qt::UserRole).toString(), QString("playpause"));
    QCOMPARE(dlg.sequence(), (QStringList{"previous", "stop", "stretch"}));
}

void CheckableListDialogTest::insertStretcherIsCappedAtGaps()
{
    // The cap is the number of gaps: the catalog size + 1 (5 here).
    CheckableListDialog dlg = stretcherDialog({"previous"});
    QListWidget* l = list(dlg);
    QPushButton* insert = dlg.findChild<QPushButton*>("insertStretcherButton");
    QVERIFY(l != nullptr);
    QVERIFY(insert != nullptr);

    for (int i = 0; i < 5; ++i)
        insert->click();
    QCOMPARE(l->count(), 9); // 4 buttons + 5 stretchers
    QVERIFY(!insert->isEnabled());

    // A remove (of a stretcher row) makes room for another one.
    l->setCurrentRow(4);
    dlg.findChild<QPushButton*>("removeButton")->click();
    QCOMPARE(l->count(), 8);
    QVERIFY(insert->isEnabled());
}

void CheckableListDialogTest::removeDeletesOnlyStretchers()
{
    CheckableListDialog dlg = stretcherDialog({"previous", "stretch"});
    QListWidget* l = list(dlg);
    QPushButton* remove = dlg.findChild<QPushButton*>("removeButton");
    QVERIFY(l != nullptr);
    QVERIFY(remove != nullptr);

    // No selection: the remove is disabled.
    QVERIFY(!remove->isEnabled());

    // A button selection: the remove is disabled (the buttons are
    // removed by unchecking, not here).
    l->setCurrentRow(0);
    QVERIFY(!remove->isEnabled());

    // A stretcher selection: the remove deletes the row.
    l->setCurrentRow(1);
    QVERIFY(remove->isEnabled());
    remove->click();
    QCOMPARE(l->count(), 4);
    QCOMPARE(dlg.sequence(), (QStringList{"previous"}));
}

void CheckableListDialogTest::stretchersMoveWithTheSelection()
{
    CheckableListDialog dlg = stretcherDialog({"previous", "stretch"});
    QListWidget* l = list(dlg);
    QPushButton* up = dlg.findChild<QPushButton*>("moveUpButton");
    QPushButton* down = dlg.findChild<QPushButton*>("moveDownButton");
    QVERIFY(l != nullptr);
    QVERIFY(up != nullptr);
    QVERIFY(down != nullptr);

    // The stretcher (row 1) moves to the top.
    l->setCurrentRow(1);
    up->click();
    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("stretch"));
    QCOMPARE(l->item(1)->data(Qt::UserRole).toString(), QString("previous"));
    QCOMPARE(dlg.sequence(), (QStringList{"stretch", "previous"}));

    // ...and back down past the hidden buttons.
    for (int i = 0; i < 4; ++i)
        down->click();
    QCOMPARE(l->item(4)->data(Qt::UserRole).toString(), QString("stretch"));
}

void CheckableListDialogTest::sequenceCarriesCheckedItemsAndStretchers()
{
    CheckableListDialog dlg = stretcherDialog({"previous", "stretch", "stop"});
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    // selectedOrder() is the checked buttons only (the stretcher is not
    // a checked item); sequence() is the full display order of the
    // checked buttons and the stretchers.
    QCOMPARE(dlg.selectedOrder(), (QStringList{"previous", "stop"}));
    QCOMPARE(dlg.sequence(), (QStringList{"previous", "stretch", "stop"}));

    // Hiding a button drops it from both; the stretcher stays.
    l->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(dlg.selectedOrder(), (QStringList{"stop"}));
    QCOMPARE(dlg.sequence(), (QStringList{"stretch", "stop"}));
}

void CheckableListDialogTest::resetDropsStretchers()
{
    CheckableListDialog dlg = stretcherDialog({"previous", "stretch", "stop"});
    QListWidget* l = list(dlg);
    QPushButton* reset = dlg.findChild<QPushButton*>("resetButton");
    QVERIFY(l != nullptr);
    QVERIFY(reset != nullptr);

    reset->click();

    QCOMPARE(l->count(), 4); // the catalog only: the stretchers are gone
    for (int i = 0; i < l->count(); ++i)
        QVERIFY(l->item(i)->checkState() == Qt::Checked);
    QCOMPARE(dlg.sequence(), (QStringList{"previous", "playpause", "stop", "next"}));
}

void CheckableListDialogTest::triStatePopulatesOrderAndStates()
{
    // The list order is the sequence as given (every key), the check
    // states as given; a missing key is the default (partially checked).
    QHash<QString, Qt::CheckState> states;
    states.insert("previous", Qt::Checked); // always
    states.insert("stop", Qt::Unchecked);   // never
    CheckableListDialog dlg(kTitle, kHint, testCatalog(),
                           {"next", "previous", "playpause", "stop"}, states);
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    QCOMPARE(l->count(), 4);
    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("next"));
    QVERIFY(l->item(0)->checkState() == Qt::PartiallyChecked);
    QCOMPARE(l->item(1)->data(Qt::UserRole).toString(), QString("previous"));
    QVERIFY(l->item(1)->checkState() == Qt::Checked);
    QCOMPARE(l->item(2)->data(Qt::UserRole).toString(), QString("playpause"));
    QVERIFY(l->item(2)->checkState() == Qt::PartiallyChecked);
    QCOMPARE(l->item(3)->data(Qt::UserRole).toString(), QString("stop"));
    QVERIFY(l->item(3)->checkState() == Qt::Unchecked);
}

void CheckableListDialogTest::triStateSequenceCarriesEveryRow()
{
    QHash<QString, Qt::CheckState> states;
    states.insert("previous", Qt::Checked); // always
    states.insert("stop", Qt::Unchecked);   // never
    CheckableListDialog dlg(kTitle, kHint, testCatalog(),
                           {"next", "previous", "playpause", "stop"}, states);

    // The never row stays in the sequence (the state is orthogonal to
    // the order); selectedOrder keeps the binary notion (the checked
    // rows only).
    QCOMPARE(dlg.sequence(),
             (QStringList{"next", "previous", "playpause", "stop"}));
    QCOMPARE(dlg.selectedOrder(), (QStringList{"previous"}));
}

void CheckableListDialogTest::triStateStatesReturnsRowStates()
{
    // An explicit empty states map (a braced initializer would resolve
    // to the binary constructor's QWidget* parameter).
    QHash<QString, Qt::CheckState> states;
    CheckableListDialog dlg(kTitle, kHint, testCatalog(),
                           {"previous", "playpause", "stop", "next"}, states);
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    QCOMPARE(dlg.states().size(), 4);
    QVERIFY(dlg.states().value("previous") == Qt::PartiallyChecked);
    QVERIFY(dlg.states().value("next") == Qt::PartiallyChecked);

    l->item(0)->setCheckState(Qt::Checked);
    l->item(2)->setCheckState(Qt::Unchecked);
    QVERIFY(dlg.states().value("previous") == Qt::Checked);
    QVERIFY(dlg.states().value("stop") == Qt::Unchecked);
    // The untouched rows keep their default.
    QVERIFY(dlg.states().value("playpause") == Qt::PartiallyChecked);
}

void CheckableListDialogTest::triStateCannotMakeLastVisibleLineNever()
{
    // Only "playpause" is visible (partially checked); the rest are
    // already never.
    QHash<QString, Qt::CheckState> states;
    states.insert("previous", Qt::Unchecked);
    states.insert("stop", Qt::Unchecked);
    states.insert("next", Qt::Unchecked);
    CheckableListDialog dlg(kTitle, kHint, testCatalog(),
                           {"previous", "playpause", "stop", "next"}, states);
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    // The last visible row cannot become never (it reverts to the
    // default, when-non-empty).
    l->item(1)->setCheckState(Qt::Unchecked);
    QVERIFY(l->item(1)->checkState() == Qt::PartiallyChecked);

    // ...and it can leave via the other states.
    l->item(1)->setCheckState(Qt::Checked);
    QVERIFY(l->item(1)->checkState() == Qt::Checked);
}

void CheckableListDialogTest::triStateResetRestoresDefaults()
{
    QHash<QString, Qt::CheckState> states;
    states.insert("previous", Qt::Checked);
    states.insert("stop", Qt::Unchecked);
    CheckableListDialog dlg(kTitle, kHint, testCatalog(),
                           {"next", "previous", "playpause", "stop"}, states);
    QListWidget* l = list(dlg);
    QVERIFY(l != nullptr);

    dlg.findChild<QPushButton*>("resetButton")->click();

    // Catalog order, every row the default (partially checked).
    QCOMPARE(l->count(), 4);
    QCOMPARE(l->item(0)->data(Qt::UserRole).toString(), QString("previous"));
    for (int i = 0; i < l->count(); ++i)
        QVERIFY(l->item(i)->checkState() == Qt::PartiallyChecked);
    QCOMPARE(dlg.sequence(),
             (QStringList{"previous", "playpause", "stop", "next"}));
}

void CheckableListDialogTest::binaryDialogStatesAreEmpty()
{
    // The states accessor is a tri-state notion: the binary dialogs
    // (the playback buttons, the peak meter channels) keep none.
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous"});
    QVERIFY(dlg.states().isEmpty());
}

// The tri-state rows carry the tristate flag: the list's plain click
// toggle is binary (it would lose the middle state), and the flag is
// what makes a real click cycle all three states. (The cycle itself
// runs in the QPA mouse path, which the offscreen QTest events
// bypass — the same path a synthesized context menu never reaches —
// so the flag is the testable contract.)
void CheckableListDialogTest::triStateRowsCarryTheTristateFlag()
{
    QHash<QString, Qt::CheckState> states;
    CheckableListDialog tri(kTitle, kHint, testCatalog(),
                           {"previous", "playpause", "stop", "next"}, states);
    QListWidget* t = list(tri);
    QVERIFY(t != nullptr);
    for (int i = 0; i < t->count(); ++i)
        QVERIFY(t->item(i)->flags() & Qt::ItemIsUserTristate);

    // The binary dialogs' rows (the playback buttons, the peak meter
    // channels) keep the plain two-state toggle.
    CheckableListDialog binary(kTitle, kHint, testCatalog(), {"previous"});
    QListWidget* b = list(binary);
    QVERIFY(b != nullptr);
    for (int i = 0; i < b->count(); ++i)
        QVERIFY((b->item(i)->flags() & Qt::ItemIsUserTristate) == 0);
}

// A dialog constructed with a state key tracks its geometry through the
// standard UiState wiring: saved on every hide, restored by a fresh
// dialog of the same key (a dialog without a key persists nothing —
// every other test here constructs without one).
void CheckableListDialogTest::sizeAndPositionArePersistentWithAStateKey()
{
    CheckableListDialog dlg(kTitle, kHint, testCatalog(), {"previous"},
                            nullptr, false, QStringLiteral("dialogs.test"));
    dlg.show();
    dlg.resize(460, 340);
    dlg.move(15, 25);
    dlg.hide(); // the tracking saves the geometry on every hide

    // The state landed in the SettingsManager under the key, in the
    // standard "geometry" shape.
    const QVariantMap saved = SettingsManager::getInstance()
                                  ->value("dialogs.test")
                                  .toMap()
                                  .value("geometry")
                                  .toMap();
    QCOMPARE(saved.value("width").toInt(), 460);
    QCOMPARE(saved.value("height").toInt(), 340);

    // A fresh dialog of the same key restores exactly what the hide
    // saved (the offscreen platform adjusts a move by a couple of
    // pixels, so the contract is the round trip, not the absolute
    // position).
    CheckableListDialog second(kTitle, kHint, testCatalog(), {"previous"},
                               nullptr, false,
                               QStringLiteral("dialogs.test"));
    QCOMPARE(second.geometry(),
             QRect(saved.value("x").toInt(), saved.value("y").toInt(),
                   460, 340));
}
}


QTEST_MAIN(CheckableListDialogTest)

#include "checkablelistdialog_test.moc"
