#ifndef TRACKLABEL_H
#define TRACKLABEL_H

#include "basewidget.h"
#include "playbackmanager.h"

class QTimer;

// A one-line, bordered label of the current track, rendered from a
// user-editable pattern ("{title} - {artist} - {year}"): by default
// the text is static (centered when it fits, drawn from the left and
// clipped when it does not); with scrolling on (an edit-mode toggle,
// off by default) it scrolls slowly to the left in an endless loop
// (the text is repeated with a separator, so the label is never
// empty). Its minimum size keeps the border and one line of text
// visible (the label never collapses to 0).
class TrackLabelWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "tracklabel";
    inline static const QString title = "Track label";
    inline static const QString tag = widgetTags::playback;

    // The pattern of a freshly created widget (only a changed pattern
    // is persisted).
    static QString defaultPattern();

    explicit TrackLabelWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    // The pattern engine: "{key}" placeholders over the item's metadata
    // (title, artist, album, albumartist, composer, genre, year, track,
    // disc, duration, comment, bitrate, samplerate, channels, file, and
    // any extra tag by name), everything else literal (an unterminated
    // placeholder included). An empty or unknown field renders as an
    // empty string; a pattern without placeholders is plain text; with
    // no track (an invalid item) it renders nothing at all.
    static QString renderPattern(const QString &pattern, const MediaItem &item);

    QString pattern() const { return m_pattern; }
    void setPattern(const QString &pattern);
    // The rendered text of the current item (the marquee's full text,
    // also the tooltip).
    QString text() const { return m_text; }

    // The marquee (off by default — a static label): the text scrolls
    // slowly to the left in an endless loop — the stream is the text
    // repeated with a separator, so the label is never empty — one
    // step per tick, only while the text does not fit the width.
    static const int scrollIntervalMs = 50;
    static const int scrollStepPx = 2;
    bool scroll() const { return m_scroll; }
    void setScroll(bool scroll);
    // True while the timer actually runs (scrolling on and the text
    // not fitting).
    bool scrolling() const;
    int scrollOffset() const { return m_offset; }
    // The stream's cycle (one text plus the separator's advance): the
    // copies tile on it and the offset wraps at it.
    int scrollCycle() const { return m_cycle; }

protected:
    // The label paints its own surface (border, background, text).
    bool selfPainted() const override { return true; }

    // The pattern editor the edit-mode "Pattern..." action opens: the
    // accepted text, an empty string on cancel (a protected seam —
    // QInputDialog is untestable offscreen, the chooseFiles() pattern).
    virtual QString patternDialog(const QString &current);

    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onPlayingItemChanged(const MediaItem &item);
    void onEditPattern();
    void onToggleScroll();
    void onTick();

private:
    void updateText();
    void updateScrolling();
    int contentWidth() const;

    PlaybackManager* m_pm;
    QString m_pattern;
    // The marquee switch (off by default: a static label).
    bool m_scroll = false;
    QString m_text;
    int m_textWidth = 0;
    // The stream's cycle (the text plus the separator's advance): the
    // copies tile on it, the offset wraps at it.
    int m_cycle = 0;
    int m_offset = 0;
    QTimer* m_timer;

    // The label's frame (the border) is a fixed 1 px line.
    static const int kFrame = 1;
};

#endif // TRACKLABEL_H
