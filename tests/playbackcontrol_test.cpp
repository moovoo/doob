#include <QtTest>
#include <QFile>

#include <QApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSize>
#include <QTimer>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <memory>

#include "layoutmanager.h"
#include "playbackcontrol.h"
#include "settingsmanager.h"
#include "testsupport.h"
#include "widgetconfiguration.h"

namespace {

// QIcon::operator== is unreliable for a freshly constructed icon (the
// pixmap cache is still empty), so compare the rendered images instead.
QImage iconImage(const QIcon &icon)
{
    return icon.pixmap(QSize(24, 24)).toImage();
}

class PlaybackControlTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void emptyPlaylistDisablesButtons();
    void itemsEnableButtons();
    void playPauseToggles();
    void stopClickStopsPlayback();
    void nextClickAdvancesAndUpdatesPlayingItem();
    void previousClickRestartsWhenPastThreeSeconds();
    void previousClickGoesBackWhenUnderThreeSeconds();
    void noCurrentTrackLabel();
    void buttonsFillRowInOrder();
    void registeredInLayoutManager();
    void buttonsUseSvgIcons();
    void buttonHeightStableAcrossPlayPause();
    void contextMenuOffersButtonsAction();
    void shuffleButtonTogglesShuffle();
    void repeatButtonCyclesModes();
    void widgetFollowsManagerModes();
    void newWidgetPicksUpCurrentModeState();
    void modeButtonsIgnorePlaylistState();
    void stretchPoliciesDefaultOn();
    void stretchTogglesUpdatePolicies();
    void stretchStatePersists();
    void stretchOffKeepsNaturalButtonSizes();
    void orientationDefaultHorizontal();
    void contextMenuSwitchesOrientation();
    void orientationPersists();
    void restoreWidgetAppliesStretchers();
    void stretcherConfigurationSurvivesJsonRoundtrip();
    void storeWidgetListsVisibleButtonsInOrder();
    void restoreWidgetAppliesVisibilityAndOrder();
    void restoreWidgetIgnoresUnknownAndDuplicateKeys();
    void restoreWidgetFallsBackToDefaultWhenNoValidButtons();
    void buttonConfigurationSurvivesJsonRoundtrip();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void fillPlaylist(int n);
    QPushButton* button(const QString &name) const;
    QAction* findAction(QMenu* menu, const QString& text) const;

    FakePlayer m_fake{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    // Both managers are write-through: keep their database private to this
    // test (a shared default-path file would collide on the explicit ids).
    std::unique_ptr<QTemporaryDir> m_dir;
    PlaybackControlWidget* m_widget = nullptr;
};

void PlaybackControlTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);

    // The shuffle/repeat buttons write through to the SettingsManager:
    // keep that store private too, so the test does not touch the
    // user's settings.json.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_widget = new PlaybackControlWidget();
    m_widget->show();
}

void PlaybackControlTest::cleanup()
{
    delete m_widget;
    m_pm->stop();
    m_pm->setShuffle(false);
    m_pm->setRepeat(PlaybackManager::RepeatMode::NoRepeat);
    m_pl->reset();
    m_store->clear();
    SettingsManager::reset();
}

// Builds the item and registers it in the standalone item store, so
// currentItem() can resolve it.
MediaItem PlaybackControlTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    // A real (dummy) file: playback is checked against the disk.
    const QString path = "/tmp/" + title + ".mp3";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    item.source = QUrl::fromLocalFile(path);
    m_store->remove(id);
    m_store->insert(item);
    return item;
}

void PlaybackControlTest::fillPlaylist(int n)
{
    QList<quint64> ids;
    for (int i = 1; i <= n; ++i)
        ids.append(makeItem(i, QString("Track%1").arg(i)).id);
    m_pl->addItems(ids);
}

QPushButton* PlaybackControlTest::button(const QString &name) const
{
    return m_widget->findChild<QPushButton*>(name);
}

