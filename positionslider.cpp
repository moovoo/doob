#include "positionslider.h"

#include "medialistmodel.h"

#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QFontMetrics>
#include <QtGlobal>

#include <algorithm>

namespace {

// A QSlider that jumps its handle to wherever the user left-clicks, so a
// bare click on the groove already lands on the target position and a
// press-and-hold continues as a regular drag from there. QSlider's own
// left press on the groove only steps the value by one page step (and
// repeats it while the button is held), which is useless for seeking.
class SeekSlider : public QSlider
{
public:
    explicit SeekSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton
            && event->buttons() == Qt::LeftButton
            && minimum() < maximum())
            setSliderPosition(valueAt(event->position().toPoint()));
        QSlider::mousePressEvent(event);
    }

private:
    // The same pixel-to-value mapping the base class uses for press
    // handling, so the jump lands exactly where the click says it should.
    int valueAt(const QPoint &pos) const
    {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                     QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                     QStyle::SC_SliderHandle, this);
        const int halfHandle = handle.width() / 2;
        const int sliderMin = groove.x();
        const int sliderMax = groove.right() - handle.width() + 1;
        return QStyle::sliderValueFromPosition(minimum(), maximum(),
                                               pos.x() - halfHandle - sliderMin,
                                               sliderMax - sliderMin, opt.upsideDown);
    }
};

} // namespace

PositionSliderWidget::PositionSliderWidget(QWidget *parent)
    : BaseWidget(parent)
{
    delete layout();

    m_pm = PlaybackManager::getInstance();

    m_positionLabel = new QLabel("0:00", this);
    m_positionLabel->setObjectName("positionLabel");

    m_slider = new SeekSlider(Qt::Horizontal, this);
    m_slider->setObjectName("positionSlider");
    m_slider->setRange(0, 0);
    m_slider->setEnabled(false);
    // The slider is the row's only stretchable part: without a minimum
    // width it collapses to nothing in a narrow widget and its handle
    // becomes unmovable.
    m_slider->setMinimumWidth(40);

    m_durationLabel = new QLabel("0:00", this);
    m_durationLabel->setObjectName("durationLabel");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_positionLabel);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_durationLabel);

    connect(m_pm, &PlaybackManager::positionChanged, this, &PositionSliderWidget::onPositionChanged);
    connect(m_pm, &PlaybackManager::durationChanged, this, &PositionSliderWidget::onDurationChanged);
    connect(m_pm, &PlaybackManager::playingItemChanged, this, &PositionSliderWidget::onPlayingItemChanged);
    connect(m_pm, &PlaybackManager::stateChanged, this, &PositionSliderWidget::onStateChanged);
    connect(m_slider, &QSlider::sliderPressed, this, &PositionSliderWidget::onSliderPressed);
    connect(m_slider, &QSlider::sliderMoved, this, &PositionSliderWidget::onSliderMoved);
    connect(m_slider, &QSlider::sliderReleased, this, &PositionSliderWidget::onSliderReleased);

    // Initialize from the current playback state: the signals only fire for
    // future changes, so a widget inserted while something is already
    // playing would otherwise start blank.
    onPlayingItemChanged(m_pm->playingItem());
    onStateChanged(m_pm->state());
}

void PositionSliderWidget::onPositionChanged(qint64 positionMs)
{
    if (m_scrubbing)
        return;

    const qint64 max = m_slider->maximum();
    m_slider->setValue(static_cast<int>(qBound<qint64>(0, positionMs, max)));
    m_positionLabel->setText(MediaListModel::formatDuration(positionMs));
}

void PositionSliderWidget::onDurationChanged(qint64 durationMs)
{
    const qint64 clamped = qMax<qint64>(0, durationMs);
    m_slider->setMaximum(static_cast<int>(qMin<qint64>(clamped, INT_MAX)));
    m_durationLabel->setText(MediaListModel::formatDuration(clamped));
    sizeLabels(m_positionLabel, m_durationLabel, clamped);
}

void PositionSliderWidget::onPlayingItemChanged(const MediaItem &item)
{
    if (!item.isValid()) {
        resetUi();
        return;
    }

    // A new track started (auto-advance or a user play): a scrub in
    // progress belongs to the previous track. Abort it so the slider
    // follows the new track instead of seeking a stale position into it.
    m_scrubbing = false;
    m_slider->setEnabled(true);
    onDurationChanged(m_pm->durationMs());
    onPositionChanged(m_pm->positionMs());
}

void PositionSliderWidget::onStateChanged(PlaybackManager::State state)
{
    if (state == PlaybackManager::Stopped) {
        resetUi();
        return;
    }
    m_slider->setEnabled(m_pm->playingItem().isValid());
}

void PositionSliderWidget::onSliderPressed()
{
    m_scrubbing = true;
}

void PositionSliderWidget::onSliderMoved(int value)
{
    // Only update the label while scrubbing; the actual seek happens on release.
    m_positionLabel->setText(MediaListModel::formatDuration(value));
}

void PositionSliderWidget::onSliderReleased()
{
    m_scrubbing = false;
    if (m_pm->playingItem().isValid())
        m_pm->setPositionMs(m_slider->value());
}

void PositionSliderWidget::resetUi()
{
    m_scrubbing = false;
    m_slider->setEnabled(false);
    m_slider->setMaximum(0);
    m_slider->setValue(0);
    m_positionLabel->setText(MediaListModel::formatDuration(0));
    m_durationLabel->setText(MediaListModel::formatDuration(0));
    sizeLabels(m_positionLabel, m_durationLabel, 0);
}

void PositionSliderWidget::sizeLabels(QLabel *positionLabel, QLabel *durationLabel,
                                      qint64 maxMs)
{
    // Both labels stay within [0, maxMs]: formatDuration(maxMs) is the
    // widest text either shows for the track, so it is their shared
    // minimum width — the pair keeps one width for the whole track and
    // the position label does not steal space from the slider as
    // playback advances.
    const QFontMetrics fm(positionLabel->font());
    const int width = fm.horizontalAdvance(MediaListModel::formatDuration(maxMs));
    positionLabel->setMinimumWidth(width);
    durationLabel->setMinimumWidth(width);
}
