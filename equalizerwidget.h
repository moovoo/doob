#ifndef EQUALIZERWIDGET_H
#define EQUALIZERWIDGET_H

#include <QList>
#include <QString>
#include <QVariant>

#include "basewidget.h"

class QSlider;
class QToolButton;
class SettingsManager;

// The 10-band equalizer: one vertical slider per band (±12 dB in
// 0.5 dB steps) over the band's frequency, plus the driver's on/off
// toggle, the preset menu and a reset to flat. The gains are not
// per-instance state: the EQ is the FFmpeg driver's DSP (eq.h,
// registered in the core's DspRegistry), so several widgets on
// screen are views of the same effect and stay in sync through the
// SettingsManager's changed() (the gains) and the registry's
// chainChanged() (the on/off switch — the same state the DSP
// preferences topic edits). With the driver not loaded (the DSP not
// registered) the widget is disabled — there is no open PCM path to
// shape.
class EqualizerWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "equalizer";
    inline static const QString title = "Equalizer";
    inline static const QString tag = widgetTags::playback;

    explicit EqualizerWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

private slots:
    void onSliderChanged();
    void onSettingChanged(const QString &key, const QVariant &value);
    void onEnableToggled(bool enabled) const;
    void onReset();
    // The registry's chainChanged (the switch, the driver loading).
    void onChainChanged();

private:
    void applyGains(const QList<double> &gains);
    void refresh();
    // The sliders' values as the stored gains (dB per band), and the
    // inverse (a shorter list pads with flat).
    QVariantList gainsFromSliders() const;
    void slidersFromGains(const QVariantList &gains);

    SettingsManager *m_settings = nullptr;
    QToolButton *m_enable = nullptr;
    QList<QSlider *> m_sliders;
    bool m_updating = false;
};

#endif // EQUALIZERWIDGET_H
