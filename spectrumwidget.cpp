#include "spectrumwidget.h"

#include "barpaint.h"
#include "fft.h"
#include "paintrate.h"
#include "settingsmanager.h"
#include "vizwidget.h"

#include <QMenu>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846F;

QString channelName(SpectrumWidget::Channel channel)
{
    if (channel == SpectrumWidget::Channel::Left)
        return QStringLiteral("Left");
    if (channel == SpectrumWidget::Channel::Right)
        return QStringLiteral("Right");
    return QStringLiteral("Mixed");
}

} // namespace

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : BaseWidget(parent)
{
    setMinimumSize(kMinimumSize);

    m_pm = PlaybackManager::getInstance();

    // The paint and release tick (a user setting): while playing, the
    // timer runs at the configured paint rate and each tick paints one
    // step of the release — the taps only feed the windows — so the
    // cadence is steady and the user's choice, not the driver's.
    PaintRate::ensureRegistered();
    applyPaintRate();
    connect(&m_timer, &QTimer::timeout, this, &SpectrumWidget::decay);
    connect(SettingsManager::getInstance(), &SettingsManager::changed,
            this, &SpectrumWidget::onSettingChanged);

    connect(m_pm, &PlaybackManager::audioFrames, this, &SpectrumWidget::onFrames);
    connect(m_pm, &PlaybackManager::stateChanged, this, &SpectrumWidget::onStateChanged);

    // A widget inserted mid-playback must start its decay timer; the
    // signal only fires for future changes.
    onStateChanged(m_pm->state());
}

QList<StyleProperty> SpectrumWidget::styleProperties()
{
    return barpaint::styleProperties(title);
}

void SpectrumWidget::onFrames(const QVector<float> &frames, quint32 sampleRate)
{
    // The tap is latency-shifted (the frames only reach the speaker
    // after the stream's latency), so a queued tap can land after a
    // pause or stop: the spectrum only follows live playback.
    if (m_pm->state() != PlaybackManager::Playing)
        return;
    // A new stream (a new track) may carry a different rate: the
    // window's bin grid changes, so a partial window of the old rate
    // must not mix into the new one.
    if (sampleRate != m_rate) {
        m_rate = sampleRate;
        m_accLeft.clear();
        m_accRight.clear();
    }
    const qsizetype n = frames.size() / 2;
    for (qsizetype i = 0; i < n; ++i) {
        m_accLeft.append(frames[2 * i]);
        m_accRight.append(frames[(2 * i) + 1]);
    }
    // Each closed window (kWindowSize samples) is one spectrum: the
    // bands jump to it (fast attack), the release timer handles the
    // fall.
    while (m_accLeft.size() >= kWindowSize) {
        processWindow(m_accLeft.mid(0, kWindowSize),
                      m_accRight.mid(0, kWindowSize));
        m_accLeft.remove(0, kWindowSize);
        m_accRight.remove(0, kWindowSize);
    }
}

void SpectrumWidget::processWindow(const QVector<float> &left,
                                   const QVector<float> &right)
{
    QVector<float> samples;
    if (m_channel == Channel::Mixed) {
        samples = left;
        for (qsizetype i = 0; i < samples.size(); ++i)
            samples[i] = 0.5F * (samples[i] + right[i]);
    } else {
        samples = (m_channel == Channel::Left) ? left : right;
    }
    const QVector<float> levels = bandLevelsFrom(samples, m_rate, m_bands);
    for (int i = 0; i < m_levels.size() && i < levels.size(); ++i)
        m_levels[i] = qMax(m_levels[i], levels[i]);
    // The taps do not paint themselves: the (running) release timer
    // shows the new levels on its next tick, like the peak meter's.
}

