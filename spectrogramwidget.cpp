#include "spectrogramwidget.h"

#include "fft.h"
#include "paintrate.h"
#include "settingsmanager.h"
#include "vizwidget.h"

#include <QImage>
#include <QMenu>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace {

constexpr float kPi = 3.14159265358979323846F;

// The fixed color maps (the named stops of the "x-cmap" property): the
// mono map's endpoints come from the widget's palette at paint time
// (the sheet's background/color recolors it), so it is not a fixed map.
QList<GradientStop> colorMap(const QString &name)
{
    if (name == QLatin1String("heat"))
        return {{0.0F, 0, 0, 0},
                {0.5F, 255, 0, 0},
                {0.75F, 255, 255, 0},
                {1.0F, 255, 255, 255},
        };
    if (name == QLatin1String("fire"))
        return {{0.0F, 0, 0, 0},
                {0.25F, 128, 0, 0},
                {0.5F, 255, 64, 0},
                {0.75F, 255, 200, 0},
                {1.0F, 255, 255, 200},
        };
    if (name == QLatin1String("ice"))
        return {{0.0F, 0, 0, 0},
                {0.35F, 0, 64, 160},
                {0.7F, 0, 160, 255},
                {1.0F, 200, 255, 255},
        };
    if (name == QLatin1String("viridis"))
        return {{0.0F, 68, 1, 84},
                {0.25F, 59, 82, 139},
                {0.5F, 33, 145, 140},
                {0.75F, 94, 201, 98},
                {1.0F, 253, 231, 37},
        };
    return {};
}

// One row's intensity from the window's magnitudes: the row's bin on
// the dB scale (0 dB = full scale, the floor = silence) mapped linearly
// between the two.
float columnLevel(const QVector<float> &mags, int bin, int n, int floorDb)
{
    const float mag = mags.value(bin, 0.0F);
    if (mag <= 0.0F)
        return 0.0F;
    // The normalization: a Hann-windowed full-scale sine at a bin
    // center produces a magnitude of n/4, which reads as 0 dB (like
    // the spectrum's bands).
    const double db = 20.0 * std::log10(4.0 * mag / n);
    if (floorDb >= 0)
        return db >= 0.0 ? 1.0F : 0.0F;
    return static_cast<float>(
        qBound(0.0, (db - floorDb) / -static_cast<double>(floorDb), 1.0));
}

}  // namespace

SpectrogramWidget::SpectrogramWidget(QWidget *parent)
    : BaseWidget(parent)
{
    setMinimumSize(kMinimumSize);

    m_pm = PlaybackManager::getInstance();

    // The paint tick (a user setting): while playing, the timer runs at
    // the configured paint rate and each tick paints one step of the
    // decay (Sweep mode) — the taps only feed the windows.
    PaintRate::ensureRegistered();
    applyPaintRate();
    connect(&m_timer, &QTimer::timeout, this, &SpectrogramWidget::tick);
    connect(SettingsManager::getInstance(), &SettingsManager::changed,
            this, &SpectrogramWidget::onSettingChanged);

    connect(m_pm, &PlaybackManager::audioFrames, this, &SpectrogramWidget::onFrames);
    connect(m_pm, &PlaybackManager::stateChanged, this, &SpectrogramWidget::onStateChanged);

    // A resize settles into a rebuild (the debounce's timeout): the
    // paint stretches the old image to the widget until then.
    connect(&m_resizeTimer, &QTimer::timeout,
            this, &SpectrogramWidget::onResizeSettled);

    // A widget inserted mid-playback must start its paint timer; the
    // signal only fires for future changes.
    onStateChanged(m_pm->state());
}

QList<StyleProperty> SpectrogramWidget::styleProperties()
{
    return {
        {
            QStringLiteral("x-cmap"),
            title,
            QStringLiteral("The color map of the intensity gradient: "
                           "mono (the widget's background up to its "
                           "bar color), heat, fire, ice or viridis. An "
                           "unknown name falls back to mono."),
            QStringLiteral("mono"),
            QStringLiteral(""),
            0,
            0,
        },
        {
            QStringLiteral("x-floor-db"),
            title,
            QStringLiteral("The dB level below which the image is its "
                           "darkest color (the contrast's floor; 0 dB "
                           "is full scale)."),
            QStringLiteral("-60"),
            QStringLiteral("dB"),
            -100,
            0,
        },
    };
}

