#include "checkablelistdialog.h"
#include "playbackcontrol.h"
#include "playbackmanager.h"
#include "playlistmanager.h"

#include <QAction>
#include <QActionGroup>
#include <QBoxLayout>
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// The buttons are icon-only (icons/ directory, /icons resource prefix);
// the icons share one 24x24 geometry, so the bar keeps a stable size
// without any height pinning.
QString iconFor(const QString& key)
{
    if (key == "previous")
        return QStringLiteral(":/icons/previous.svg");
    if (key == "playpause")
        return QStringLiteral(":/icons/play.svg");
    if (key == "stop")
        return QStringLiteral(":/icons/stop.svg");
    if (key == "next")
        return QStringLiteral(":/icons/next.svg");
    if (key == "shuffle")
        return QStringLiteral(":/icons/shuffle-off.svg");
    if (key == "repeat")
        return QStringLiteral(":/icons/repeat-off.svg");
    return {};
}

QString labelForKey(const QString& key)
{
    if (key == "previous")
        return QStringLiteral("Previous");
    if (key == "playpause")
        return QStringLiteral("Play / Pause");
    if (key == "stop")
        return QStringLiteral("Stop");
    if (key == "next")
        return QStringLiteral("Next");
    if (key == "shuffle")
        return QStringLiteral("Shuffle");
    if (key == "repeat")
        return QStringLiteral("Repeat");
    return {};
}

// The icon-only buttons' state -> (icon, tooltip) mapping. The tooltips
// are the buttons' captions: they name the current state and the press
// that would leave it.
QPair<QString, QString> shuffleState(bool on)
{
    return { on ? QStringLiteral(":/icons/shuffle.svg")
                : QStringLiteral(":/icons/shuffle-off.svg"),
             on ? QStringLiteral("Shuffle on (click to turn off)")
                : QStringLiteral("Shuffle off (click to turn on)") };
}

QPair<QString, QString> repeatState(PlaybackManager::RepeatMode mode)
{
    switch (mode) {
    case PlaybackManager::RepeatMode::RepeatTrack:
        return { QStringLiteral(":/icons/repeat-one.svg"),
                 QStringLiteral("Repeat one (click for repeat off)") };
    case PlaybackManager::RepeatMode::RepeatPlaylist:
        return { QStringLiteral(":/icons/repeat.svg"),
                 QStringLiteral("Repeat all (click for repeat one)") };
    case PlaybackManager::RepeatMode::NoRepeat:
    default:
        return { QStringLiteral(":/icons/repeat-off.svg"),
                 QStringLiteral("Repeat off (click for repeat all)") };
    }
}

} // namespace

PlaybackControlWidget::PlaybackControlWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_pm = PlaybackManager::getInstance();
    m_pl = PlaylistManager::getInstance();

    m_previous = addButton("previous", "previousButton");
    m_playPause = addButton("playpause", "playPauseButton");
    m_stop = addButton("stop", "stopButton");
    m_next = addButton("next", "nextButton");
    m_shuffle = addButton("shuffle", "shuffleButton");
    m_repeat = addButton("repeat", "repeatButton");
    m_shuffle->setCheckable(true);

    for (const ControlButton& b : m_buttons)
        m_sequence.append({b.key, b.button});

    connect(m_previous, &QPushButton::clicked, this, &PlaybackControlWidget::onPreviousClicked);
    connect(m_playPause, &QPushButton::clicked, this, &PlaybackControlWidget::onPlayPauseClicked);
    connect(m_stop, &QPushButton::clicked, this, &PlaybackControlWidget::onStopClicked);
    connect(m_next, &QPushButton::clicked, this, &PlaybackControlWidget::onNextClicked);
    connect(m_shuffle, &QPushButton::toggled, this, &PlaybackControlWidget::onShuffleToggled);
    connect(m_repeat, &QPushButton::clicked, this, &PlaybackControlWidget::onRepeatClicked);
    connect(m_pm, &PlaybackManager::stateChanged, this, &PlaybackControlWidget::onStateChanged);
    connect(m_pl, &PlaylistManager::playlistChanged, this, &PlaybackControlWidget::onPlaylistChanged);
    connect(m_pl, &PlaylistManager::currentPlaylistChanged, this, &PlaybackControlWidget::onPlaylistChanged);
    connect(m_pm, &PlaybackManager::shuffleChanged, this, &PlaybackControlWidget::onShuffleChanged);
    connect(m_pm, &PlaybackManager::repeatChanged, this, &PlaybackControlWidget::onRepeatChanged);

    // Initialize from the current playback state: the signals only fire
    // for future changes, so a widget inserted while playing (or with
    // shuffle / a repeat mode on) would otherwise start stale. The guard
    // keeps the shuffle button's toggled from feeding back into the
    // manager while the state is reflected.
    m_syncing = true;
    updateButtons();
    updateShuffleButton();
    updateRepeatButton();
    m_syncing = false;

    applyStretchPolicies();
    rebuildLayout();
}