QAction* PlaybackControlTest::findAction(QMenu* menu, const QString& text) const
{
    for (QAction* action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

void PlaybackControlTest::emptyPlaylistDisablesButtons()
{
    QVERIFY(!button("previousButton")->isEnabled());
    QVERIFY(!button("playPauseButton")->isEnabled());
    QVERIFY(!button("stopButton")->isEnabled());
    QVERIFY(!button("nextButton")->isEnabled());
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackControlTest::itemsEnableButtons()
{
    fillPlaylist(2);

    QVERIFY(button("previousButton")->isEnabled());
    QVERIFY(button("playPauseButton")->isEnabled());
    QVERIFY(button("nextButton")->isEnabled());
    QVERIFY(!button("stopButton")->isEnabled());
}

void PlaybackControlTest::playPauseToggles()
{
    fillPlaylist(2);

    button("playPauseButton")->click();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(iconImage(button("playPauseButton")->icon()), iconImage(QIcon(":/icons/pause.svg")));
    QVERIFY(button("stopButton")->isEnabled());

    button("playPauseButton")->click();
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);
    QCOMPARE(iconImage(button("playPauseButton")->icon()), iconImage(QIcon(":/icons/play.svg")));
}

void PlaybackControlTest::stopClickStopsPlayback()
{
    fillPlaylist(2);
    button("playPauseButton")->click();
    QCOMPARE(m_pm->playingItem().title, QString("Track1"));

    button("stopButton")->click();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
    QVERIFY(!button("stopButton")->isEnabled());
}

void PlaybackControlTest::nextClickAdvancesAndUpdatesPlayingItem()
{
    fillPlaylist(2);
    button("playPauseButton")->click();
    QCOMPARE(m_pm->playingItem().title, QString("Track1"));

    button("nextButton")->click();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().title, QString("Track2"));
}

void PlaybackControlTest::previousClickRestartsWhenPastThreeSeconds()
{
    fillPlaylist(2);
    m_pl->setCurrentIndex(1);
    QCOMPARE(m_pm->playingItem().title, QString("Track2"));
    m_fake.m_position = 4000;

    button("previousButton")->click();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_playCount, 2);
}

void PlaybackControlTest::previousClickGoesBackWhenUnderThreeSeconds()
{
    fillPlaylist(2);
    m_pl->setCurrentIndex(1);
    m_fake.m_position = 1000;

    button("previousButton")->click();

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_pm->playingItem().title, QString("Track1"));
}

void PlaybackControlTest::noCurrentTrackLabel()
{
    QVERIFY(m_widget->findChild<QLabel*>("currentLabel") == nullptr);
    QCOMPARE(m_widget->findChildren<QLabel*>().size(), 0);
}

void PlaybackControlTest::buttonsFillRowInOrder()
{
    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(m_widget->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }

    // The buttons fill the row in the default order (no spacers).
    QCOMPARE(layout->count(), 6);
    QCOMPARE(layout->itemAt(0)->widget(), button("previousButton"));
    QCOMPARE(layout->itemAt(1)->widget(), button("playPauseButton"));
    QCOMPARE(layout->itemAt(2)->widget(), button("stopButton"));
    QCOMPARE(layout->itemAt(3)->widget(), button("nextButton"));
    QCOMPARE(layout->itemAt(4)->widget(), button("shuffleButton"));
    QCOMPARE(layout->itemAt(5)->widget(), button("repeatButton"));
}

void PlaybackControlTest::buttonsUseSvgIcons()
{
    // Icon-only buttons: no unicode captions.
    for (const QString &name : { "previousButton", "playPauseButton",
                                 "stopButton", "nextButton",
                                 "shuffleButton", "repeatButton", })
        QVERIFY(button(name)->text().isEmpty());

    QCOMPARE(iconImage(button("previousButton")->icon()), iconImage(QIcon(":/icons/previous.svg")));
    QCOMPARE(iconImage(button("playPauseButton")->icon()), iconImage(QIcon(":/icons/play.svg"))); // stopped
    QCOMPARE(iconImage(button("stopButton")->icon()), iconImage(QIcon(":/icons/stop.svg")));
    QCOMPARE(iconImage(button("nextButton")->icon()), iconImage(QIcon(":/icons/next.svg")));
    QCOMPARE(iconImage(button("shuffleButton")->icon()), iconImage(QIcon(":/icons/shuffle-off.svg")));
    QCOMPARE(iconImage(button("repeatButton")->icon()), iconImage(QIcon(":/icons/repeat-off.svg")));
}