void SpectrogramWidget::onFrames(const QVector<float> &frames,
                                 quint32 sampleRate)
{
    // The tap is latency-shifted (the frames only reach the speaker
    // after the stream's latency), so a queued tap can land after a
    // pause or stop: the spectrogram only follows live playback.
    if (m_pm->state() != PlaybackManager::Playing)
        return;
    // A new stream (a new track) may carry a different rate: the bin
    // grid changes, so a partial window of the old rate must not mix
    // into the new one, and the rows' bins follow the rate.
    if (sampleRate != m_rate) {
        m_rate = sampleRate;
        m_accLeft.clear();
        m_accRight.clear();
        rebuild();
    }
    const qsizetype n = frames.size() / 2;
    for (qsizetype i = 0; i < n; ++i) {
        m_accLeft.append(frames[2 * i]);
        m_accRight.append(frames[(2 * i) + 1]);
    }
    // Each closed window (the FFT's size samples) is one image column:
    // the waterfall's cadence is the audio's rate.
    while (m_accLeft.size() >= m_windowSize) {
        processWindow(m_accLeft.mid(0, m_windowSize),
                      m_accRight.mid(0, m_windowSize));
        m_accLeft.remove(0, m_windowSize);
        m_accRight.remove(0, m_windowSize);
    }
}

void SpectrogramWidget::processWindow(const QVector<float> &left,
                                      const QVector<float> &right)
{
    if (m_image.isEmpty() || m_rowBin.isEmpty())
        return;  // no size or rate yet
    // The mono mix (the per-sample average of left and right, like the
    // spectrum's Mixed), Hann-windowed (leakage control: a full-scale
    // sine at a bin center then produces a magnitude of n/4, which the
    // normalization below reads as 0 dB).
    const int n = static_cast<int>(left.size());
    QVector<float> samples = left;
    for (int i = 0; i < n; ++i)
        samples[i] = 0.5F * (samples[i] + right[i])
            * (0.5F * (1.0F - std::cos(2.0F * kPi * static_cast<float>(i)
                                        / static_cast<float>(n - 1))));
    const QVector<float> mags = fft::magnitudes(samples);
    const int floorDb = styleInt("x-floor-db", kDefaultFloorDb, -100, 0);

    const int w = static_cast<int>(m_image.size() / m_rowBin.size());
    const int h = static_cast<int>(m_rowBin.size());
    if (m_mode == Mode::Scroll) {
        // The waterfall: the ring's next slot becomes the visible
        // window's right edge once the window advances one — the column
        // lands there (no copy: the scroll is an index, the image is a
        // ring).
        const int col = m_origin;
        for (int r = 0; r < h; ++r)
            m_image[(r * w) + col] =
                columnLevel(mags, m_rowBin.at(r), n, floorDb);
        m_origin = (m_origin + 1) % w;
    } else {
        // The sweep: the line rewrites its column and advances one.
        for (int r = 0; r < h; ++r)
            m_image[(r * w) + m_sweepX] =
                columnLevel(mags, m_rowBin.at(r), n, floorDb);
        m_sweepX = (m_sweepX + 1) % w;
    }
    // The taps do not paint themselves: the (running) paint timer shows
    // the new column on its next tick, like the spectrum's.
    m_dirty = true;
}

QVector<float> SpectrogramWidget::image() const
{
    // The logical order (the test's view): the ring unwound from its
    // origin (the sweep's origin is 0, so its buffer is already the
    // logical one).
    if (m_origin == 0)
        return m_image;
    const int w = static_cast<int>(m_image.size() / m_rowBin.size());
    const int h = static_cast<int>(m_rowBin.size());
    QVector<float> out(m_image.size());
    for (int r = 0; r < h; ++r)
        for (int x = 0; x < w; ++x) {
            int phys = m_origin + x;
            if (phys >= w)
                phys -= w;
            out[(r * w) + x] = m_image[(r * w) + phys];
        }
    return out;
}

