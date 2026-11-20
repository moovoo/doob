#include "peakmeterwidget.h"

#include "barpaint.h"
#include "checkablelistdialog.h"
#include "paintrate.h"
#include "settingsmanager.h"
#include "vizwidget.h"

#include <QAction>
#include <QDialog>
#include <QMenu>
#include <QPainter>

PeakMeterWidget::PeakMeterWidget(QWidget *parent)
    : BaseWidget(parent)
{
    setMinimumSize(kMinimumSize);

    m_pm = PlaybackManager::getInstance();

    // The paint and release tick (a user setting): while playing, the
    // timer runs at the configured paint rate and each tick paints one
    // step of the release — the taps only set the peaks — so the
    // cadence is steady and the user's choice, not the driver's.
    PaintRate::ensureRegistered();
    applyPaintRate();
    connect(&m_timer, &QTimer::timeout, this, &PeakMeterWidget::decay);
    connect(SettingsManager::getInstance(), &SettingsManager::changed,
            this, &PeakMeterWidget::onSettingChanged);

    connect(m_pm, &PlaybackManager::audioLevels, this, &PeakMeterWidget::onLevels);
    connect(m_pm, &PlaybackManager::stateChanged, this, &PeakMeterWidget::onStateChanged);

    // A widget inserted mid-playback must start its decay timer; the
    // signal only fires for future changes.
    onStateChanged(m_pm->state());
}

void PeakMeterWidget::onLevels(float leftPeak, float rightPeak)
{
    // The tap is latency-shifted (the frames only reach the speaker
    // after the stream's latency), so a queued tap can land after a
    // pause or stop: the meter only follows live playback.
    if (m_pm->state() != PlaybackManager::Playing)
        return;
    // Fast attack: jump to a new peak (clamped to 0..1). The timer
    // shows it and handles the release (the taps do not paint
    // themselves).
    m_left = qMax(m_left, qBound(0.0F, leftPeak, 1.0F));
    m_right = qMax(m_right, qBound(0.0F, rightPeak, 1.0F));
}

void PeakMeterWidget::onStateChanged(PlaybackManager::State state)
{
    if (viz::runningFor(state, &m_timer))
        return;
    // A pause or stop is silence: clear the bars and stop the release.
    m_left = 0.0F;
    m_right = 0.0F;
    update();
}

void PeakMeterWidget::decay()
{
    m_left *= m_decay;
    m_right *= m_decay;
    if (m_left < 0.002F)
        m_left = 0.0F;
    if (m_right < 0.002F)
        m_right = 0.0F;
    update();
}

void PeakMeterWidget::onSettingChanged(const QString &key, const QVariant &value)
{
    if (key != PaintRate::key())
        return;
    applyPaintRate();
}

void PeakMeterWidget::applyPaintRate()
{
    PaintRate::applyTo(&m_timer);
    // The release is per unit of time, not per tick: rescale the
    // per-tick factor (0.85 per 16 ms at the default rate) so the bar
    // falls in the same time at any paint rate.
    m_decay = static_cast<float>(
        PaintRate::decayFactor(m_timer.interval(), 0.85, 16.0));
}

void PeakMeterWidget::setDisplayStyle(Style style)
{
    if (style == m_displayStyle)
        return;
    m_displayStyle = style;
    update();
}

void PeakMeterWidget::setOrientation(Orientation orientation)
{
    if (orientation == m_orientation)
        return;
    m_orientation = orientation;
    update();
}

void PeakMeterWidget::setChannels(const QStringList& channels)
{
    // Keep only known keys, in the given order, without duplicates.
    QStringList valid;
    for (const QString& key : channels)
        if ((key == "left" || key == "right") && !valid.contains(key))
            valid.append(key);
    if (valid.isEmpty()) {
        // At least one channel must stay visible: fall back to the
        // default set.
        for (const QPair<QString, QString>& pair : channelCatalog())
            valid.append(pair.second);
    }
    if (valid == m_channels)
        return;
    m_channels = valid;
    update();
}

