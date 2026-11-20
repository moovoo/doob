#include "oscillogramwidget.h"

#include "paintrate.h"
#include "settingsmanager.h"
#include "vizwidget.h"

#include <QMenu>
#include <QPainter>
#include <QPolygon>

#include <cmath>

namespace {

QString styleName(OscillogramWidget::TraceStyle style)
{
    if (style == OscillogramWidget::TraceStyle::Line)
        return QStringLiteral("Line");
    if (style == OscillogramWidget::TraceStyle::Boxes)
        return QStringLiteral("Boxes");
    return QStringLiteral("Bars");
}

// The edit menu's label for a window length ("0.25 s", "0.5 s",
// "1 s", ...).
QString windowName(int windowMs)
{
    return QStringLiteral("%1 s").arg(static_cast<double>(windowMs) / 1000.0);
}

} // namespace

OscillogramWidget::OscillogramWidget(QWidget *parent)
    : BaseWidget(parent)
{
    setMinimumSize(kMinimumSize);

    m_pm = PlaybackManager::getInstance();
    m_ring.resize(2 * static_cast<qsizetype>(m_ringSize));

    // The paint's rhythm (a user setting): while playing, the timer
    // samples the trace at the configured rate — the taps only feed it
    // (see the class comment) — so the cadence is steady and the
    // user's choice.
    PaintRate::ensureRegistered();
    PaintRate::applyTo(&m_paintTimer);
    connect(&m_paintTimer, &QTimer::timeout, this, [this] {
        update();
    });
    connect(SettingsManager::getInstance(), &SettingsManager::changed,
            this, &OscillogramWidget::onSettingChanged);

    connect(m_pm, &PlaybackManager::audioWaveform, this, &OscillogramWidget::onWaveform);
    connect(m_pm, &PlaybackManager::stateChanged, this, &OscillogramWidget::onStateChanged);

    // A widget inserted mid-playback starts with a running paint timer
    // and an empty trace; the next taps fill it in.
    onStateChanged(m_pm->state());
}

void OscillogramWidget::onWaveform(const QVector<float> &waveform, quint32 sampleRate)
{
    // The tap is latency-shifted (the frames only reach the speaker
    // after the stream's latency), so a queued tap can land after a
    // pause or stop: the trace only follows live playback. The tap does
    // not paint itself: the paint timer samples the trace at the
    // user's rate (see the class comment).
    if (m_pm->state() != PlaybackManager::Playing)
        return;
    const qsizetype cols = waveform.size() / 2;
    if (cols <= 0 || sampleRate == 0)
        return;
    m_rate = sampleRate;
    // Re-bucket the tap's columns (8 frames each) into display columns
    // (each spanning the window's audio span over the capacity, see
    // the header): the scroll's speed is then capacity columns per
    // window, whatever the sample rate.
    // K = (frames per second over 1000) * (ms per window) / (frames per
    // display column): 48 kHz with the default 1 s window over 256
    // columns of 8 frames is 23 tap columns per display column.
    m_tapsPerColumn = qMax(
        1, qRound(static_cast<double>(m_rate) / 1000.0 * m_windowMs
                  / (static_cast<double>(m_capacity) * 8.0)));
    for (qsizetype i = 0; i < cols; ++i) {
        const float mn = waveform[2 * i];
        const float mx = waveform[(2 * i) + 1];
        m_accMn = qMin(m_accMn, mn);
        m_accMx = qMax(m_accMx, mx);
        if (++m_accCols < m_tapsPerColumn)
            continue;
        m_accCols = 0;
        const qsizetype slot = m_colCount % m_ringSize;
        m_ring[2 * slot] = m_accMn;
        m_ring[(2 * slot) + 1] = m_accMx;
        ++m_colCount;
        m_accMn = 1.0F;   // reset for the next column (the min/max of
        m_accMx = -1.0F;  // the empty accumulator is the identity)
    }
}

void OscillogramWidget::onStateChanged(PlaybackManager::State state)
{
    if (viz::runningFor(state, &m_paintTimer))
        return;
    clearTrace();
}

void OscillogramWidget::onSettingChanged(const QString &key, const QVariant &value)
{
    if (key != PaintRate::key())
        return;
    m_paintTimer.setInterval(PaintRate::intervalMs(value.toInt()));
}

void OscillogramWidget::clearTrace()
{
    if (m_colCount == 0 && m_accCols == 0)
        return;
    m_colCount = 0;
    m_accCols = 0;
    m_accMn = 1.0F;
    m_accMx = -1.0F;
    update();
}

double OscillogramWidget::windowEnd() const
{
    if (m_colCount == 0)
        return 0.0;
    // The right edge: the newest closed column — its right edge is the
    // widget's edge itself (see the class comment).
    return static_cast<double>(m_colCount);
}

