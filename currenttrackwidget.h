#ifndef CURRENTTRACKWIDGET_H
#define CURRENTTRACKWIDGET_H

#include <QHash>
#include <QPair>
#include <QSet>
#include <QVariantMap>

#include "basewidget.h"
#include "playbackmanager.h"

class QLabel;
class QMenu;

// The visibility of a current-track line: never (no line),
// when-non-empty (the default — a line only when the value is
// non-empty) and always (a line always; an empty value renders the
// name alone, the art the placeholder block). The `extra` group is
// binary (it cannot render when there is nothing).
enum class LineVisibility
{
    Never,
    WhenNonEmpty,
    Always,
};

class CurrentTrackWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "currenttrack";
    inline static const QString title = "Current track";
    inline static const QString tag = widgetTags::playback;

    // The catalog's pseudo-lines: the art block and the group of
    // non-standard tags (the fixed lines carry their display name,
    // lowercased, as the key).
    inline static const QString artKey = QStringLiteral("art");
    inline static const QString extraKey = QStringLiteral("extra");

    explicit CurrentTrackWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    // The (display label, key) catalog of every line, in the default
    // display order: the art block first, the fixed lines, the extra
    // group last. The Stream section is not a line (it stays the fixed
    // final section, rendered when the driver reports stream info).
    static QList<QPair<QString, QString>> lineCatalog();

    // The display order of the lines' keys (the default is the catalog
    // order).
    QStringList sequence() const { return m_sequence; }
    // The visibility of a line key (the default is when-non-empty).
    LineVisibility visibility(const QString &key) const;

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration &config) override;

private slots:
    void onPlayingItemChanged(const MediaItem &item);
    // The active driver's stream info (the codec, container and the
    // decoded stream's actual parameters); an empty map clears it.
    void onStreamInfoChanged(const QVariantMap &info);
    // The store's art changed (an import's art landed, a resync replaced
    // it, a removal): re-render the block when it is the playing item's.
    void onItemArtworkChanged(const MediaItem &item);
    void onEditLines();

private:
    static QStringList defaultSequence();
    static const QHash<QString, QString> &labelMap();
    static const QSet<QString> &knownKeys();

    QString renderHtml(const MediaItem &item,
                       const QVariantMap &streamInfo) const;
    // The scaled art block above the text lines (hidden when the playing
    // item has no art — the text-only look; the placeholder block when
    // the art line is "always").
    void updateArt();
    void applySequence(const QStringList &order);
    void applyVisibilities(const QHash<QString, LineVisibility> &visibilities);

    PlaybackManager* m_pm;
    QLabel* m_view;
    QLabel* m_art;
    // The last stream info the active driver reported (empty = none).
    QVariantMap m_streamInfo;
    // The display order of the lines' keys (the default is the catalog
    // order).
    QStringList m_sequence;
    // The lines' visibilities (only the non-defaults; the absent keys
    // are when-non-empty).
    QHash<QString, LineVisibility> m_visibilities;
};

#endif // CURRENTTRACKWIDGET_H
