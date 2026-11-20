#ifndef SPECTROGRAMWIDGET_H
#define SPECTROGRAMWIDGET_H

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>

#include <array>

#include "basewidget.h"
#include "playbackmanager.h"
#include "styleproperty.h"

class QMenu;
class QPainter;

// One stop of a color map's gradient (a position 0..1 and the color at
// it): the maps are a linear RGB interpolation between their stops.
struct GradientStop
{
    float pos;
    float r;
    float g;
    float b;
};

// A spectrogram (a waterfall display): the driver's raw-frame taps
// (audioFrames) are mixed to mono and accumulated into a window
// (the FFT's size, a per-instance option — the larger it is, the finer
// the frequency resolution (the sample rate over the size) and the
// coarser the time resolution (the size over the sample rate)); each
// closed window is windowed (Hann) and FFT'd and becomes one column of
// an intensity image the size of the widget itself — the vertical axis
// is the frequency (linear, 0 at the bottom up to the lower of 20 kHz
// and the Nyquist at the top, one row per pixel, so the image never
// carries more resolution than the widget shows) and the horizontal
// axis is time.
//
// The display mode (an edit-mode option) is Scroll (the default): the
// image is a ring of the widget's columns — each closed window writes
// its column into the ring's next slot and the visible window advances
// one (a new column costs its own height, no copy) — so the waterfall
// scrolls at the audio's rate (one column per window, the window's
// audio span wide in time). Sweep: the image is
// fixed and a line moves across it one column per closed window,
// rewriting the column it passes; the whole image decays toward silence
// on the paint ticks (a persistence of kSweepHalfLifeMs, rescaled to
// the paint rate), so the trail behind the line fades out.
//
// A level is the bin's magnitude on a dB scale (0 dB = full scale, the
// "x-floor-db" property = silence, -60 by default). The color of an
// intensity is a color map (the "x-cmap" property): mono (the default
// — the widget's background up to its bar color, so the sheet's
// background/color recolors the gradient) or one of the fixed maps
// (heat, fire, ice, viridis; an unknown name falls back to mono).
//
// A pause or stop freezes the image (the taps and the paint timer stop
// with the state — the waterfall stands still — and the next play
// continues over it: the display's history is not the stream's, so
// nothing clears it). A resize re-grids it the same way: the paint
// stretches the old image to the widget, and a debounce timer
// (kResizeDebounceMs, reset by every resize event) rebuilds it at the
// settled size — the old columns resampled into the new one, not
// cleared — so a drag-resize costs one rebuild per settled pause. A
// resize with no image to preserve takes the new size at once.
// The paint's rhythm is a user setting (visualization.fps, see
// PaintRate): while playing, the timer runs at the configured rate and
// each tick paints one step of the decay (Sweep mode) — the taps only
// feed the windows. The paint is a lookup-table fill of the image-
// sized canvas (a table entry per intensity level, no per-pixel color
// math) and a tick repaints only when the image changed since the
// last paint — the cost is the canvas' pixel count, not the rate's.
class SpectrogramWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "spectrogram";
    inline static const QString title = "Spectrogram";
    inline static const QString tag = widgetTags::visualization;

    // The minimum size: the image is painted into the widget itself
    // (it has no children to carry a layout minimum, like the other
    // custom-painted visualizations) — the floor keeps a splitter from
    // collapsing it to nothing.
    static inline const QSize kMinimumSize{24, 24};

    // The window's sample count (the FFT's size, a power of two): one
    // closed window is one image column, so a column's time resolution
    // is the window's audio span (the size over the sample rate) and
    // the frequency resolution the sample rate over the size. The
    // size is an edit-mode option (setWindowSize, the allowed ones
    // kWindowSizeOptions).
    static constexpr int kDefaultWindowSize = 512;
    static constexpr std::array<int, 4> kWindowSizeOptions = {
        256, 512, 1024, 4096,
    };
    // The frequency ceiling (the lower of it and the Nyquist): the row
    // at the top of the image.
    static constexpr double kMaxFreq = 20000.0;
    // The level's dB floor (silence) — the "x-floor-db" default.
    static constexpr int kDefaultFloorDb = -60;
    // The sweep image's persistence: the time an intensity takes to
    // halve between the line's passes.
    static constexpr int kSweepHalfLifeMs = 2000;
    // The resize's debounce: a resize stretches the old image and the
    // rebuild runs only once no resize event has arrived for this long
    // (a drag-resize is a stream of events; each one resets the timer).
    static constexpr int kResizeDebounceMs = 1500;

    // How the image is refreshed (an edit-mode option): Scroll (the
    // waterfall) or Sweep (the moving line, the decaying trail).
    enum class Mode { Scroll, Sweep };
    Mode displayMode() const { return m_mode; }
    void setDisplayMode(Mode mode);

    // The window's sample count (an edit-mode option): the FFT's size.
    int windowSize() const { return m_windowSize; }
    void setWindowSize(int size);

    // The "x-" style properties this widget reads (see StyleManager):
    // the style sheet editor's help documents them from here.
    static QList<StyleProperty> styleProperties();

    explicit SpectrogramWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration &config) override;

    // The displayed intensities in logical order (row-major, 0..1, the
    // ring unwound from its origin), the sweep's next column (Sweep
    // mode), the tap's accumulation and the sample rate (the test
    // seams), and the paint timer's interval (ms) and state.
    QVector<float> image() const;
    int sweepX() const { return m_sweepX; }
    quint32 sampleRate() const { return m_rate; }
    int accumulatedSamples() const
    {
        return static_cast<int>(m_accLeft.size());
    }
    int paintIntervalMs() const { return m_timer.interval(); }
    bool paintTimerActive() const { return m_timer.isActive(); }