QPushButton* PlaybackControlWidget::addButton(const QString& key, const QString& objectName)
{
    QPushButton* button = new QPushButton(this);
    button->setObjectName(objectName);
    button->setIcon(QIcon(iconFor(key)));
    button->setToolTip(labelForKey(key));
    m_buttons.append({key, button});
    return button;
}

void PlaybackControlWidget::rebuildLayout()
{
    // The buttons fill the row (or column) in the sequence's order; the
    // stretchers are the layout's stretch items.
    delete layout();
    QBoxLayout* box = m_orientation == Orientation::Horizontal
        ? static_cast<QBoxLayout*>(new QHBoxLayout(this))
        : static_cast<QBoxLayout*>(new QVBoxLayout(this));
    box->setContentsMargins(0, 0, 0, 0);

    for (const SequenceItem& item : m_sequence) {
        if (item.button == nullptr)
            box->addStretch(1);
        else
            box->addWidget(item.button);
    }

    for (const ControlButton& b : m_buttons) {
        bool visible = false;
        for (const SequenceItem& item : m_sequence)
            if (item.button == b.button) {
                visible = true;
                break;
            }
        b.button->setVisible(visible);
    }
}

void PlaybackControlWidget::applySequence(const QStringList& keys)
{
    m_sequence.clear();
    for (const QString& key : keys) {
        if (key == CheckableListDialog::stretcherKey) {
            m_sequence.append({key, nullptr});
            continue;
        }
        QPushButton* button = buttonFor(key);
        if (button == nullptr)
            continue; // unknown (stale) key
        bool present = false;
        for (const SequenceItem& item : m_sequence)
            if (item.button == button) {
                present = true;
                break;
            }
        if (present)
            continue; // duplicate
        m_sequence.append({key, button});
    }
    if (m_sequence.isEmpty()) {
        // At least one button must stay visible: fall back to the
        // default set (and no stretchers).
        for (const ControlButton& b : m_buttons)
            m_sequence.append({b.key, b.button});
    }
    rebuildLayout();
}

void PlaybackControlWidget::applyStretchPolicies()
{
    // Two policies, neither of which is a no-op in the box layout:
    // - off: Fixed. A QBoxLayout hands the surplus space to Preferred
    //   items too (an even split when no item has an explicit stretch
    //   factor), so a Preferred off-state would stretch like the on
    //   state. Fixed pins the buttons at their natural size; a
    //   stretcher, when present, absorbs the whole surplus.
    // - on: Expanding, plus an explicit policy stretch of 1. A policy
    //   alone carries no stretch factor here, and once a stretcher (an
    //   explicit stretch item) is in the sequence the layout distributes
    //   the surplus by stretch factor only — without the explicit factor
    //   the stretchers would swallow all of it.
    const QSizePolicy::Policy h = m_stretchHorizontal
        ? QSizePolicy::Expanding : QSizePolicy::Fixed;
    const QSizePolicy::Policy v = m_stretchVertical
        ? QSizePolicy::Expanding : QSizePolicy::Fixed;
    const int stretch = 1;
    for (const ControlButton& b : m_buttons) {
        QSizePolicy policy(h, v);
        policy.setHorizontalStretch(m_stretchHorizontal ? stretch : 0);
        policy.setVerticalStretch(m_stretchVertical ? stretch : 0);
        b.button->setSizePolicy(policy);
    }
}