QVector<int> SpectrogramWidget::rowBins(const QSize &size, quint32 rate,
                                        int windowSize)
{
    const int h = size.height();
    QVector<int> bins(static_cast<qsizetype>(h), 0);
    if (h <= 0 || rate == 0)
        return bins;
    // Row r (0 at the top) covers the frequency band
    // [fMax * (h - r - 1) / h, fMax * (h - r) / h) — its center samples
    // the nearest FFT bin (a bin k is centered on k * rate / n Hz).
    const double nyquist = static_cast<double>(rate) / 2.0;
    const double fMax = qMin(kMaxFreq, nyquist);
    const int n = windowSize;
    const int half = n / 2;
    for (int r = 0; r < h; ++r) {
        const double freq = fMax * (h - r - 0.5) / h;
        bins[r] = qBound(0,
                         static_cast<int>(std::lround(freq * n / rate)),
                         half);
    }
    return bins;
}

QVector<float> SpectrogramWidget::resample(const QVector<float> &old,
                                           int w0, int h0, int origin,
                                           int w1, int h1)
{
    QVector<float> out(static_cast<qsizetype>(w1 * h1), 0.0F);
    if (w1 <= 0 || h1 <= 0 || w0 <= 0 || h0 <= 0 || old.isEmpty())
        return out;
    // Bilinear over the old pixels (a column's time, a row's
    // frequency), the ring unwound: a column's sample is `origin`-
    // shifted, wrapping.
    for (int r = 0; r < h1; ++r) {
        const double fy =
            (h1 > 1) ? static_cast<double>(r) * (h0 - 1) / (h1 - 1) : 0.0;
        const int y0 = qBound(0, static_cast<int>(std::floor(fy)), h0 - 1);
        const int y1 = qBound(0, y0 + 1, h0 - 1);
        const double ty = fy - y0;
        for (int x = 0; x < w1; ++x) {
            const double fx =
                (w1 > 1) ? static_cast<double>(x) * (w0 - 1) / (w1 - 1) : 0.0;
            const int x0 = qBound(0, static_cast<int>(std::floor(fx)), w0 - 1);
            const int x1 = qBound(0, x0 + 1, w0 - 1);
            const double tx = fx - x0;
            auto at = [&](int y, int xi) {
                return old[static_cast<qsizetype>(y * w0)
                           + ((origin + xi) % w0)];
            };
            const double top =
                (at(y0, x0) * (1.0 - tx)) + (at(y0, x1) * tx);
            const double bottom =
                (at(y1, x0) * (1.0 - tx)) + (at(y1, x1) * tx);
            out[static_cast<qsizetype>(r * w1) + x] =
                static_cast<float>((top * (1.0 - ty)) + (bottom * ty));
        }
    }
    return out;
}

void SpectrogramWidget::onStateChanged(PlaybackManager::State state)
{
    if (viz::runningFor(state, &m_timer))
        return;
    // A pause or stop is silence: the image freezes (it is not cleared
    // — the next play continues over it, and nothing feeds it while the
    // state is not Playing: the taps are dropped and the paint timer is
    // off, so it stands still) and the tap's partial window is dropped,
    // so the old stream's tail cannot mix into the next track's first
    // column.
    m_accLeft.clear();
    m_accRight.clear();
    update();
}

void SpectrogramWidget::onResizeSettled()
{
    rebuild();
    update();
}

void SpectrogramWidget::rebuild()
{
    // The image is the widget's own size (the resolution is the
    // display's, never more). A re-grid (a resize, a rate or a
    // window-size change) resamples the old columns into the new one
    // (the history stretches to the new grid, the ring re-anchors at
    // the left edge) instead of clearing, and settles any pending
    // resize's debounce (the image now matches the size).
    const int h0 = static_cast<int>(m_rowBin.size());
    const int w0 = (h0 > 0) ? static_cast<int>(m_image.size() / h0) : 0;
    const int w = width();
    const int h = height();
    m_image = resample(m_image, w0, h0, m_origin, w, h);
    m_canvas = QImage(w, h, QImage::Format_RGB32);
    m_rowBin = (m_rate != 0)
                   ? rowBins(QSize(w, h), m_rate, m_windowSize)
                   : QVector<int>();
    m_origin = 0;  // the ring re-anchors (the resample unwound it)
    m_sweepX = (w0 > 0 && w > 0) ? qBound(0, m_sweepX * w / w0, w - 1) : 0;
    m_resizeTimer.stop();
    m_dirty = true;
}