void PlaybackControlTest::buttonHeightStableAcrossPlayPause()
{
    fillPlaylist(2);
    QCoreApplication::processEvents();

    // The icon-only buttons share one 24x24 icon geometry, so the layout
    // gives them all the same size.
    const int h = button("playPauseButton")->height();
    QCOMPARE(button("previousButton")->height(), h);
    QCOMPARE(button("stopButton")->height(), h);
    QCOMPARE(button("nextButton")->height(), h);

    // The play/pause icon swap must not resize the bar.
    button("playPauseButton")->click();
    QCoreApplication::processEvents();
    QCOMPARE(iconImage(button("playPauseButton")->icon()), iconImage(QIcon(":/icons/pause.svg")));
    QCOMPARE(button("playPauseButton")->height(), h);
    QCOMPARE(button("previousButton")->height(), h);

    // Back to the play icon: still stable.
    button("stopButton")->click();
    QCoreApplication::processEvents();
    QCOMPARE(iconImage(button("playPauseButton")->icon()), iconImage(QIcon(":/icons/play.svg")));
    QCOMPARE(button("playPauseButton")->height(), h);
}

void PlaybackControlTest::contextMenuOffersButtonsAction()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* action = nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == "Buttons...")
            action = a;
    }
    QVERIFY(action != nullptr);

    bool dialogOpened = false;
    QTimer::singleShot(0, m_widget, [&dialogOpened] {
        QWidget* dlg = QApplication::activeModalWidget();
        if (dlg == nullptr)
            return;
        dialogOpened = true;
        qobject_cast<QDialog*>(dlg)->accept();
    });

    action->trigger();

    QVERIFY(dialogOpened);
    menu->deleteLater();
}

void PlaybackControlTest::shuffleButtonTogglesShuffle()
{
    QPushButton* shuffle = button("shuffleButton");

    shuffle->click();
    QVERIFY(m_pm->shuffle());
    QVERIFY(shuffle->isChecked());
    QCOMPARE(iconImage(shuffle->icon()), iconImage(QIcon(":/icons/shuffle.svg")));
    QCOMPARE(shuffle->toolTip(), QStringLiteral("Shuffle on (click to turn off)"));

    shuffle->click();
    QVERIFY(!m_pm->shuffle());
    QVERIFY(!shuffle->isChecked());
    QCOMPARE(iconImage(shuffle->icon()), iconImage(QIcon(":/icons/shuffle-off.svg")));
}

void PlaybackControlTest::repeatButtonCyclesModes()
{
    QPushButton* repeat = button("repeatButton");

    repeat->click();  // off -> all
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatPlaylist);
    QCOMPARE(iconImage(repeat->icon()), iconImage(QIcon(":/icons/repeat.svg")));
    QCOMPARE(repeat->toolTip(), QStringLiteral("Repeat all (click for repeat one)"));

    repeat->click();  // all -> one
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatTrack);
    QCOMPARE(iconImage(repeat->icon()), iconImage(QIcon(":/icons/repeat-one.svg")));
    QCOMPARE(repeat->toolTip(), QStringLiteral("Repeat one (click for repeat off)"));

    repeat->click();  // one -> off
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::NoRepeat);
    QCOMPARE(iconImage(repeat->icon()), iconImage(QIcon(":/icons/repeat-off.svg")));
    QCOMPARE(repeat->toolTip(), QStringLiteral("Repeat off (click for repeat all)"));
}