QVector<float> SpectrumWidget::bandLevelsFrom(const QVector<float> &samples,
                                              quint32 rate, int bands)
{
    const int n = static_cast<int>(samples.size());
    QVector<float> levels(static_cast<qsizetype>(bands), 0.0F);
    if (bands <= 0 || rate == 0 || !fft::isPowerOfTwo(n))
        return levels;

    // The Hann window (leakage control): a full-scale sine at a bin
    // center then produces a magnitude of n/4, which the normalization
    // below reads as 0 dB.
    QVector<std::complex<float>> data(static_cast<qsizetype>(n));
    for (int i = 0; i < n; ++i) {
        const float w = 0.5F
            * (1.0F - std::cos(2.0F * kPi * static_cast<float>(i)
                               / static_cast<float>(n - 1)));
        data[i] = std::complex<float>(samples[i] * w, 0.0F);
    }
    fft::transform(data, false);
    const int half = n / 2;

    // The bands: log-spaced from kMinFreq to the lower of kMaxFreq and
    // the Nyquist (a 20 kHz ceiling: above it the ear hears nothing).
    // A band's level is the average magnitude of the bins it overlaps:
    // a bin (centered on k * rate / n Hz, spanning one bin width)
    // straddling a band's edge joins it in proportion to the overlap —
    // the integration keeps the sub-bin-wide low bands alive when the
    // bin grid is coarser than a band.
    const double nyquist = static_cast<double>(rate) / 2.0;
    const double fMax = qMin(kMaxFreq, nyquist);
    if (fMax <= kMinFreq)
        return levels;  // a sample rate with no audible range
    const double ratio = std::log(fMax / kMinFreq);
    const double binWidth = static_cast<double>(rate) / n;  // Hz per bin
    for (int b = 0; b < bands; ++b) {
        const double fLo = kMinFreq * std::exp(ratio * b / bands);
        const double fHi = kMinFreq * std::exp(ratio * (b + 1) / bands);
        const double k1 = fLo / binWidth;  // the band's span in bin units
        const double k2 = fHi / binWidth;
        int kStart = static_cast<int>(std::floor(k1 - 0.5));
        int kEnd = static_cast<int>(std::ceil(k2 + 0.5));
        kStart = qBound(0, kStart, half);
        kEnd = qBound(0, kEnd, half + 1);
        double sum = 0.0;
        double weight = 0.0;
        for (int k = kStart; k < kEnd; ++k) {
            const double overlap = qMin(k + 0.5, k2) - qMax(k - 0.5, k1);
            if (overlap <= 0.0)
                continue;
            sum += static_cast<double>(std::abs(data[k])) * overlap;
            weight += overlap;
        }
        if (weight <= 0.0)
            continue;
        const double mag = sum / weight;
        if (mag <= 0.0)
            continue;
        // The normalization: 20*log10(4*mag/n) is 0 dB full scale (see
        // the window's comment), kFloorDb is silence; the levels map
        // linearly between the two.
        const double db = 20.0 * std::log10(4.0 * mag / n);
        levels[b] =
            static_cast<float>(qBound(0.0, (db - kFloorDb) / -kFloorDb, 1.0));
    }
    return levels;
}

void SpectrumWidget::onStateChanged(PlaybackManager::State state)
{
    if (viz::runningFor(state, &m_timer))
        return;
    // A pause or stop is silence: clear the bands and stop the release.
    clear();
    update();
}

void SpectrumWidget::clear()
{
    for (float &level : m_levels)
        level = 0.0F;
    m_accLeft.clear();
    m_accRight.clear();
}

void SpectrumWidget::decay()
{
    for (float &level : m_levels) {
        level *= m_decay;
        if (level < 0.002F)
            level = 0.0F;
    }
    update();
}

void SpectrumWidget::onSettingChanged(const QString &key, const QVariant &value)
{
    if (key != PaintRate::key())
        return;
    applyPaintRate();
}

void SpectrumWidget::applyPaintRate()
{
    PaintRate::applyTo(&m_timer);
    // The release is per unit of time, not per tick: rescale the
    // per-tick factor (0.85 per 16 ms at the default rate) so the bars
    // fall in the same time at any paint rate.
    m_decay = static_cast<float>(
        PaintRate::decayFactor(m_timer.interval(), 0.85, 16.0));
}

