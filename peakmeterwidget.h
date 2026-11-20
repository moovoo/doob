#ifndef PEAKMETERWIDGET_H
#define PEAKMETERWIDGET_H

#include <QList>
#include <QPair>
#include <QSize>
#include <QStringList>
#include <QTimer>

#include "barpaint.h"
#include "basewidget.h"
#include "playbackmanager.h"

class QMenu;
class QPainter;

// A two-channel (L/R) peak meter: each bar jumps to a new peak on the
// driver's audioLevels tap and decays toward silence on a timer. The
// decay runs only while playback is Playing; a pause or stop clears the
// bars immediately (silence is silence). The display style, the fill
// direction and the visible channels (and their order) are edit-mode
// options (see widgetContextMenu()).
//
// The paint's rhythm is a user setting (visualization.fps, see
// PaintRate): while playing, the timer runs at the configured rate and
// each tick paints one step of the release — the taps only set the
// peaks. The release time is per unit of time, not per tick: the
// per-tick decay factor is rescaled so the bar falls in the same time
// at any paint rate.
//
// The bar's rendering is tuned by style-sheet custom properties (see
// StyleManager): "x-segments" — the number of discrete blocks the
// "Segments" display style splits each bar into along the fill axis
// (a 1px gap between them; the blocks the level reaches are lit, the
// rest carry the track color), "x-bar-margin" — the space between and
// around the bars (auto: a fixed share of the across-axis), and
// "x-track-color" — a bar's unfilled part (auto: the background
// lightened).
class PeakMeterWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "peakmeter";
    inline static const QString title = "Peak meter";
    inline static const QString tag = widgetTags::visualization;

    // The minimum size: the bars are painted into the widget itself
    // (it has no children to carry a layout minimum, like the
    // component-built widgets do) — the floor keeps a splitter from
    // collapsing the meter to nothing.
    static inline const QSize kMinimumSize{24, 24};

    // The direction the bars fill (the shared bar rendering of the
    // visualization's bar displays, see barpaint): Up/Down draw vertical
    // bars (from the bottom / from the top), LeftToRight/RightToLeft
    // draw horizontal bars (from the left / from the right). Up is the
    // default.
    using Orientation = barpaint::Orientation;
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation orientation);

    // How the bars render: Solid (the default, one filled rect per bar)
    // or Segments (each bar split into discrete blocks along the fill
    // axis, see the class comment).
    enum class Style { Solid, Segments };
    Style displayStyle() const { return m_displayStyle; }
    void setDisplayStyle(Style style);

    // The visible channels, in display order (keys "left"/"right",
    // default {"left", "right"}); a channel not on the list is hidden.
    QStringList visibleChannels() const { return m_channels; }
    void setChannels(const QStringList& channels);

    // (Display label, config key) pairs for every known channel, in
    // default order — the catalog the "Channels..." edit-mode dialog
    // operates on.
    static QList<QPair<QString, QString>> channelCatalog();

    // The "x-" style properties this widget reads (see StyleManager):
    // the shared bar rendering's, under this widget's title — the
    // style sheet editor's help documents them from here.
    static QList<StyleProperty> styleProperties();

    explicit PeakMeterWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    // The currently displayed (decayed) levels, 0..1 — the test seam.
    float leftLevel() const { return m_left; }
    float rightLevel() const { return m_right; }
    // The paint timer's interval, in ms, and whether it is running
    // (the test seam).
    int paintIntervalMs() const { return m_timer.interval(); }
    bool paintTimerActive() const { return m_timer.isActive(); }

protected:
    // The bars are painted into the widget itself: the style scaffold
    // is the bare "#name" rule, no "#name *" for children.
    bool selfPainted() const override { return true; }

    void paintEvent(QPaintEvent *event) override;

private slots:
    void onLevels(float leftPeak, float rightPeak);
    void onStateChanged(PlaybackManager::State state);
    // One step of the peak's release (the timer's tick) and the paint.
    void decay();
    void onSettingChanged(const QString &key, const QVariant &value);
    void onEditChannels();

private:
    // The timer's interval at the configured paint rate, and the
    // per-tick decay factor rescaled to match it (see the class
    // comment).
    void applyPaintRate();

    PlaybackManager* m_pm;
    float m_left = 0.0F;
    float m_right = 0.0F;
    float m_decay = 0.85F;  // per tick (0.85 per 16 ms at the default rate)
    QTimer m_timer;
    Style m_displayStyle = Style::Solid;
    Orientation m_orientation = Orientation::Up;
    QStringList m_channels = {"left", "right"};
};

#endif // PEAKMETERWIDGET_H