void PlaybackControlWidget::setStretchHorizontal(bool on)
{
    if (on == m_stretchHorizontal)
        return;
    m_stretchHorizontal = on;
    applyStretchPolicies();
    updateGeometry();
}

void PlaybackControlWidget::setStretchVertical(bool on)
{
    if (on == m_stretchVertical)
        return;
    m_stretchVertical = on;
    applyStretchPolicies();
    updateGeometry();
}

void PlaybackControlWidget::setOrientation(Orientation orientation)
{
    if (orientation == m_orientation)
        return;
    m_orientation = orientation;
    rebuildLayout();
}

QPushButton* PlaybackControlWidget::buttonFor(const QString& key) const
{
    for (const ControlButton& b : m_buttons)
        if (b.key == key)
            return b.button;
    return nullptr;
}

QList<QPair<QString, QString>> PlaybackControlWidget::buttonCatalog() const
{
    QList<QPair<QString, QString>> catalog;
    for (const ControlButton& b : m_buttons)
        catalog.append(qMakePair(labelForKey(b.key), b.key));
    return catalog;
}

QStringList PlaybackControlWidget::sequence() const
{
    QStringList keys;
    for (const SequenceItem& item : m_sequence)
        keys.append(item.key);
    return keys;
}

QMenu* PlaybackControlWidget::widgetContextMenu()
{
    // Start from the base menu (the toolbar entries, when a toolbar
    // exists) and add this widget's own actions.
    QMenu* menu = BaseWidget::widgetContextMenu();
    if (menu == nullptr)
        menu = new QMenu(title, this);

    QAction* buttons = menu->addAction("Buttons...");
    connect(buttons, &QAction::triggered, this, &PlaybackControlWidget::onEditButtons);

    QAction* stretchHorizontal = menu->addAction("Stretch horizontally");
    stretchHorizontal->setCheckable(true);
    stretchHorizontal->setChecked(m_stretchHorizontal);
    connect(stretchHorizontal, &QAction::toggled, this,
            [this](bool on) { setStretchHorizontal(on); });

    QAction* stretchVertical = menu->addAction("Stretch vertically");
    stretchVertical->setCheckable(true);
    stretchVertical->setChecked(m_stretchVertical);
    connect(stretchVertical, &QAction::toggled, this,
            [this](bool on) { setStretchVertical(on); });

    menu->addSeparator();

    // The orientation pair sits in an exclusive action group, the same
    // as every option menu: Qt draws the options as radio buttons.
    QActionGroup* group = new QActionGroup(menu);
    group->setExclusive(true);
    QAction* horizontal = menu->addAction("Horizontal");
    horizontal->setCheckable(true);
    horizontal->setChecked(m_orientation == Orientation::Horizontal);
    horizontal->setActionGroup(group);
    connect(horizontal, &QAction::triggered, this,
            [this] { setOrientation(Orientation::Horizontal); });
    QAction* vertical = menu->addAction("Vertical");
    vertical->setCheckable(true);
    vertical->setChecked(m_orientation == Orientation::Vertical);
    vertical->setActionGroup(group);
    connect(vertical, &QAction::triggered, this,
            [this] { setOrientation(Orientation::Vertical); });

    return menu;
}

void PlaybackControlWidget::onEditButtons()
{
    CheckableListDialog dialog(QStringLiteral("Playback buttons"),
                               QStringLiteral("Checked buttons are visible; "
                                              "use the arrows to rearrange them. "
                                              "Stretchers fill the space around "
                                              "the buttons (at most one per gap)."),
                               buttonCatalog(), sequence(), this, true,
                               QStringLiteral("dialogs.playback_buttons"));
    if (dialog.exec() == QDialog::Accepted)
        applySequence(dialog.sequence());
}