QList<QPair<QString, QString>> PeakMeterWidget::channelCatalog()
{
    return {qMakePair(QStringLiteral("Left"), QStringLiteral("left")),
            qMakePair(QStringLiteral("Right"), QStringLiteral("right")),
    };
}

QList<StyleProperty> PeakMeterWidget::styleProperties()
{
    return barpaint::styleProperties(title);
}

QMenu* PeakMeterWidget::widgetContextMenu()
{
    // Start from the base menu (the toolbar entries, when a toolbar
    // exists) and add this widget's own actions.
    QMenu* menu = viz::editMenu(BaseWidget::widgetContextMenu(), this, title);

    // The display style: an exclusive checkable group, rebuilt from the
    // current state on every menu.
    viz::optionGroup<Style>(this, menu, {Style::Solid, Style::Segments},
                            [](Style style) {
                                return style == Style::Solid
                                           ? QStringLiteral("Solid")
                                           : QStringLiteral("Segments");
                            },
                            m_displayStyle,
                            [this](Style style) { setDisplayStyle(style); });

    menu->addSeparator();

    // The fill direction: an exclusive checkable group, rebuilt from the
    // current state on every menu.
    viz::optionGroup<Orientation>(
        this, menu,
        {Orientation::Up, Orientation::Down, Orientation::LeftToRight,
         Orientation::RightToLeft,
        },
        barpaint::orientationName, m_orientation,
        [this](Orientation orientation) { setOrientation(orientation); });

    menu->addSeparator();

    QAction* channels = menu->addAction("Channels...");
    connect(channels, &QAction::triggered, this, &PeakMeterWidget::onEditChannels);

    return menu;
}

void PeakMeterWidget::onEditChannels()
{
    CheckableListDialog dialog(QStringLiteral("Peak meter channels"),
                               QStringLiteral("Checked channels are visible; "
                                              "use the arrows to rearrange them."),
                               channelCatalog(), visibleChannels(), this, false,
                               QStringLiteral("dialogs.peak_meter_channels"));
    if (dialog.exec() == QDialog::Accepted)
        setChannels(dialog.selectedOrder());
}

WidgetConfiguration PeakMeterWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only non-default states are persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the defaults.
    if (m_displayStyle != Style::Solid)
        config["displayStyle"] = static_cast<int>(m_displayStyle);
    if (m_orientation != Orientation::Up)
        config["orientation"] = static_cast<int>(m_orientation);
    config["channels"] = m_channels;
    return config;
}

void PeakMeterWidget::restoreWidget(WidgetConfiguration& config)
{
    BaseWidget::restoreWidget(config);
    // A stale value outside the enum range is ignored (the menu only
    // ever passes valid ones).
    if (config.contains("displayStyle")) {
        const int value = config.value("displayStyle").toInt();
        if (value >= 0 && value <= static_cast<int>(Style::Segments))
            setDisplayStyle(static_cast<Style>(value));
    }
    if (config.contains("orientation")) {
        const int value = config.value("orientation").toInt();
        if (value >= 0 && value <= static_cast<int>(Orientation::RightToLeft))
            setOrientation(static_cast<Orientation>(value));
    }
    if (config.contains("channels"))
        setChannels(config.value("channels").toStringList());
}

void PeakMeterWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    // The styling surfaces (a stylesheet's "background" recolors the
    // background, the track follows; "color" the bars); the display
    // style (an edit-mode option) picks the rendering, the "x-"
    // properties tune it.
    const BarSurface surface = barSurface(p);
    const int bars = static_cast<int>(m_channels.size());
    for (int i = 0; i < bars; ++i) {
        const QRect slot =
            barpaint::barSlot(i, bars, size(), m_orientation, surface.margin);
        if (!slot.isValid())
            continue;
        const float level = m_channels.at(i) == "left" ? m_left : m_right;
        if (m_displayStyle == Style::Segments)
            barpaint::paintSegments(p, slot, level, surface.segments,
                                    m_orientation, surface.track, surface.bar);
        else
            barpaint::paintSolid(p, slot, level, m_orientation, surface.track,
                                 surface.bar);
    }
}
