#include "currenttrackwidget.h"

#include "checkablelistdialog.h"
#include "mediaitemstore.h"
#include "medialistmodel.h"

#include <QAction>
#include <QColor>
#include <QDesktopServices>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QUrl>

namespace {

// The art block's size (the cover is scaled to fit, aspect preserved).
const int kArtSize = 160;

// The block the "always" art renders when the playing item has no
// cover: a solid gray square of the art size.
QPixmap artPlaceholder()
{
    static const QPixmap placeholder = [] {
        QPixmap pm(kArtSize, kArtSize);
        pm.fill(QColor(90, 90, 90));
        return pm;
    }();
    return placeholder;
}

// Only these schemes are rendered as links; anything else (for example a
// title that merely contains a colon) stays plain text.
bool isLinkable(const QString &value)
{
    const QUrl url(value);
    return url.isValid()
        && (url.scheme() == QLatin1String("file")
            || url.scheme() == QLatin1String("http")
            || url.scheme() == QLatin1String("https"));
}

// One "Name: value" line: the name is bold, the value is regular text, and
// a non-empty href turns the value into a link.
QString line(const QString &name, const QString &value, const QString &href)
{
    const QString shown = href.isEmpty()
        ? value.toHtmlEscaped()
        : QString("<a href=\"%1\">%2</a>").arg(href.toHtmlEscaped(), value.toHtmlEscaped());
    return QString("<b>%1</b>: %2").arg(name.toHtmlEscaped(), shown);
}

// The plain text fields: the value as-is (an empty value is the "no
// value" state — the "always" lines render their name alone for it).
QPair<QString, QString> textLineValue(const QString &key, const MediaItem &item)
{
    if (key == QStringLiteral("title"))
        return { item.title, QString() };
    if (key == QStringLiteral("artist"))
        return { item.artist, QString() };
    if (key == QStringLiteral("albumartist"))
        return { item.albumArtist, QString() };
    if (key == QStringLiteral("album"))
        return { item.album, QString() };
    if (key == QStringLiteral("composer"))
        return { item.composer, QString() };
    if (key == QStringLiteral("genre"))
        return { item.genre, QString() };
    if (key == QStringLiteral("comment"))
        return { item.comment, QString() };
    return { QString(), QString() };
}

// "n" or "n/total" (the zero count is the "no value" state).
QString numberPair(int number, int total)
{
    if (number <= 0)
        return {};
    return total > 0 ? QString("%1/%2").arg(number).arg(total)
                     : QString::number(number);
}

// The value fields: the text derived from a number (zero is the "no
// value" state). The File line displays the local path but links the
// URL.
QPair<QString, QString> valueLineValue(const QString &key, const MediaItem &item)
{
    if (key == QStringLiteral("year"))
        return { item.year > 0 ? QString::number(item.year) : QString(), QString() };
    if (key == QStringLiteral("track"))
        return { numberPair(item.trackNumber, item.trackTotal), QString() };
    if (key == QStringLiteral("disc"))
        return { numberPair(item.discNumber, item.discTotal), QString() };
    if (key == QStringLiteral("duration"))
        return { item.durationMs > 0
                ? MediaListModel::formatDuration(item.durationMs)
                : QString(), QString() };
    if (key == QStringLiteral("file")) {
        if (!item.source.isValid())
            return { QString(), QString() };
        return { item.source.isLocalFile() ? item.source.toLocalFile()
                                           : item.source.toString(),
                 item.source.toString() };
    }
    if (key == QStringLiteral("bitrate"))
        return { item.bitrate > 0 ? QString("%1 kbps").arg(item.bitrate)
                                  : QString(), QString() };
    if (key == QStringLiteral("samplerate"))
        return { item.sampleRate > 0 ? QString("%1 Hz").arg(item.sampleRate)
                                     : QString(), QString() };
    if (key == QStringLiteral("channels"))
        return { item.channels > 0 ? QString::number(item.channels)
                                   : QString(), QString() };
    return { QString(), QString() };
}

// The (value, href) of a fixed line: the value fields derive their text
// from a number, the rest is the plain text as-is.
QPair<QString, QString> lineValue(const QString &key, const MediaItem &item)
{
    static const QSet<QString> valueKeys = {
        QStringLiteral("year"),
        QStringLiteral("track"),
        QStringLiteral("disc"),
        QStringLiteral("duration"),
        QStringLiteral("file"),
        QStringLiteral("bitrate"),
        QStringLiteral("samplerate"),
        QStringLiteral("channels"),
    };
    if (valueKeys.contains(key))
        return valueLineValue(key, item);
    return textLineValue(key, item);
}

// The tri-state dialog's check states as the line visibilities (and
// back): unchecked = never, partially checked = when-non-empty,
// checked = always.
Qt::CheckState checkState(LineVisibility vis)
{
    switch (vis) {
    case LineVisibility::Never:
        return Qt::Unchecked;
    case LineVisibility::Always:
        return Qt::Checked;
    case LineVisibility::WhenNonEmpty:
        return Qt::PartiallyChecked;
    }
    return Qt::PartiallyChecked;
}

LineVisibility visibilityFromCheckState(Qt::CheckState state)
{
    switch (state) {
    case Qt::Unchecked:
        return LineVisibility::Never;
    case Qt::Checked:
        return LineVisibility::Always;
    case Qt::PartiallyChecked:
        return LineVisibility::WhenNonEmpty;
    }
    return LineVisibility::WhenNonEmpty;
}

// The active driver's observation of the decoded stream (the codec, the
// container, the actual parameters) as the "Stream" section below the
// metadata: a bold header line plus the same "Name: value" lines (an
// empty value renders no line).
void appendStreamLines(QStringList &lines, const QVariantMap &info)
{
    auto add = [&lines](const QString &name, const QString &value) {
        if (!value.trimmed().isEmpty())
            lines.append(line(name, value, QString()));
    };
    lines.append(QStringLiteral("<b>Stream</b>"));
    add("Codec", info.value("codec").toString());
    add("Format", info.value("format").toString());
    // The stream's bitrate in bits/s.
    const qint64 bitrateBps = info.value("bitrate").toLongLong();
    if (bitrateBps > 0)
        add("Bitrate", QString("%1 kbps").arg(bitrateBps / 1000));
    const int sampleRate = info.value("sampleRate").toInt();
    if (sampleRate > 0)
        add("Sample rate", QString("%1 Hz").arg(sampleRate));
    const int channels = info.value("channels").toInt();
    if (channels > 0) {
        const QString layout = info.value("channelLayout").toString();
        add("Channels", layout.isEmpty()
            ? QString::number(channels)
            : QString("%1 (%2)").arg(channels).arg(layout));
    }
}

// The non-standard tags: one line each, in sorted order (the map's),
// skipped when the group is off or the item has none (a group cannot
// render when there is nothing).
void appendExtraLines(QStringList &lines, const MediaItem &item,
                      LineVisibility group)
{
    if (group == LineVisibility::Never)
        return;
    for (auto it = item.extra.cbegin(); it != item.extra.cend(); ++it) {
        if (it.value().trimmed().isEmpty())
            continue;
        const QString link = isLinkable(it.value()) ? it.value() : QString();
        lines.append(line(it.key(), it.value(), link));
    }
}

} // namespace