WidgetConfiguration PlaybackControlWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    config["buttons"] = sequence();
    // Only non-default states are persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the defaults.
    if (!m_stretchHorizontal)
        config["stretchHorizontal"] = false;
    if (!m_stretchVertical)
        config["stretchVertical"] = false;
    if (m_orientation != Orientation::Horizontal)
        config["orientation"] = static_cast<int>(m_orientation);
    return config;
}

void PlaybackControlWidget::restoreWidget(WidgetConfiguration& config)
{
    BaseWidget::restoreWidget(config);
    QStringList keys;
    const QList<QVariant> list = config.value("buttons").toList();
    for (const QVariant& value : list)
        keys.append(value.toString());
    applySequence(keys);

    if (config.contains("stretchHorizontal"))
        setStretchHorizontal(config.value("stretchHorizontal").toBool());
    if (config.contains("stretchVertical"))
        setStretchVertical(config.value("stretchVertical").toBool());
    // A stale value outside the enum range is ignored (the menu only
    // ever passes valid ones).
    if (config.contains("orientation")) {
        const int value = config.value("orientation").toInt();
        if (value >= 0 && value <= static_cast<int>(Orientation::Vertical))
            setOrientation(static_cast<Orientation>(value));
    }
}

void PlaybackControlWidget::onPlayPauseClicked()
{
    m_pm->togglePlayPause();
}

void PlaybackControlWidget::onNextClicked()
{
    m_pm->next();
}

void PlaybackControlWidget::onPreviousClicked()
{
    m_pm->previous();
}

void PlaybackControlWidget::onStopClicked()
{
    m_pm->stop();
}

void PlaybackControlWidget::onShuffleToggled(bool checked)
{
    if (m_syncing)
        return;
    m_pm->setShuffle(checked);
}

void PlaybackControlWidget::onRepeatClicked()
{
    m_pm->cycleRepeat();
}

void PlaybackControlWidget::onShuffleChanged(bool shuffle)
{
    Q_UNUSED(shuffle)  // the button reads the state back from the manager
    updateShuffleButton();
}

void PlaybackControlWidget::onRepeatChanged(PlaybackManager::RepeatMode mode)
{
    Q_UNUSED(mode)  // the button reads the state back from the manager
    updateRepeatButton();
}

void PlaybackControlWidget::onStateChanged(PlaybackManager::State state)
{
    updateButtons();
}

void PlaybackControlWidget::onPlaylistChanged()
{
    updateButtons();
}

void PlaybackControlWidget::updateButtons()
{
    const bool hasItems = m_pl->count() > 0;
    const PlaybackManager::State state = m_pm->state();

    m_previous->setEnabled(hasItems);
    m_playPause->setEnabled(hasItems);
    m_next->setEnabled(hasItems);
    m_stop->setEnabled(hasItems && state != PlaybackManager::Stopped);
    const bool playing = state == PlaybackManager::Playing;
    m_playPause->setIcon(QIcon(playing ? QStringLiteral(":/icons/pause.svg")
                                       : QStringLiteral(":/icons/play.svg")));
    m_playPause->setToolTip(playing ? QStringLiteral("Pause") : QStringLiteral("Play"));
}

void PlaybackControlWidget::updateShuffleButton()
{
    const QPair<QString, QString> state = shuffleState(m_pm->shuffle());
    m_shuffle->setChecked(m_pm->shuffle());
    m_shuffle->setIcon(QIcon(state.first));
    m_shuffle->setToolTip(state.second);
}

void PlaybackControlWidget::updateRepeatButton()
{
    const QPair<QString, QString> state = repeatState(m_pm->repeat());
    m_repeat->setIcon(QIcon(state.first));
    m_repeat->setToolTip(state.second);
}