void SpectrumWidget::setDisplayStyle(Style style)
{
    if (style == m_displayStyle)
        return;
    m_displayStyle = style;
    update();
}

void SpectrumWidget::setOrientation(Orientation orientation)
{
    if (orientation == m_orientation)
        return;
    m_orientation = orientation;
    update();
}

void SpectrumWidget::setChannel(Channel channel)
{
    if (channel == m_channel)
        return;
    m_channel = channel;
    update();
}

void SpectrumWidget::setBandCount(int bands)
{
    if (bands == m_bands)
        return;
    m_bands = bands;
    // The old grid no longer lines up with the new one: the next closed
    // window refills the levels.
    m_levels = QVector<float>(static_cast<qsizetype>(bands), 0.0F);
    update();
}

QMenu* SpectrumWidget::widgetContextMenu()
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

    // The fill direction: an exclusive checkable group, rebuilt from
    // the current state on every menu.
    viz::optionGroup<Orientation>(
        this, menu,
        {Orientation::Up, Orientation::Down, Orientation::LeftToRight,
         Orientation::RightToLeft,
        },
        barpaint::orientationName, m_orientation,
        [this](Orientation orientation) { setOrientation(orientation); });

    menu->addSeparator();

    // The analyzed channel: an exclusive checkable group, rebuilt from
    // the current state on every menu.
    viz::optionGroup<Channel>(
        this, menu, {Channel::Mixed, Channel::Left, Channel::Right},
        channelName, m_channel,
        [this](Channel channel) { setChannel(channel); });

    menu->addSeparator();

    // The number of bands: an exclusive checkable group, rebuilt from
    // the current state on every menu.
    viz::optionGroup<int>(this, menu, kBandOptions,
                          [](int bands) {
                              return QStringLiteral("%1 bands").arg(bands);
                          },
                          m_bands,
                          [this](int bands) { setBandCount(bands); });

    return menu;
}

WidgetConfiguration SpectrumWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only non-default states are persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the defaults.
    if (m_displayStyle != Style::Solid)
        config["displayStyle"] = static_cast<int>(m_displayStyle);
    if (m_orientation != Orientation::Up)
        config["orientation"] = static_cast<int>(m_orientation);
    if (m_channel != Channel::Mixed)
        config["channel"] = static_cast<int>(m_channel);
    if (m_bands != kDefaultBands)
        config["bands"] = m_bands;
    return config;
}

void SpectrumWidget::restoreWidget(WidgetConfiguration& config)
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
    if (config.contains("channel")) {
        const int value = config.value("channel").toInt();
        if (value >= 0 && value <= static_cast<int>(Channel::Right))
            setChannel(static_cast<Channel>(value));
    }
    if (config.contains("bands")) {
        const int value = config.value("bands").toInt();
        if (std::find(kBandOptions.begin(), kBandOptions.end(), value)
            != kBandOptions.end())
            setBandCount(value);
    }
}

void SpectrumWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    // The styling surfaces (a stylesheet's "background" recolors the
    // background, the track follows; "color" the bars); the display
    // style (an edit-mode option) picks the rendering, the "x-"
    // properties tune it.
    const BarSurface surface = barSurface(p);
    for (int i = 0; i < m_bands; ++i) {
        const QRect slot =
            barpaint::barSlot(i, m_bands, size(), m_orientation, surface.margin);
        if (!slot.isValid())
            continue;
        const float level = m_levels.value(i, 0.0F);
        if (m_displayStyle == Style::Segments)
            barpaint::paintSegments(p, slot, level, surface.segments,
                                    m_orientation, surface.track, surface.bar);
        else
            barpaint::paintSolid(p, slot, level, m_orientation, surface.track,
                                 surface.bar);
    }
}