QVector<float> OscillogramWidget::trace() const
{
    // The visible window's closed display columns (oldest first) — the
    // test seam (the flat tail's columns are a paint artifact and stay
    // out).
    QVector<float> out;
    if (m_colCount == 0)
        return out;
    const double end = windowEnd();
    const qsizetype g0 = static_cast<qsizetype>(
        std::ceil(qMax(0.0, end - m_capacity)));
    const qsizetype g1 = m_colCount - 1;
    if (g1 < g0)
        return out;
    out.reserve(2 * (g1 - g0 + 1));
    for (qsizetype g = g0; g <= g1; ++g) {
        const qsizetype slot = g % m_ringSize;
        out << m_ring[2 * slot] << m_ring[(2 * slot) + 1];
    }
    return out;
}

void OscillogramWidget::setTraceStyle(TraceStyle style)
{
    if (style == m_traceStyle)
        return;
    m_traceStyle = style;
    update();
}

void OscillogramWidget::setWindowMs(int ms)
{
    if (ms == m_windowMs)
        return;
    m_windowMs = ms;
    // The next taps close display columns at the new width; the old
    // ones scroll off. A repaint keeps the (now re-mapped) window
    // current at once.
    update();
}

QList<StyleProperty> OscillogramWidget::styleProperties()
{
    return {
        {
            QStringLiteral("x-line-width"),
            title,
            QStringLiteral("The pen's width of the Line trace style, "
                           "in pixels."),
            QStringLiteral("2"),
            QStringLiteral("px"),
            1,
            16,
        },
        {
            QStringLiteral("x-box-size"),
            title,
            QStringLiteral("The size of the Boxes trace style' LCD "
                           "boxes, in pixels or as a percent of the "
                           "widget's height."),
            QStringLiteral("3"),
            QStringLiteral("px or %"),
            1,
            0,
        },
        {
            QStringLiteral("x-box-color-off"),
            title,
            QStringLiteral("The color the Boxes trace style' unlit "
                           "grid glows through. The default is the "
                           "widget's background lightened by 20%."),
            QStringLiteral("auto"),
            QStringLiteral("color"),
            0,
            0,
        },
    };
}

QMenu* OscillogramWidget::widgetContextMenu()
{
    // Start from the base menu (the toolbar entries, when a toolbar
    // exists) and add this widget's own actions.
    QMenu* menu = viz::editMenu(BaseWidget::widgetContextMenu(), this, title);

    // The trace style: an exclusive checkable group, rebuilt from the
    // current state on every menu. A separator keeps it apart from the
    // base's entries above (a horizontal line between the groups, like
    // the other option menus).
    menu->addSeparator();
    viz::optionGroup<TraceStyle>(
        this, menu, {TraceStyle::Bars, TraceStyle::Line, TraceStyle::Boxes},
        styleName, m_traceStyle,
        [this](TraceStyle style) { setTraceStyle(style); });

    // The window's length (how much audio the window holds): the
    // scroll's speed is the capacity over the span. A separator keeps
    // it apart from the trace style group above.
    menu->addSeparator();
    viz::optionGroup<int>(this, menu, kWindowOptions, windowName, m_windowMs,
                          [this](int ms) { setWindowMs(ms); });

    return menu;
}

WidgetConfiguration OscillogramWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only a non-default trace style is persisted, so a fresh
    // a layout document (or a copy of the widget) cannot pin the default.
    if (m_traceStyle != TraceStyle::Bars)
        config["traceStyle"] = static_cast<int>(m_traceStyle);
    if (m_windowMs != kDefaultWindowMs)
        config["windowMs"] = m_windowMs;
    return config;
}

void OscillogramWidget::restoreWidget(WidgetConfiguration& config)
{
    BaseWidget::restoreWidget(config);
    // A stale value outside the enum range is ignored (the menu only
    // ever passes valid ones).
    if (config.contains("traceStyle")) {
        const int value = config.value("traceStyle").toInt();
        if (value >= 0 && value <= static_cast<int>(TraceStyle::Boxes))
            setTraceStyle(static_cast<TraceStyle>(value));
    }
    // Only the menu's own lengths are valid; a stale value is ignored.
    if (config.contains("windowMs")) {
        const int value = config.value("windowMs").toInt();
        for (const int option : kWindowOptions)
            if (value == option) {
                setWindowMs(value);
                break;
            }
    }
}

void OscillogramWidget::columnPair(qsizetype g, float &mn, float &mx) const
{
    const qsizetype slot = g % m_ringSize;
    mn = m_ring[2 * slot];
    mx = m_ring[(2 * slot) + 1];
}

void OscillogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    // The styling surfaces: a stylesheet's "background" recolors the
    // background and "color" the trace (the theme's roles stand in
    // without a rule: Base, Highlight — the bar widgets' surface); the
    // trace style (an edit-mode option) picks the rendering and the
    // "x-" properties tune it.
    const QColor bg = palette().color(QPalette::Base);
    const QColor grid = styleColor(QPalette::WindowText, QPalette::Mid);
    const QColor traceColor = styleColor(QPalette::WindowText, QPalette::Highlight);
    p.fillRect(rect(), bg);

    const int w = width();
    const int h = height();
    const int mid = h / 2;
    // The center line (zero amplitude).
    p.setPen(grid);
    // p.drawLine(0, mid, w, mid);

    if (m_colCount == 0)
        return;
    // The window's right edge (see windowEnd): the newest closed
    // column — g1 is the last column whose left edge lies before the
    // edge, so the last drawn column's right edge is the widget's
    // edge itself.
    const double end = windowEnd();
    const double start = end - m_capacity;
    const qsizetype g0 = static_cast<qsizetype>(
        std::ceil(qMax(0.0, start)));
    const qsizetype g1 = static_cast<qsizetype>(std::ceil(end)) - 1;
    if (g1 < g0)
        return;

    // The window fills the widget (capacity columns across the width);
    // the fractional start is the sub-column phase the scroll glides
    // with. Amplitude 1.0 spans the full height.
    const double colWidth =
        static_cast<double>(w) / static_cast<double>(m_capacity);
    const float half = static_cast<float>(mid);

    if (m_traceStyle == TraceStyle::Line)
        paintLine(p, traceColor, colWidth, start, g0, g1, mid, half);
    else if (m_traceStyle == TraceStyle::Boxes)
        paintBoxes(p, traceColor, bg, colWidth, start, g0, g1, mid, half);
    else
        paintBars(p, traceColor, colWidth, start, g0, g1, mid, half);
}

void OscillogramWidget::paintBars(QPainter &p, const QColor &color, double colWidth,
                                  double start, qsizetype g0, qsizetype g1, int mid,
                                  float half) const
{
    p.setPen(color);
    for (qsizetype g = g0; g <= g1; ++g) {
        float mn = 0.0F;
        float mx = 0.0F;
        columnPair(g, mn, mx);
        const int x = qRound((static_cast<double>(g) - start) * colWidth);
        const int yTop = mid - qRound(mx * half);
        const int yBottom = mid - qRound(mn * half);
        p.drawLine(x, yTop, x, yBottom);
    }
}

void OscillogramWidget::paintLine(QPainter &p, const QColor &color, double colWidth,
                                  double start, qsizetype g0, qsizetype g1, int mid,
                                  float half) const
{
    // A single polyline that oscillates through the columns' envelopes:
    // each column contributes its max (top) and min (bottom), the pen
    // swinging between them column by column — a triangle wave that
    // keeps both edges the two-envelope view would show. Round-capped
    // and round-joined; "x-line-width" sets the pen.
    //
    // Drawn as its segments, not with drawPolyline: the raster
    // engine's stroked outline of a dense self-intersecting polyline
    // is pathologically slow — a width-2 pen over the full window's
    // ~500 segments costs ~100 ms drawn as one path, ~3 ms as
    // individual lines (the bars' and boxes' range).
    const int penWidth = styleInt("x-line-width", 2, 1, 16);
    QPen pen(color);
    pen.setWidth(penWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    QPolygon trace;
    const qsizetype columns = g1 - g0 + 1;
    trace.reserve(2 * columns);
    for (qsizetype g = g0; g <= g1; ++g) {
        float mn = 0.0F;
        float mx = 0.0F;
        columnPair(g, mn, mx);
        // The column's peak and trough, each a quarter of a column off
        // its center — the pen's swing spans the column's width. The
        // points are integer, like the bars' and boxes' columns: the
        // scroll steps a pixel per column, the same as the other styles.
        const double x = (static_cast<double>(g) - start + 0.5) * colWidth;
        trace << QPoint(qRound(x - (colWidth * 0.25)),
                        mid - qRound(mx * half))
              << QPoint(qRound(x + (colWidth * 0.25)),
                        mid - qRound(mn * half));
    }
    for (int i = 1; i < trace.size(); ++i)
        p.drawLine(trace[i - 1], trace[i]);
}

void OscillogramWidget::paintBoxes(QPainter &p, const QColor &color, const QColor &bg,
                                   double colWidth, double start, qsizetype g0,
                                   qsizetype g1, int mid, float half) const
{
    // LCD style: the column's envelope is quantized to a grid of
    // discrete boxes ("x-box-size", px or % of the height); the unlit
    // grid of every data column glows through in "x-box-color-off"
    // (the classic ghost segments, the widget's background lightened
    // by 20% by default — the bar widgets' auto track, so the same
    // palette approach as the other trace styles: a subtle tint on a
    // light as on a dark background).
    const int boxSize = styleLength("x-box-size", 3, height());
    const QColor off = styleColorValue("x-box-color-off", bg.lighter(120));

    for (qsizetype g = g0; g <= g1; ++g) {
        float mn = 0.0F;
        float mx = 0.0F;
        columnPair(g, mn, mx);
        const int x = qRound((static_cast<double>(g) - start) * colWidth);
        const int next = qRound((static_cast<double>(g + 1) - start) * colWidth);
        const int bw = qMax(1, next - x);
        // The unlit grid of the column (the ghost).
        p.fillRect(x, 0, bw, height(), off);
        // The lit boxes: the envelope snapped to the grid.
        const int yTop = mid - qRound(mx * half);
        const int yBottom = mid - qRound(mn * half);
        const int topRow = (yTop / boxSize) * boxSize;
        const int botRow = (yBottom / boxSize) * boxSize;
        for (int y = topRow; y <= botRow; y += boxSize)
            p.fillRect(x, y, bw, boxSize, color);
    }
}
