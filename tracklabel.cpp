#include "tracklabel.h"

#include "medialistmodel.h"

#include <QAction>
#include <QFontMetrics>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QTimer>

namespace {

// The separator between the repetitions of the text in the marquee's
// endless stream.
const QString kMarqueeSeparator = QStringLiteral(" • ");

// "n" or "n/total" (empty when the number is unset).
QString numberPair(int number, int total)
{
    if (number <= 0)
        return {};
    if (total > 0)
        return QString("%1/%2").arg(number).arg(total);
    return QString::number(number);
}

// "value unit" (empty when the value is unset).
QString suffixed(int value, const char* unit)
{
    if (value <= 0)
        return {};
    return QString("%1 %2").arg(value).arg(unit);
}

// The plain string fields. A field with an empty value answers empty
// like a missing one would (the caller falls through to the derived
// fields and comes back empty there as well).
QString simpleField(const MediaItem &item, const QString &k)
{
    if (k == "title")
        return item.title;
    if (k == "artist")
        return item.artist;
    if (k == "album")
        return item.album;
    if (k == "albumartist")
        return item.albumArtist;
    if (k == "composer")
        return item.composer;
    if (k == "genre")
        return item.genre;
    if (k == "comment")
        return item.comment;
    return {};
}

// The pattern's field keys (lowercase): the item's metadata as a value.
// An extra tag is reachable by its name (case-insensitive — the keys are
// stored normalized to uppercase, the pattern is matched against them).
QString fieldFor(const MediaItem &item, const QString &key)
{
    const QString k = key.trimmed().toLower();
    QString simple = simpleField(item, k);
    if (!simple.isEmpty())
        return simple;
    if (k == "year")
        return item.year > 0 ? QString::number(item.year) : QString();
    if (k == "track")
        return numberPair(item.trackNumber, item.trackTotal);
    if (k == "disc")
        return numberPair(item.discNumber, item.discTotal);
    if (k == "duration")
        return item.durationMs > 0 ? MediaListModel::formatDuration(item.durationMs) : QString();
    if (k == "bitrate")
        return suffixed(item.bitrate, "kbps");
    if (k == "samplerate")
        return suffixed(item.sampleRate, "Hz");
    if (k == "channels")
        return item.channels > 0 ? QString::number(item.channels) : QString();
    if (k == "file") {
        if (!item.source.isValid())
            return {};
        return item.source.isLocalFile() ? item.source.toLocalFile() : item.source.toString();
    }
    // Any extra tag by name (its stored form is uppercase).
    for (auto it = item.extra.cbegin(); it != item.extra.cend(); ++it)
        if (it.key().compare(key.trimmed(), Qt::CaseInsensitive) == 0)
            return it.value();
    return {};
}

} // namespace

QString TrackLabelWidget::defaultPattern()
{
    return QStringLiteral("{title} - {artist} - {year}");
}

TrackLabelWidget::TrackLabelWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_pm = PlaybackManager::getInstance();
    m_pattern = defaultPattern();

    // A visible minimum (never 0, so a tight layout cannot collapse the
    // label into nothing): the border plus one character wide, one line
    // of text high.
    const QFontMetrics fm = fontMetrics();
    setMinimumSize(fm.horizontalAdvance(QLatin1Char('x')) + (2 * kFrame),
                   fm.height() + (2 * kFrame));

    m_timer = new QTimer(this);
    m_timer->setInterval(scrollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &TrackLabelWidget::onTick);

    connect(m_pm, &PlaybackManager::playingItemChanged,
            this, &TrackLabelWidget::onPlayingItemChanged);

    // A track that started playing before this widget existed will not
    // emit again, so render the current item on construction.
    updateText();
}

QString TrackLabelWidget::renderPattern(const QString &pattern, const MediaItem &item)
{
    // No track: nothing to render (not the pattern's bare separators).
    if (!item.isValid())
        return {};

    // Plain substitution: a "{key}" placeholder becomes the field's
    // value (an empty string for a missing/unknown field), everything
    // else is literal text (an unterminated placeholder included).
    QString out;
    qsizetype start = 0;
    while (start < pattern.size()) {
        const qsizetype open = pattern.indexOf(QLatin1Char('{'), start);
        if (open < 0) {
            out += pattern.mid(start);
            break;
        }
        const qsizetype close = pattern.indexOf(QLatin1Char('}'), open + 1);
        if (close < 0) {
            out += pattern.mid(start);
            break;
        }
        out += pattern.mid(start, open - start);
        out += fieldFor(item, pattern.mid(open + 1, close - open - 1));
        start = close + 1;
    }
    return out;
}

void TrackLabelWidget::setPattern(const QString &pattern)
{
    if (pattern == m_pattern)
        return;
    m_pattern = pattern;
    updateText();
}

int TrackLabelWidget::contentWidth() const
{
    return width() - (2 * kFrame);
}

bool TrackLabelWidget::scrolling() const
{
    return m_timer->isActive();
}

void TrackLabelWidget::updateText()
{
    m_text = renderPattern(m_pattern, m_pm->playingItem());
    const QFontMetrics fm = fontMetrics();
    m_textWidth = fm.horizontalAdvance(m_text);
    // The endless stream's cycle: one text plus the separator (the
    // copies tile on it, the offset wraps at it).
    m_cycle = m_textWidth + fm.horizontalAdvance(kMarqueeSeparator);
    // The full text in a tooltip: the marquee keeps it in motion, the
    // hover keeps it still.
    setToolTip(m_text);
    m_offset = 0;
    updateScrolling();
    update();
}