void SpectrogramWidget::tick()
{
    // The trail's persistence (Sweep mode only): one decay step toward
    // silence (settled pixels stay put, so a faded trail costs
    // nothing). The repaint runs only when the image changed since the
    // last paint — in Scroll mode that is a new column (the audio's
    // rate), so the timer's ticks in between cost nothing.
    if (m_mode == Mode::Sweep) {
        bool changed = false;
        for (float &level : m_image) {
            if (level <= 0.0F)
                continue;
            level *= m_decay;
            if (level < 0.002F)
                level = 0.0F;
            changed = true;
        }
        m_dirty = m_dirty || changed;
    }
    if (m_dirty) {
        m_dirty = false;
        update();
    }
}

void SpectrogramWidget::onSettingChanged(const QString &key,
                                         const QVariant &value)
{
    if (key != PaintRate::key())
        return;
    applyPaintRate();
}

void SpectrogramWidget::applyPaintRate()
{
    PaintRate::applyTo(&m_timer);
    // The persistence is per unit of time, not per tick: rescale the
    // per-tick factor (a halving per kSweepHalfLifeMs at the default
    // rate) so the trail fades in the same time at any paint rate.
    m_decay = static_cast<float>(
        PaintRate::decayFactor(m_timer.interval(), 0.5, kSweepHalfLifeMs));
}

void SpectrogramWidget::setDisplayMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    m_origin = 0;  // the ring starts from the left on a mode switch
    m_sweepX = 0;  // the line starts from the left on a mode switch
    m_dirty = true;
    update();
}

void SpectrogramWidget::setWindowSize(int size)
{
    if (size == m_windowSize)
        return;
    m_windowSize = size;
    // The bin grid moves with the window: the tap's accumulation
    // restarts (a partial window of the old grid must not mix into the
    // new one) and the image re-grids (the old columns resampled in,
    // stretched to the new grid).
    m_accLeft.clear();
    m_accRight.clear();
    rebuild();
    update();
}

QMenu* SpectrogramWidget::widgetContextMenu()
{
    // Start from the base menu (the toolbar entries, when a toolbar
    // exists) and add this widget's own actions.
    QMenu* menu = viz::editMenu(BaseWidget::widgetContextMenu(), this, title);

    // The display mode: an exclusive checkable group, rebuilt from the
    // current state on every menu.
    viz::optionGroup<Mode>(this, menu, {Mode::Scroll, Mode::Sweep},
                           [](Mode mode) {
                               return mode == Mode::Scroll
                                          ? QStringLiteral("Scroll")
                                          : QStringLiteral("Sweep");
                           },
                           m_mode,
                           [this](Mode mode) { setDisplayMode(mode); });

    menu->addSeparator();

    // The window size (the FFT's size): an exclusive checkable group,
    // rebuilt from the current state on every menu.
    viz::optionGroup<int>(this, menu, kWindowSizeOptions,
                          [](int size) {
                              return QStringLiteral("%1 samples").arg(size);
                          },
                          m_windowSize,
                          [this](int size) { setWindowSize(size); });

    return menu;
}

WidgetConfiguration SpectrogramWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only non-default states are persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the defaults.
    if (m_mode != Mode::Scroll)
        config["mode"] = static_cast<int>(m_mode);
    if (m_windowSize != kDefaultWindowSize)
        config["windowSize"] = m_windowSize;
    return config;
}

void SpectrogramWidget::restoreWidget(WidgetConfiguration& config)
{
    BaseWidget::restoreWidget(config);
    // A stale value outside the enum range is ignored (the menu only
    // ever passes valid ones).
    if (config.contains("mode")) {
        const int value = config.value("mode").toInt();
        if (value >= 0 && value <= static_cast<int>(Mode::Sweep))
            setDisplayMode(static_cast<Mode>(value));
    }
    // A stale size outside the offered ones is ignored (the menu only
    // ever passes valid ones).
    if (config.contains("windowSize")) {
        const int value = config.value("windowSize").toInt();
        if (std::find(kWindowSizeOptions.begin(), kWindowSizeOptions.end(),
                      value) != kWindowSizeOptions.end())
            setWindowSize(value);
    }
}

void SpectrogramWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    // The image is the widget's own size, but the re-grid is not per
    // pixel: with no image to preserve the new size is taken at once,
    // and with one the paint stretches the old image to the widget
    // until the debounce timer (reset by every event) rebuilds it at
    // the settled size, the old columns resampled in.
    if (m_image.isEmpty()) {
        rebuild();
    } else {
        m_resizeTimer.start(kResizeDebounceMs);
    }
    update();
}

