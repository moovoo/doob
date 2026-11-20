#ifndef POSITIONSLIDER_H
#define POSITIONSLIDER_H

#include "basewidget.h"
#include "playbackmanager.h"

class QSlider;
class QLabel;

class PositionSliderWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "position";
    inline static const QString title = "Position";
    inline static const QString tag = widgetTags::playback;

    explicit PositionSliderWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

private slots:
    void onPositionChanged(qint64 positionMs);
    void onDurationChanged(qint64 durationMs);
    void onPlayingItemChanged(const MediaItem &item);
    void onStateChanged(PlaybackManager::State state);
    void onSliderPressed();
    void onSliderMoved(int value);
    void onSliderReleased();

private:
    void resetUi();
    static void sizeLabels(QLabel *positionLabel, QLabel *durationLabel, qint64 maxMs);

    PlaybackManager* m_pm;
    QSlider* m_slider;
    QLabel* m_positionLabel;
    QLabel* m_durationLabel;
    bool m_scrubbing = false;
};

#endif // POSITIONSLIDER_H
