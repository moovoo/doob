#ifndef OSCILLOGRAMWIDGET_H
#define OSCILLOGRAMWIDGET_H

#include <QColor>
#include <QList>
#include <QSize>
#include <QTimer>
#include <QVector>

#include <array>

#include "basewidget.h"
#include "playbackmanager.h"
#include "styleproperty.h"

class QPainter;

// A scrolling oscillogram: the driver's min/max waveform taps (each of
// its columns spans 8 frames) are re-bucketed into display columns —
// each spanning a fixed share of the trace's window (the window's
// audio span over the capacity, whatever the sample rate) — and kept
// in a ring, so the signal scrolls left at a steady, small speed. A
// pause or stop clears the trace. The trace's display style and the
// window's length (how much audio the window holds) are edit-mode
// options (see widgetContextMenu()).
//
// The scroll is locked to the data's rate: a display column closes per
// K tap columns (the audio's rate over the window's span), so the
// trace advances one column per close, at whatever speed the audio
// flows — no free-running timer to beat the display. The window's
// right edge is the newest closed column itself (its right edge is
// the widget's edge): the trace always reaches the edge with real
// data, the newest sample at the edge, as on a real scope. The paint's
// rhythm is a user setting (visualization.fps, see PaintRate): a timer
// samples the trace at the rate — the taps only feed it — so the
// cadence is steady and the user's choice.
//
// The trace's rendering is a style-sheet custom property (see
// StyleManager): the "line" style is a single polyline that oscillates
// through the columns' envelopes — each column swings the pen between
// its max (top) and min (bottom), a triangle wave that keeps both
// edges the two-envelope view would show (round-capped and joined,
// "x-line-width" pixels wide, the default 2), the "boxes" style
// quantizes the envelope to "x-box-size" LCD boxes (px or % of the
// height, the default 3); the unlit grid of every data column glows
// through in "x-box-color-off" (the widget's background lightened by
// 20% by default — the bar widgets' auto track: a subtle tint on a
// light as on a dark background).
class OscillogramWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "oscillogram";
    inline static const QString title = "Oscillogram";
    inline static const QString tag = widgetTags::visualization;

    // The minimum size: the trace is painted into the widget itself
    // (it has no children to carry a layout minimum, like the
    // component-built widgets do) — the floor keeps a splitter from
    // collapsing the scope to nothing.
    static inline const QSize kMinimumSize{24, 24};
    // The window lengths the edit menu offers, in ms of audio the
    // window holds; the default (1 s) keeps the per-frame advance
    // small (a few pixels).
    static constexpr int kDefaultWindowMs = 1000;
    static constexpr std::array<int, 5> kWindowOptions = {
        250, 500, 1000, 2000, 5000,
    };
    // How the trace renders: Bars (the default, one vertical line per
    // column), Line or Boxes (see the class comment).
    enum class TraceStyle { Bars, Line, Boxes };
    TraceStyle traceStyle() const { return m_traceStyle; }
    void setTraceStyle(TraceStyle style);

    // The window's audio span, in ms (an edit-mode option): the
    // scroll's speed is the capacity over the span.
    int windowMs() const { return m_windowMs; }
    void setWindowMs(int ms);

    // The "x-" style properties this widget reads (see StyleManager and
    // the class comment): the style sheet editor's help documents them
    // from here.
    static QList<StyleProperty> styleProperties();

    explicit OscillogramWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    // The visible window's closed display columns as min/max pairs
    // (2 * points floats, oldest first) — the test seam.
    QVector<float> trace() const;
    int capacity() const { return m_capacity; }
    // The tap columns per display column (the re-bucketing, from the
    // tap's sample rate; 0 before the first tap) — the test seam.
    int tapsPerDisplayColumn() const { return m_tapsPerColumn; }
    // The window's right edge (display columns, the test seam): the
    // newest closed column — the trace reaches the widget's edge.
    double windowEnd() const;
    // The paint timer's interval, in ms, and whether it is running
    // (the test seam).
    int paintIntervalMs() const { return m_paintTimer.interval(); }
    bool paintTimerActive() const { return m_paintTimer.isActive(); }

protected:
    // The trace is painted into the widget itself: the style scaffold
    // is the bare "#name" rule, no "#name *" for children.
    bool selfPainted() const override { return true; }

    void paintEvent(QPaintEvent *event) override;

private slots:
    void onWaveform(const QVector<float> &waveform, quint32 sampleRate);
    void onStateChanged(PlaybackManager::State state);
    void onSettingChanged(const QString &key, const QVariant &value);

private:
    void clearTrace();
    // The display column's envelope (its ring slot).
    void columnPair(qsizetype g, float &mn, float &mx) const;

    // The three trace renderings, one per TraceStyle. Each takes the
    // shared geometry: the window's start in display columns (`start`,
    // fractional — the sub-column phase is what makes the scroll glide
    // smoothly), the columns to draw (`g0`..`g1` inclusive) on a grid
    // of `colWidth` px per column, and the amplitude mapping (`mid` the
    // center-line y, `half` the pixels per unit amplitude).
    void paintBars(QPainter &p, const QColor &color, double colWidth,
                   double start, qsizetype g0, qsizetype g1, int mid,
                   float half) const;
    void paintLine(QPainter &p, const QColor &color, double colWidth,
                   double start, qsizetype g0, qsizetype g1, int mid,
                   float half) const;
    void paintBoxes(QPainter &p, const QColor &color, const QColor &bg,
                    double colWidth, double start, qsizetype g0, qsizetype g1,
                    int mid, float half) const;

    PlaybackManager* m_pm;
    int m_capacity = 256;       // display columns kept
    int m_windowMs = kDefaultWindowMs;  // the window's audio span (ms)
    int m_ringSize = 512;       // the ring's slots (2 * the capacity)
    QVector<float> m_ring;      // the ring's min/max pairs
    qsizetype m_colCount = 0;   // closed display columns (absolute)
    // The re-bucketing: the tap columns (8 frames each) accumulated
    // into the open display column.
    quint32 m_rate = 0;         // the tap's sample rate
    int m_tapsPerColumn = 0;    // tap columns per display column
    qsizetype m_accCols = 0;
    // The open display column's running min/max (the identity for an
    // empty accumulator).
    float m_accMn = 1.0F;
    float m_accMx = -1.0F;
    TraceStyle m_traceStyle = TraceStyle::Bars;
    QTimer m_paintTimer;     // the paint's rhythm (see the class comment)
};

#endif // OSCILLOGRAMWIDGET_H
