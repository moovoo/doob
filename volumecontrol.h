#ifndef VOLUMECONTROL_H
#define VOLUMECONTROL_H

#include "basewidget.h"
#include "playbackmanager.h"

class QSlider;
class QLabel;
class QToolButton;

class VolumeControlWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "volume";
    inline static const QString title = "Volume";
    inline static const QString tag = widgetTags::playback;

    explicit VolumeControlWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

private slots:
    void onSliderChanged(int value);
    void onMuteToggled(bool checked);
    void onVolumeChanged(int volume);
    void onMutedChanged(bool muted);
    void onVolumeSupportChanged(bool supported);

private:
    void updateLabel();

    PlaybackManager* m_pm;
    QToolButton* m_muteButton;
    QSlider* m_slider;
    QLabel* m_label;
    // True while the constructor reflects the manager's state into the
    // UI: the slider's valueChanged must not feed back into the manager
    // then (a setVolume while muted would unmute it mid-sync).
    bool m_syncing = false;
};

#endif // VOLUMECONTROL_H