QRgb SpectrogramWidget::lerpStops(const QList<GradientStop> &stops, float t)
{
    if (stops.isEmpty())
        return qRgb(0, 0, 0);
    if (t <= stops.first().pos)
        return qRgb(qRound(stops.first().r), qRound(stops.first().g),
                    qRound(stops.first().b));
    if (t >= stops.last().pos)
        return qRgb(qRound(stops.last().r), qRound(stops.last().g),
                    qRound(stops.last().b));
    for (int i = 1; i < stops.size(); ++i) {
        const GradientStop &a = stops.at(i - 1);
        const GradientStop &b = stops.at(i);
        if (t <= b.pos) {
            const float f = (t - a.pos) / (b.pos - a.pos);
            return qRgb(qRound(a.r + ((b.r - a.r) * f)),
                        qRound(a.g + ((b.g - a.g) * f)),
                        qRound(a.b + ((b.b - a.b) * f)));
        }
    }
    return qRgb(qRound(stops.last().r), qRound(stops.last().g),
                qRound(stops.last().b));
}

void SpectrogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    // The styling surfaces: a stylesheet's "background" recolors the
    // background and "color" the loud end of the mono gradient (the
    // theme's roles stand in without a rule); "x-cmap" swaps the
    // gradient for a fixed map.
    const QColor bg = palette().color(QPalette::Base);
    const QColor loud = styleColor(QPalette::WindowText, QPalette::Highlight);
    p.fillRect(rect(), bg);

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0 || m_image.isEmpty() || m_canvas.isNull()
        || m_rowBin.isEmpty())
        return;

    // The canvas is the image's own size: the widget's, until a resize
    // settles (then the old one, stretched below).
    const int iw = m_canvas.width();
    const int ih = m_canvas.height();
    if (iw <= 0 || ih <= 0)
        return;

    // The gradient as a 256-entry lookup table: a per-pixel walk of the
    // stops' list would be the paint's cost, and 8 bits is the image's
    // own precision (the mono map runs from the background to the bar
    // color, the named maps carry their own stops, an unknown name
    // falls back to mono).
    QList<GradientStop> stops = colorMap(styleValue("x-cmap", "mono"));
    if (stops.isEmpty())
        stops = {{0.0F, bg.redF() * 255.0F, bg.greenF() * 255.0F,
                  bg.blueF() * 255.0F,
                 },
                 {1.0F, loud.redF() * 255.0F, loud.greenF() * 255.0F,
                  loud.blueF() * 255.0F,
                 },
        };

    std::array<QRgb, 256> lut;
    for (int i = 0; i < 256; ++i)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        lut[i] = lerpStops(stops, static_cast<float>(i) / 255.0F);

    // The canvas is the ring unwound from its origin (the sweep's
    // origin is 0, so the wrap is a no-op there).
    for (int r = 0; r < ih; ++r) {
        // The canvas' row is the ring's row reinterpreted as pixels
        // (a 32-bit format's scan line is exactly its QRgb row).
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        QRgb* line = reinterpret_cast<QRgb*>(m_canvas.scanLine(r));
        const float* row = m_image.data() + static_cast<std::ptrdiff_t>(r * iw);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        for (int x = 0; x < iw; ++x) {
            int phys = m_origin + x;
            if (phys >= iw)
                phys -= iw;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index)
            const int level = static_cast<int>(std::lround(row[phys] * 255.0F));
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic, cppcoreguidelines-pro-bounds-constant-array-index)
            line[x] = lut[level];
        }
    }
    if (iw == w && ih == h) {
        p.drawImage(0, 0, m_canvas);
    } else {
        // A resize is in flight (the debounce's rebuild is pending):
        // stretch the old image to the widget until it lands.
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(rect(), m_canvas);
    }

    if (m_mode == Mode::Sweep) {
        // The line: the sweep's next column (where the next window
        // lands), scaled to the widget when the image is stretched.
        const int x = qBound(0, m_sweepX * w / iw, w - 1);
        p.setPen(QPen(loud));
        p.drawLine(x, 0, x, h - 1);
    }
    m_dirty = false;
}
