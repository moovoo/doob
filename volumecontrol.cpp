#include "volumecontrol.h"

#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QIcon>

VolumeControlWidget::VolumeControlWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_pm = PlaybackManager::getInstance();

    m_muteButton = new QToolButton(this);
    m_muteButton->setObjectName("muteButton");
    m_muteButton->setCheckable(true);
    m_muteButton->setAutoRaise(true);
    // Icon-only button (icons/ directory, /icons resource prefix); the
    // icons share the 24x24 geometry of the playback bar's set.
    m_muteButton->setIcon(QIcon(QStringLiteral(":/icons/volume.svg")));
    m_muteButton->setToolTip("Mute");

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setObjectName("volumeSlider");
    m_slider->setRange(0, 100);
    // The slider is the row's only stretchable part: without a minimum
    // width it collapses to nothing in a narrow widget and its handle
    // becomes unmovable.
    m_slider->setMinimumWidth(40);

    m_label = new QLabel(this);
    m_label->setObjectName("volumeLabel");
    // A minimum for the widest text it shows ("Muted" and the maximum
    // level "100%"): the label keeps one width for every value, so
    // sliding does not take space from (or give it back to) the slider.
    const QFontMetrics labelFont{m_label->font()};
    m_label->setMinimumWidth(qMax(labelFont.horizontalAdvance(QStringLiteral("Muted")),
                                  labelFont.horizontalAdvance(QStringLiteral("100%"))));

    // The base's zero-margin QHBoxLayout is the row as-is:
    // [mute] [slider ...] [label].
    QHBoxLayout* box = qobject_cast<QHBoxLayout*>(layout());
    box->addWidget(m_muteButton);
    box->addWidget(m_slider, 1);
    box->addWidget(m_label);

    connect(m_pm, &PlaybackManager::volumeChanged, this,
            &VolumeControlWidget::onVolumeChanged);
    connect(m_pm, &PlaybackManager::mutedChanged, this,
            &VolumeControlWidget::onMutedChanged);
    connect(m_pm, &PlaybackManager::volumeSupportChanged, this,
            &VolumeControlWidget::onVolumeSupportChanged);
    connect(m_slider, &QSlider::valueChanged, this,
            &VolumeControlWidget::onSliderChanged);
    connect(m_muteButton, &QToolButton::toggled, this,
            &VolumeControlWidget::onMuteToggled);

    // Initialize from the current playback state: the signals only fire
    // for future changes, so a widget inserted with a non-default volume
    // (or a driver switch in flight) would otherwise start stale. The
    // guard keeps the slider's valueChanged from feeding back into the
    // manager while the state is reflected (a setVolume while muted
    // would unmute it mid-sync).
    m_syncing = true;
    onVolumeSupportChanged(m_pm->volumeSupported());
    onVolumeChanged(m_pm->volume());
    onMutedChanged(m_pm->muted());
    m_syncing = false;
}

void VolumeControlWidget::onSliderChanged(int value)
{
    if (m_syncing)
        return;
    // The manager emits nothing when the value is unchanged (and a
    // change while muted unmutes), so the sync from its signals cannot
    // loop back into this slot.
    m_pm->setVolume(value);
}

void VolumeControlWidget::onMuteToggled(bool checked)
{
    if (m_syncing)
        return;
    m_pm->setMuted(checked);
}

void VolumeControlWidget::onVolumeChanged(int volume)
{
    if (m_slider->value() != volume)
        m_slider->setValue(volume);
    updateLabel();
}

void VolumeControlWidget::onMutedChanged(bool muted)
{
    m_muteButton->setChecked(muted);
    m_muteButton->setIcon(QIcon(muted ? QStringLiteral(":/icons/volume-off.svg")
                                      : QStringLiteral(":/icons/volume.svg")));
    m_muteButton->setToolTip(muted ? "Unmute" : "Mute");
    updateLabel();
}

void VolumeControlWidget::onVolumeSupportChanged(bool supported)
{
    m_slider->setEnabled(supported);
    m_muteButton->setEnabled(supported);
}

void VolumeControlWidget::updateLabel()
{
    m_label->setText(m_pm->muted()
                         ? QStringLiteral("Muted")
                         : QStringLiteral("%1%").arg(m_pm->volume()));
}