void PlaybackControlTest::widgetFollowsManagerModes()
{
    QPushButton* shuffle = button("shuffleButton");
    QPushButton* repeat = button("repeatButton");
    QVERIFY(!shuffle->isChecked());

    // A change that originates in the manager (not the button) must
    // reach the widget: the buttons reflect the new state.
    m_pm->setShuffle(true);
    QVERIFY(shuffle->isChecked());
    QCOMPARE(iconImage(shuffle->icon()), iconImage(QIcon(":/icons/shuffle.svg")));

    m_pm->setShuffle(false);
    QVERIFY(!shuffle->isChecked());
    QCOMPARE(iconImage(shuffle->icon()), iconImage(QIcon(":/icons/shuffle-off.svg")));

    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatTrack);
    QCOMPARE(iconImage(repeat->icon()), iconImage(QIcon(":/icons/repeat-one.svg")));

    m_pm->setRepeat(PlaybackManager::RepeatMode::NoRepeat);
    QCOMPARE(iconImage(repeat->icon()), iconImage(QIcon(":/icons/repeat-off.svg")));
}

void PlaybackControlTest::newWidgetPicksUpCurrentModeState()
{
    // The state was set before this widget existed: the constructor
    // must sync with the manager's current state.
    m_pm->setShuffle(true);
    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatTrack);

    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(PlaybackControlWidget::id, cfg);
    QVERIFY(w != nullptr);

    QPushButton* shuffle = w->findChild<QPushButton*>("shuffleButton");
    QPushButton* repeat = w->findChild<QPushButton*>("repeatButton");
    QVERIFY(shuffle != nullptr);
    QVERIFY(repeat != nullptr);
    QVERIFY(shuffle->isChecked());
    QCOMPARE(iconImage(shuffle->icon()), iconImage(QIcon(":/icons/shuffle.svg")));
    QCOMPARE(iconImage(repeat->icon()), iconImage(QIcon(":/icons/repeat-one.svg")));

    delete w;
}

void PlaybackControlTest::modeButtonsIgnorePlaylistState()
{
    // The mode buttons control the playback mode, not the current track:
    // an empty playlist disables the transport, not the modes.
    QVERIFY(button("shuffleButton")->isEnabled());
    QVERIFY(button("repeatButton")->isEnabled());

    fillPlaylist(2);
    QVERIFY(button("shuffleButton")->isEnabled());
    QVERIFY(button("repeatButton")->isEnabled());
}

void PlaybackControlTest::stretchPoliciesDefaultOn()
{
    QVERIFY(m_widget->stretchHorizontal());
    QVERIFY(m_widget->stretchVertical());
    for (const QString &name : { "previousButton", "playPauseButton",
                                 "stopButton", "nextButton",
                                 "shuffleButton", "repeatButton", }) {
        const QSizePolicy policy = button(name)->sizePolicy();
        QCOMPARE(policy.horizontalPolicy(), QSizePolicy::Expanding);
        QCOMPARE(policy.verticalPolicy(), QSizePolicy::Expanding);
    }
}

void PlaybackControlTest::stretchTogglesUpdatePolicies()
{
    m_widget->setStretchHorizontal(false);
    QVERIFY(!m_widget->stretchHorizontal());
    QCOMPARE(button("playPauseButton")->sizePolicy().horizontalPolicy(),
             QSizePolicy::Fixed);
    QCOMPARE(button("playPauseButton")->sizePolicy().verticalPolicy(),
             QSizePolicy::Expanding);

    m_widget->setStretchVertical(false);
    QVERIFY(!m_widget->stretchVertical());
    QCOMPARE(button("playPauseButton")->sizePolicy().horizontalPolicy(),
             QSizePolicy::Fixed);
    QCOMPARE(button("playPauseButton")->sizePolicy().verticalPolicy(),
             QSizePolicy::Fixed);

    // Back to the defaults: the buttons expand on both axes again.
    m_widget->setStretchHorizontal(true);
    m_widget->setStretchVertical(true);
    QCOMPARE(button("playPauseButton")->sizePolicy().horizontalPolicy(),
             QSizePolicy::Expanding);
    QCOMPARE(button("playPauseButton")->sizePolicy().verticalPolicy(),
             QSizePolicy::Expanding);
}