void TrackLabelWidget::updateScrolling()
{
    // The marquee runs only while scrolling is on (the default is a
    // static label) and the text does not fit (a fit is re-checked on
    // every resize and every text change).
    if (!m_scroll || m_textWidth <= contentWidth()) {
        m_timer->stop();
        m_offset = 0;
    } else {
        m_timer->start();
    }
}

void TrackLabelWidget::onPlayingItemChanged(const MediaItem &item)
{
    Q_UNUSED(item)  // the label renders from the manager's current item
    updateText();
}

void TrackLabelWidget::onEditPattern()
{
    const QString text = patternDialog(m_pattern);
    if (!text.isEmpty())
        setPattern(text);
}

void TrackLabelWidget::onTick()
{
    // One step of the endless stream: the offset wraps at the cycle
    // (one text plus the separator), and the next repetition takes
    // over exactly where the previous one ended — no gap, no empty
    // label.
    m_offset += scrollStepPx;
    if (m_offset >= m_cycle)
        m_offset -= m_cycle;
    update();
}

void TrackLabelWidget::setScroll(bool scroll)
{
    if (scroll == m_scroll)
        return;
    m_scroll = scroll;
    updateScrolling();
    update();
}

void TrackLabelWidget::onToggleScroll()
{
    setScroll(!m_scroll);
}

QMenu* TrackLabelWidget::widgetContextMenu()
{
    // Start from the base menu (the "Style this widget..." action) and
    // add this widget's own actions.
    QMenu* menu = BaseWidget::widgetContextMenu();
    if (menu == nullptr)
        menu = new QMenu(title, this);

    QAction* pattern = menu->addAction("Pattern...");
    connect(pattern, &QAction::triggered, this, &TrackLabelWidget::onEditPattern);

    // The marquee switch (off by default: a static label). The menu is
    // rebuilt on every edit-mode open, so a plain checkable action
    // carries the current state.
    QAction* scroll = menu->addAction("Scroll");
    scroll->setCheckable(true);
    scroll->setChecked(m_scroll);
    connect(scroll, &QAction::triggered, this, &TrackLabelWidget::onToggleScroll);

    return menu;
}

QString TrackLabelWidget::patternDialog(const QString &current)
{
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Track label pattern"));
    dialog.setLabelText(
        tr("Enter the label's pattern. Placeholders: {title}, {artist}, "
           "{album}, {albumartist}, {composer}, {genre}, {year}, {track}, "
           "{disc}, {duration}, {comment}, {bitrate}, {samplerate}, "
           "{channels}, {file} and any extra tag by name. An empty or "
           "unknown field renders as an empty string; a pattern "
           "without placeholders is plain text."));
    // The prompt is long: the dialog's label is a single line by
    // default, which stretches the dialog very wide — let it wrap.
    if (QLabel* label = dialog.findChild<QLabel*>())
        label->setWordWrap(true);
    dialog.setTextEchoMode(QLineEdit::Normal);
    dialog.setTextValue(current);

    if (dialog.exec() == QDialog::Accepted)
        return dialog.textValue();
    return {};
}

WidgetConfiguration TrackLabelWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only a changed pattern is persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the default.
    if (m_pattern != defaultPattern())
        config["pattern"] = m_pattern;
    // Only the non-default (scrolling) state is persisted: a missing
    // key is the static label.
    if (m_scroll)
        config["scroll"] = m_scroll;
    return config;
}

void TrackLabelWidget::restoreWidget(WidgetConfiguration& config)
{
    BaseWidget::restoreWidget(config);
    if (config.contains("pattern"))
        setPattern(config.value("pattern").toString());
    if (config.contains("scroll"))
        setScroll(config.value("scroll").toBool());
}

void TrackLabelWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);

    // The surface (the QSS-reachable roles: a rule matching this widget
    // overrides, the theme's roles stand in unstyled).
    p.fillRect(rect(), styleColor(QPalette::Base, QPalette::Base));
    p.setPen(styleColor(QPalette::Mid, QPalette::Mid));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRect content = rect().adjusted(kFrame, kFrame, -kFrame, -kFrame);
    p.setPen(styleColor(QPalette::Text, QPalette::Text));
    if (m_textWidth <= content.width()) {
        // It fits: the text sits centered.
        p.drawText(content, Qt::AlignCenter, m_text);
    } else if (m_scroll) {
        // It does not, and the marquee is on: the endless stream
        // scrolls across, clipped to the content. The copies tile on
        // the cycle (the text plus the separator's advance), so at
        // any offset the label is full of text — never an empty gap.
        p.setClipRect(content);
        const QString stream = m_text + kMarqueeSeparator;
        for (int x = content.left() - m_offset; x < content.right(); x += m_cycle)
            p.drawText(QRect(x, content.y(), m_cycle, content.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, stream);
    } else {
        // It does not, and the label is static: the text is drawn
        // from the left edge and clipped (its beginning stays
        // visible).
        p.setClipRect(content);
        p.drawText(QRect(content.left(), content.y(), content.width(), content.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_text);
    }
}

void TrackLabelWidget::resizeEvent(QResizeEvent *event)
{
    BaseWidget::resizeEvent(event);
    updateScrolling();
    update();
}
