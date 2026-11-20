#ifndef SPECTRUMWIDGET_H
#define SPECTRUMWIDGET_H

#include <QList>
#include <QPair>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>

#include <array>

#include "barpaint.h"
#include "basewidget.h"
#include "playbackmanager.h"

class QMenu;
class QPainter;

// A frequency spectrum: the driver's raw-frame taps (audioFrames) are
// accumulated into a window (kWindowSize samples, the FFT's size) and
// each closed window is windowed (Hann) and FFT'd (the core's fft, a
// self-contained radix-2) into log-spaced bands (20 Hz up to the lower
// of 20 kHz and the Nyquist, the band count an edit-mode option) — each
// band's level is the average magnitude of the bins it overlaps (a bin
// straddling a band's edge joins it in proportion — the low bands are
// narrower than a bin at common rates) on a dB scale (0 dB = full
// scale, kFloorDb = silence). Each new window jumps the bands it
// reaches (fast attack) and a timer decays them toward silence (the
// release, like the peak meter's: per unit of time, rescaled to the
// paint rate). A pause or stop clears the bands immediately (silence is
// silence). The analyzed channel (Mixed by default — the per-sample
// average of left and right — or Left / Right), the fill direction and
// the display style are edit-mode options (see widgetContextMenu()).
//
// The paint's rhythm is a user setting (visualization.fps, see
// PaintRate): while playing, the timer runs at the configured rate and
// each tick paints one step of the release — the taps only feed the
// windows.
//
// The bars' rendering is tuned by style-sheet custom properties (see
// StyleManager): "x-segments" — the number of discrete blocks the
// "Segments" display style splits each bar into along the fill axis
// (like the peak meter's), "x-bar-margin" — the space between and
// around the bars (auto: a fixed share of the across-axis), and
// "x-track-color" — a bar's unfilled part (auto: the background
// lightened).
class SpectrumWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "spectrum";
    inline static const QString title = "Spectrum";
    inline static const QString tag = widgetTags::visualization;

    // The minimum size: the bars are painted into the widget itself
    // (it has no children to carry a layout minimum, like the
    // component-built widgets do) — the floor keeps a splitter from
    // collapsing the meter to nothing.
    static inline const QSize kMinimumSize{24, 24};

    // The window's sample count (the FFT's size, a power of two): one
    // closed window is one spectrum, so the analysis's time resolution
    // is the window's audio span (kWindowSize over the sample rate).
    static constexpr int kWindowSize = 1024;
    // The band counts the edit menu offers (the default, 16, keeps the
    // bars wide enough on a small widget and the grid fine enough for
    // the low end).
    static constexpr int kDefaultBands = 16;
    static constexpr std::array<int, 3> kBandOptions = {8, 16, 32};
    // The bands' frequency span (log-spaced from kMinFreq to the lower
    // of kMaxFreq and the Nyquist) and the level's dB floor (silence).
    static constexpr double kMinFreq = 20.0;
    static constexpr double kMaxFreq = 20000.0;
    static constexpr double kFloorDb = -60.0;

    // The direction the bars fill (like the peak meter's): Up/Down draw
    // vertical bars (from the bottom / from the top),
    // LeftToRight/RightToLeft draw horizontal bars (from the left / from
    // the right). Up is the default.
    using Orientation = barpaint::Orientation;
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation orientation);

    // How the bars render (like the peak meter's): Solid (the default,
    // one filled rect per bar) or Segments (each bar split into
    // discrete blocks along the fill axis, "x-segments").
    enum class Style { Solid, Segments };
    Style displayStyle() const { return m_displayStyle; }
    void setDisplayStyle(Style style);

    // The analyzed channel: Mixed (the default, the per-sample average
    // of left and right), Left or Right.
    enum class Channel { Mixed, Left, Right };
    Channel channel() const { return m_channel; }
    void setChannel(Channel channel);

    // The number of bands (an edit-mode option; a change resets the
    // levels — the old grid no longer lines up with the new one, the
    // next closed window refills them).
    int bandCount() const { return m_bands; }
    void setBandCount(int bands);

    // The "x-" style properties this widget reads (see StyleManager):
    // the shared bar rendering's, under this widget's title — the
    // style sheet editor's help documents them from here.
    static QList<StyleProperty> styleProperties();

    explicit SpectrumWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    // The currently displayed (decayed) band levels, 0..1 (the test
    // seam), and the state of the tap's accumulation.
    QVector<float> bandLevels() const { return m_levels; }
    quint32 sampleRate() const { return m_rate; }
    int accumulatedSamples() const { return static_cast<int>(m_accLeft.size()); }
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
    void onFrames(const QVector<float> &frames, quint32 sampleRate);
    void onStateChanged(PlaybackManager::State state);
    // One step of the levels' release (the timer's tick) and the paint.
    void decay();
    void onSettingChanged(const QString &key, const QVariant &value);

private:
    // The timer's interval at the configured paint rate, and the
    // per-tick decay factor rescaled to match it (see the class
    // comment).
    void applyPaintRate();
    // Clears the levels and the tap's accumulation (a pause or stop).
    void clear();
    // One closed window: the band levels of the analyzed channel,
    // attacked onto the displayed ones.
    void processWindow(const QVector<float> &left, const QVector<float> &right);
    // The log-spaced band levels of one window (see the class comment):
    // pure, unit-testable through the fft helpers.
    static QVector<float> bandLevelsFrom(const QVector<float> &samples,
                                         quint32 rate, int bands);

    PlaybackManager* m_pm;
    // The displayed (decayed) band levels (the default grid, silent).
    QVector<float> m_levels =
        QVector<float>(static_cast<qsizetype>(kDefaultBands), 0.0F);
    QVector<float> m_accLeft;  // the tap's accumulation (per channel)
    QVector<float> m_accRight;
    quint32 m_rate = 0;
    float m_decay = 0.85F;  // per tick (0.85 per 16 ms at the default rate)
    QTimer m_timer;
    Style m_displayStyle = Style::Solid;
    Orientation m_orientation = Orientation::Up;
    Channel m_channel = Channel::Mixed;
    int m_bands = kDefaultBands;
};

#endif // SPECTRUMWIDGET_H