void PlaybackControlTest::stretchStatePersists()
{
    // Both on (the default): nothing is stored.
    WidgetConfiguration stored = m_widget->storeWidget();
    QVERIFY(!stored.contains("stretchHorizontal"));
    QVERIFY(!stored.contains("stretchVertical"));

    m_widget->setStretchHorizontal(false);
    m_widget->setStretchVertical(false);
    stored = m_widget->storeWidget();
    QCOMPARE(stored.value("stretchHorizontal").toBool(), false);
    QCOMPARE(stored.value("stretchVertical").toBool(), false);

    PlaybackControlWidget* other = new PlaybackControlWidget();
    other->restoreWidget(stored);
    QVERIFY(!other->stretchHorizontal());
    QVERIFY(!other->stretchVertical());
    QPushButton* otherPlayPause = other->findChild<QPushButton*>("playPauseButton");
    QCOMPARE(otherPlayPause->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(otherPlayPause->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    delete other;
}

void PlaybackControlTest::stretchOffKeepsNaturalButtonSizes()
{
    // The off state must be visible: in a wide widget the buttons keep
    // their natural size (the Preferred policy the off state used to set
    // would not — a QBoxLayout hands the surplus to Preferred items just
    // like Expanding ones, so the off state looked like the on state).
    m_widget->setStretchHorizontal(false);
    m_widget->setStretchVertical(false);
    m_widget->resize(800, 200);
    QCoreApplication::processEvents();

    for (const QString &name : { "previousButton", "playPauseButton",
                                 "stopButton", "nextButton",
                                 "shuffleButton", "repeatButton", }) {
        const QPushButton* b = button(name);
        QCOMPARE(b->width(), b->sizeHint().width());
        QCOMPARE(b->height(), b->sizeHint().height());
    }

    // A stretcher absorbs the whole surplus: the buttons pack at the
    // row's start, still at their natural size.
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"stop", "stretch", "next"};
    m_widget->restoreWidget(cfg);
    QCoreApplication::processEvents();
    QCOMPARE(button("stopButton")->x(), 0);
    QCOMPARE(button("stopButton")->width(), button("stopButton")->sizeHint().width());
    QCOMPARE(button("nextButton")->width(), button("nextButton")->sizeHint().width());

    // The on state stretches the buttons across the surplus space.
    m_widget->setStretchHorizontal(true);
    m_widget->setStretchVertical(true);
    QCoreApplication::processEvents();
    for (const QString &name : { "stopButton", "nextButton" }) {
        const QPushButton* b = button(name);
        QVERIFY(b->width() > b->sizeHint().width());
        QVERIFY(b->height() > b->sizeHint().height());
    }
}

void PlaybackControlTest::orientationDefaultHorizontal()
{
    QCOMPARE(m_widget->orientation(), PlaybackControlWidget::Orientation::Horizontal);
    QVERIFY(qobject_cast<QHBoxLayout*>(m_widget->layout()) != nullptr);

    WidgetConfiguration stored = m_widget->storeWidget();
    QVERIFY(!stored.contains("orientation"));
}

void PlaybackControlTest::contextMenuSwitchesOrientation()
{
    QMenu* menu = m_widget->widgetContextMenu();
    QVERIFY(menu != nullptr);

    QAction* horizontal = findAction(menu, "Horizontal");
    QAction* vertical = findAction(menu, "Vertical");
    QAction* stretchH = findAction(menu, "Stretch horizontally");
    QAction* stretchV = findAction(menu, "Stretch vertically");
    QVERIFY(horizontal != nullptr);
    QVERIFY(vertical != nullptr);
    QVERIFY(stretchH != nullptr);
    QVERIFY(stretchV != nullptr);

    // The defaults: horizontal, both stretches on.
    QVERIFY(horizontal->isChecked());
    QVERIFY(!vertical->isChecked());
    QVERIFY(stretchH->isChecked());
    QVERIFY(stretchV->isChecked());

    vertical->trigger();
    QCOMPARE(m_widget->orientation(), PlaybackControlWidget::Orientation::Vertical);
    QVERIFY(qobject_cast<QVBoxLayout*>(m_widget->layout()) != nullptr);
    QVERIFY(vertical->isChecked());

    // The exclusive pair: a fresh menu shows the vertical entry
    // checked and the horizontal one unchecked.
    QMenu* menu2 = m_widget->widgetContextMenu();
    QVERIFY(menu2 != nullptr);
    QVERIFY(findAction(menu2, "Vertical") != nullptr);
    QVERIFY(findAction(menu2, "Horizontal") != nullptr);
    QVERIFY(findAction(menu2, "Vertical")->isChecked());
    QVERIFY(!findAction(menu2, "Horizontal")->isChecked());
    menu->deleteLater();
    menu2->deleteLater();
}

void PlaybackControlTest::orientationPersists()
{
    m_widget->setOrientation(PlaybackControlWidget::Orientation::Vertical);
    WidgetConfiguration stored = m_widget->storeWidget();
    QCOMPARE(stored.value("orientation").toInt(),
             static_cast<int>(PlaybackControlWidget::Orientation::Vertical));

    PlaybackControlWidget* other = new PlaybackControlWidget();
    other->restoreWidget(stored);
    QCOMPARE(other->orientation(), PlaybackControlWidget::Orientation::Vertical);
    QVERIFY(qobject_cast<QVBoxLayout*>(other->layout()) != nullptr);
    delete other;

    // A stale out-of-range value is ignored (the horizontal default).
    PlaybackControlWidget* fresh = new PlaybackControlWidget();
    WidgetConfiguration bogus;
    bogus["orientation"] = 42;
    fresh->restoreWidget(bogus);
    QCOMPARE(fresh->orientation(), PlaybackControlWidget::Orientation::Horizontal);
    delete fresh;
}

void PlaybackControlTest::restoreWidgetAppliesStretchers()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"previous", "stretch", "stop", "stretch", "next"};
    m_widget->restoreWidget(cfg);

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(m_widget->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }
    QCOMPARE(layout->count(), 5); // 3 buttons + 2 stretchers
    QCOMPARE(layout->itemAt(0)->widget(), button("previousButton"));
    QVERIFY(layout->itemAt(1)->spacerItem() != nullptr);
    QCOMPARE(layout->itemAt(2)->widget(), button("stopButton"));
    QVERIFY(layout->itemAt(3)->spacerItem() != nullptr);
    QCOMPARE(layout->itemAt(4)->widget(), button("nextButton"));

    // The stretchers ride in the stored sequence.
    QCOMPARE(m_widget->sequence(),
             (QStringList{"previous", "stretch", "stop", "stretch", "next"}));

    // A vertical widget takes the same sequence as a column.
    m_widget->setOrientation(PlaybackControlWidget::Orientation::Vertical);
    QVBoxLayout* column = qobject_cast<QVBoxLayout*>(m_widget->layout());
    QVERIFY(column != nullptr);
    QCOMPARE(column->count(), 5);
    QVERIFY(column->itemAt(1)->spacerItem() != nullptr);
    QVERIFY(column->itemAt(3)->spacerItem() != nullptr);
}