protected:
    // The image is painted into the widget itself: the style scaffold
    // is the bare "#name" rule, no "#name *" for children.
    bool selfPainted() const override { return true; }

    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onFrames(const QVector<float> &frames, quint32 sampleRate);
    void onStateChanged(PlaybackManager::State state);
    // One paint step: one decay step of the image (Sweep mode) and the
    // repaint (the timer's tick).
    void tick();
    // The resize has settled (the debounce's timeout): rebuild the
    // image at the widget's new size (the old columns resampled in).
    void onResizeSettled();
    void onSettingChanged(const QString &key, const QVariant &value);

private:
    // The timer's interval at the configured paint rate, and the
    // per-tick decay factor rescaled to match it (see the class
    // comment).
    void applyPaintRate();
    // Rebuilds the image and the row->bin mapping for the current size
    // (and rate, when known): a resize (the debounce's timeout), a rate
    // or a window-size change — the old columns resampled into the new
    // one (an empty old image is a fresh silent one).
    void rebuild();
    // One closed window: the mono mix's column intensities, written per
    // the display mode.
    void processWindow(const QVector<float> &left,
                       const QVector<float> &right);
    // The row->bin mapping for a size, a rate and a window size: per
    // row (0 at the top, the highest frequency), the index of the
    // nearest FFT bin (see the class comment).
    static QVector<int> rowBins(const QSize &size, quint32 rate,
                                int windowSize);
    // Resamples an old image (a ring of w0 x h0, the leftmost visible
    // column at `origin`) into a fresh one of w1 x h1: bilinear over
    // the old pixels (a column's time, a row's frequency), the ring
    // unwound first — a re-grid keeps the history stretched to the new
    // grid, instead of clearing it.
    static QVector<float> resample(const QVector<float> &old, int w0,
                                   int h0, int origin, int w1, int h1);
    // The color of an intensity 0..1 in a list of gradient stops
    // (positions 0..1, a linear RGB interpolation between them).
    static QRgb lerpStops(const QList<GradientStop> &stops, float t);

    PlaybackManager* m_pm;
    QVector<float> m_image;  // the intensities (row-major, a ring of columns)
    QImage m_canvas;         // the painted image (the intensities' colors)
    QVector<int> m_rowBin;   // per row: the FFT bin the row samples
    int m_origin = 0;        // the ring's leftmost visible column (Scroll)
    int m_sweepX = 0;        // the sweep's next column (Sweep mode)
    QVector<float> m_accLeft;  // the tap's accumulation (per channel)
    QVector<float> m_accRight;
    quint32 m_rate = 0;
    int m_windowSize = kDefaultWindowSize;  // the FFT's size (an option)
    float m_decay = 0.99F;  // per tick (rescaled to the paint rate)
    bool m_dirty = false;   // the image changed since the last paint
    QTimer m_timer;
    QTimer m_resizeTimer;   // the resize's debounce (single-shot)
    Mode m_mode = Mode::Scroll;
};

#endif // SPECTROGRAMWIDGET_H