CurrentTrackWidget::CurrentTrackWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_pm = PlaybackManager::getInstance();
    m_sequence = defaultSequence();

    m_art = new QLabel;
    m_art->setObjectName("currentTrackArt");
    m_art->hide();

    m_view = new QLabel;
    m_view->setObjectName("currentTrackView");
    m_view->setTextFormat(Qt::RichText);
    m_view->setWordWrap(true);
    m_view->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_view->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);

    // Open links with the OS default handler (file:// URLs open the file
    // browser). Routed through the signal (instead of
    // QLabel::setOpenExternalLinks) so the click path is observable in tests.
    connect(m_view, &QLabel::linkActivated, this, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    // The scroll area's content: the art block above the text lines (a
    // frameless scroll area so long metadata lists stay reachable without
    // the label looking like a text box).
    QWidget* content = new QWidget;
    content->setObjectName("currentTrackContent");
    QVBoxLayout* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    contentLayout->addWidget(m_art);
    contentLayout->addWidget(m_view);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setObjectName("currentTrackScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);

    layout()->addWidget(scroll);

    connect(m_pm, &PlaybackManager::playingItemChanged, this, &CurrentTrackWidget::onPlayingItemChanged);
    connect(m_pm, &PlaybackManager::streamInfoChanged, this, &CurrentTrackWidget::onStreamInfoChanged);
    connect(MediaItemStore::getInstance(), &MediaItemStore::itemArtworkChanged,
            this, &CurrentTrackWidget::onItemArtworkChanged);

    // A track that started playing before this widget existed will not emit
    // again, so sync with the current item on construction.
    onPlayingItemChanged(m_pm->playingItem());
}

QList<QPair<QString, QString>> CurrentTrackWidget::lineCatalog()
{
    return {
        { QStringLiteral("Art"), artKey },
        { QStringLiteral("Title"), QStringLiteral("title") },
        { QStringLiteral("Artist"), QStringLiteral("artist") },
        { QStringLiteral("Album Artist"), QStringLiteral("albumartist") },
        { QStringLiteral("Album"), QStringLiteral("album") },
        { QStringLiteral("Composer"), QStringLiteral("composer") },
        { QStringLiteral("Genre"), QStringLiteral("genre") },
        { QStringLiteral("Year"), QStringLiteral("year") },
        { QStringLiteral("Track"), QStringLiteral("track") },
        { QStringLiteral("Disc"), QStringLiteral("disc") },
        { QStringLiteral("Duration"), QStringLiteral("duration") },
        { QStringLiteral("Comment"), QStringLiteral("comment") },
        { QStringLiteral("File"), QStringLiteral("file") },
        { QStringLiteral("Bitrate"), QStringLiteral("bitrate") },
        { QStringLiteral("Sample rate"), QStringLiteral("samplerate") },
        { QStringLiteral("Channels"), QStringLiteral("channels") },
        { QStringLiteral("Non-standard tags"), extraKey },
    };
}

QStringList CurrentTrackWidget::defaultSequence()
{
    QStringList order;
    for (const QPair<QString, QString> &entry : lineCatalog())
        order.append(entry.second);
    return order;
}

const QHash<QString, QString> &CurrentTrackWidget::labelMap()
{
    static const QHash<QString, QString> labels = [] {
        QHash<QString, QString> map;
        for (const QPair<QString, QString> &entry : lineCatalog())
            map.insert(entry.second, entry.first);
        return map;
    }();
    return labels;
}

const QSet<QString> &CurrentTrackWidget::knownKeys()
{
    static const QSet<QString> keys = [] {
        QSet<QString> set;
        for (const QPair<QString, QString> &entry : lineCatalog())
            set.insert(entry.second);
        return set;
    }();
    return keys;
}

LineVisibility CurrentTrackWidget::visibility(const QString &key) const
{
    return m_visibilities.value(key, LineVisibility::WhenNonEmpty);
}

void CurrentTrackWidget::onPlayingItemChanged(const MediaItem &item)
{
    m_view->setText(item.isValid() ? renderHtml(item, m_streamInfo) : QString());
    updateArt();
}

void CurrentTrackWidget::onItemArtworkChanged(const MediaItem &item)
{
    if (item.id == m_pm->playingItem().id)
        updateArt();
}

void CurrentTrackWidget::updateArt()
{
    const MediaItem item = m_pm->playingItem();
    QPixmap pixmap;
    if (item.isValid()
        && MediaItemStore::getInstance()->hasArtwork(item.id)) {
        QString mime;
        QByteArray data;
        if (MediaItemStore::getInstance()->artwork(item.id, &mime, &data)) {
            // fromData() detects the format from the bytes (the mime is
            // the store's to record, not to parse).
            const QImage image = QImage::fromData(data);
            if (!image.isNull())
                pixmap = QPixmap::fromImage(
                    image.scaled(kArtSize, kArtSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation));
        }
    }

    const LineVisibility vis = visibility(artKey);
    // A stopped player (or a "never" art) keeps the text-only look.
    if (!item.isValid() || vis == LineVisibility::Never) {
        m_art->setPixmap(QPixmap());
        m_art->setVisible(false);
        return;
    }
    if (pixmap.isNull()) {
        // The "always" art renders a placeholder block in place of the
        // missing cover; the default shows the text-only look.
        if (vis != LineVisibility::Always) {
            m_art->setPixmap(QPixmap());
            m_art->setVisible(false);
            return;
        }
        pixmap = artPlaceholder();
    }
    m_art->setPixmap(pixmap);
    m_art->setVisible(true);
}

void CurrentTrackWidget::onStreamInfoChanged(const QVariantMap &info)
{
    m_streamInfo = info;
    onPlayingItemChanged(m_pm->playingItem());
}

// Renders the item's lines in the sequence's order. The "always" lines
// render their name alone for an empty value, the "never" lines render
// nothing, and the default lines render only when the value is
// non-empty. The Stream section stays the fixed final section (the
// driver's observation, when non-empty).
QString CurrentTrackWidget::renderHtml(const MediaItem &item,
                                       const QVariantMap &streamInfo) const
{
    QStringList lines;

    for (const QString &key : m_sequence) {
        if (key == artKey)
            continue; // the art block is above the text (updateArt)
        if (key == extraKey) {
            appendExtraLines(lines, item, visibility(extraKey));
            continue;
        }
        const LineVisibility vis = visibility(key);
        if (vis == LineVisibility::Never)
            continue;
        const QPair<QString, QString> value = lineValue(key, item);
        const bool empty = value.first.trimmed().isEmpty();
        if (vis == LineVisibility::WhenNonEmpty && empty)
            continue;
        const QString name = labelMap().value(key, key);
        if (empty) {
            lines.append(QString("<b>%1</b>").arg(name.toHtmlEscaped()));
            continue;
        }
        // href defaults to the value itself when the value carries a
        // linkable URL; the File line passes the URL explicitly because
        // it displays the local path instead.
        const QString link = value.second.isEmpty() && isLinkable(value.first)
            ? value.first : value.second;
        lines.append(line(name, value.first, link));
    }

    // The driver's observation of the decoded stream: its own section
    // below the metadata (the tag-derived lines above stand on their
    // own — the two sources are complementary, not the same field).
    if (!streamInfo.isEmpty())
        appendStreamLines(lines, streamInfo);

    return lines.join("<br>");
}

QMenu* CurrentTrackWidget::widgetContextMenu()
{
    // Start from the base menu (the style action, the toolbar entries
    // when a toolbar exists) and add this widget's own actions.
    QMenu* menu = BaseWidget::widgetContextMenu();
    if (menu == nullptr)
        menu = new QMenu(title, this);

    QAction* lines = menu->addAction("Lines...");
    connect(lines, &QAction::triggered, this, &CurrentTrackWidget::onEditLines);

    return menu;
}

void CurrentTrackWidget::onEditLines()
{
    // The tri-state dialog: the check state is the visibility (unchecked
    // = never, partially checked = when-non-empty, checked = always) and
    // the list order is the display sequence.
    QHash<QString, Qt::CheckState> states;
    for (const QPair<QString, QString> &entry : lineCatalog())
        states.insert(entry.second, checkState(visibility(entry.second)));

    CheckableListDialog dialog(
        QStringLiteral("Current track lines"),
        QStringLiteral("The list order is the display order. A line's state "
                       "is its visibility: unchecked = never, partially "
                       "checked = only when non-empty, checked = always "
                       "(an empty value renders the name alone, the art a "
                       "placeholder block). Non-standard tags are a group, "
                       "shown when the item has any."),
        lineCatalog(), m_sequence, states, this,
        QStringLiteral("dialogs.current_track_lines"));
    if (dialog.exec() != QDialog::Accepted)
        return;

    QHash<QString, LineVisibility> visibilities;
    const QHash<QString, Qt::CheckState> newStates = dialog.states();
    for (auto it = newStates.cbegin(); it != newStates.cend(); ++it)
        visibilities.insert(it.key(), visibilityFromCheckState(it.value()));

    applySequence(dialog.sequence());
    applyVisibilities(visibilities);
    onPlayingItemChanged(m_pm->playingItem());
}

void CurrentTrackWidget::applySequence(const QStringList &order)
{
    // The known keys in the given order: a stale (unknown) key is
    // dropped, a duplicate skipped, the missing keys appended in catalog
    // order (the sequence always covers every line).
    QStringList sequence;
    QSet<QString> seen;
    for (const QString &key : order) {
        if (!knownKeys().contains(key) || seen.contains(key))
            continue;
        seen.insert(key);
        sequence.append(key);
    }
    for (const QPair<QString, QString> &entry : lineCatalog())
        if (!seen.contains(entry.second))
            sequence.append(entry.second);
    m_sequence = sequence;
}

void CurrentTrackWidget::applyVisibilities(
    const QHash<QString, LineVisibility> &visibilities)
{
    for (auto it = visibilities.cbegin(); it != visibilities.cend(); ++it) {
        if (!knownKeys().contains(it.key()))
            continue; // a stale key the catalog no longer knows
        LineVisibility vis = it.value();
        if (it.key() == extraKey && vis == LineVisibility::Always)
            vis = LineVisibility::WhenNonEmpty; // the group is binary
        if (vis == LineVisibility::WhenNonEmpty) {
            m_visibilities.remove(it.key()); // the default is not stored
            continue;
        }
        m_visibilities.insert(it.key(), vis);
    }
}

WidgetConfiguration CurrentTrackWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only non-default states are persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the defaults: the default
    // sequence is the catalog order, the default visibility is
    // when-non-empty (and m_visibilities holds only the non-defaults).
    if (m_sequence != defaultSequence())
        config["sequence"] = m_sequence;
    if (!m_visibilities.isEmpty()) {
        // A map (key -> the visibility as an int): it round-trips the
        // layout document's JSON as an object of numbers.
        QVariantMap vis;
        for (auto it = m_visibilities.cbegin(); it != m_visibilities.cend();
             ++it)
            vis.insert(it.key(), static_cast<int>(it.value()));
        config["lineVisibility"] = vis;
    }
    return config;
}

void CurrentTrackWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    if (config.contains("sequence")) {
        QStringList keys;
        const QList<QVariant> list = config.value("sequence").toList();
        for (const QVariant &value : list)
            keys.append(value.toString());
        applySequence(keys);
    }
    if (config.contains("lineVisibility")) {
        const QVariantMap vis = config.value("lineVisibility").toMap();
        QHash<QString, LineVisibility> visibilities;
        for (auto it = vis.cbegin(); it != vis.cend(); ++it) {
            const int value = it.value().toInt();
            if (value < static_cast<int>(LineVisibility::Never)
                    || value > static_cast<int>(LineVisibility::Always))
                continue; // a stale value outside the enum range
            visibilities.insert(it.key(), static_cast<LineVisibility>(value));
        }
        applyVisibilities(visibilities);
    }
    onPlayingItemChanged(m_pm->playingItem());
}