void PlaybackControlTest::stretcherConfigurationSurvivesJsonRoundtrip()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"stretch", "playpause", "stretch"};
    m_widget->restoreWidget(cfg);

    // Same path the layout persistence and copy/paste take.
    QJsonObject json = widgetConfigurationToJson(m_widget->storeWidget());
    json.insert("type", PlaybackControlWidget::id);
    WidgetConfiguration restored = widgetConfigurationFromJson(json);

    PlaybackControlWidget* other = new PlaybackControlWidget();
    other->restoreWidget(restored);

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(other->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }
    QCOMPARE(layout->count(), 3);
    QVERIFY(layout->itemAt(0)->spacerItem() != nullptr);
    QCOMPARE(layout->itemAt(1)->widget(), other->findChild<QPushButton*>("playPauseButton"));
    QVERIFY(layout->itemAt(2)->spacerItem() != nullptr);

    delete other;
}

void PlaybackControlTest::storeWidgetListsVisibleButtonsInOrder()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"next", "previous"};
    m_widget->restoreWidget(cfg);

    QCOMPARE(m_widget->storeWidget().value("buttons").toStringList(),
             (QStringList{"next", "previous"}));
}

void PlaybackControlTest::restoreWidgetAppliesVisibilityAndOrder()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"next", "previous"};
    m_widget->restoreWidget(cfg);

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(m_widget->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }
    QCOMPARE(layout->count(), 2); // the 2 visible buttons, no spacers

    QCOMPARE(layout->itemAt(0)->widget(), button("nextButton"));
    QCOMPARE(layout->itemAt(1)->widget(), button("previousButton"));

    QVERIFY(!button("nextButton")->isHidden());
    QVERIFY(!button("previousButton")->isHidden());
    QVERIFY(button("playPauseButton")->isHidden());
    QVERIFY(button("stopButton")->isHidden());
}

void PlaybackControlTest::restoreWidgetIgnoresUnknownAndDuplicateKeys()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"bogus", "next", "next", "stop"};
    m_widget->restoreWidget(cfg);

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(m_widget->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }
    QCOMPARE(layout->count(), 2); // the 2 valid buttons, no spacers
    QCOMPARE(layout->itemAt(0)->widget(), button("nextButton"));
    QCOMPARE(layout->itemAt(1)->widget(), button("stopButton"));
}

void PlaybackControlTest::restoreWidgetFallsBackToDefaultWhenNoValidButtons()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"bogus"};
    m_widget->restoreWidget(cfg);

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(m_widget->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }
    QCOMPARE(layout->count(), 6); // all 6 buttons, no spacers
    QCOMPARE(layout->itemAt(0)->widget(), button("previousButton"));
    QCOMPARE(layout->itemAt(1)->widget(), button("playPauseButton"));
    QCOMPARE(layout->itemAt(2)->widget(), button("stopButton"));
    QCOMPARE(layout->itemAt(3)->widget(), button("nextButton"));
    QCOMPARE(layout->itemAt(4)->widget(), button("shuffleButton"));
    QCOMPARE(layout->itemAt(5)->widget(), button("repeatButton"));
}

void PlaybackControlTest::buttonConfigurationSurvivesJsonRoundtrip()
{
    WidgetConfiguration cfg;
    cfg["buttons"] = QStringList{"stop", "playpause"};
    m_widget->restoreWidget(cfg);

    // Same path the layout persistence and copy/paste take.
    QJsonObject json = widgetConfigurationToJson(m_widget->storeWidget());
    json.insert("type", PlaybackControlWidget::id);
    WidgetConfiguration restored = widgetConfigurationFromJson(json);

    PlaybackControlWidget* other = new PlaybackControlWidget();
    other->restoreWidget(restored);

    QHBoxLayout* layout = qobject_cast<QHBoxLayout*>(other->layout());
    if (layout == nullptr) {
        QFAIL("widget layout is not a QHBoxLayout");
        return;
    }
    QCOMPARE(layout->count(), 2); // the 2 visible buttons, no spacers
    QCOMPARE(layout->itemAt(0)->widget(), other->findChild<QPushButton*>("stopButton"));
    QCOMPARE(layout->itemAt(1)->widget(), other->findChild<QPushButton*>("playPauseButton"));

    delete other;
}

void PlaybackControlTest::registeredInLayoutManager()
{
    WidgetConfiguration cfg;
    BaseWidget* w = LayoutManager::getInstance()->createWidget(PlaybackControlWidget::id, cfg);

    QVERIFY(w != nullptr);
    QCOMPARE(w->widgetId(), PlaybackControlWidget::id);
    QCOMPARE(w->widgetTitle(), PlaybackControlWidget::title);
    delete w;
}
}


QTEST_MAIN(PlaybackControlTest)

#include "playbackcontrol_test.moc"
