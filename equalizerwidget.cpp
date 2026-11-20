#include "equalizerwidget.h"

#include "dspregistry.h"
#include "eq.h"
#include "settingsmanager.h"

#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{

// The band's caption: the hertz, the kilohertz abbreviated.
QString bandLabel(int index)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    const double hz = eq::Hz[index];
    if (hz >= 1000)
        return QStringLiteral("%1k").arg(hz / 1000.0, 0, 'g');
    return QStringLiteral("%1").arg(hz, 0, 'f', 0);
}

// The presets: convenient gain patterns (every value a multiple of
// the slider's 0.5 dB step).
QList<QPair<QString, QList<double>>> presetCatalog()
{
    return {
        {QStringLiteral("Flat"), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("Bass boost"), {6, 6, 4, 2, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("Treble boost"), {0, 0, 0, 0, 0, 0, 2, 4, 6, 6}},
        {QStringLiteral("Vocal"), {0, 1, 3, 4, 2, 0, -1, -1, 1, 2}},
        {QStringLiteral("Loudness"), {4, 4, 2, 0, 0, 0, -1, -1, 0, 3}},
        {QStringLiteral("Rock"), {4, 4, 2, -1, -2, 1, 3, 4, 4, 3}},
    };
}

int toSliderStep(double db)
{
    return qRound(db / eq::GainStepDb);
}

} // namespace

EqualizerWidget::EqualizerWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_settings = SettingsManager::getInstance();

    // The base's row layout fits no ten bands: a grid, one column per
    // band (the slider over its frequency), in a box so the toolbar
    // machinery can insert its strip.
    delete layout();
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    QGridLayout *grid = new QGridLayout;
    for (int i = 0; i < eq::BandCount; ++i) {
        QSlider *slider = new QSlider(Qt::Vertical);
        slider->setObjectName(QStringLiteral("bandSlider%1").arg(i));
        slider->setRange(-toSliderStep(eq::MaxGainDb),
                         toSliderStep(eq::MaxGainDb));
        slider->setTickPosition(QSlider::TicksBothSides);
        slider->setTickInterval(toSliderStep(6.0));
        connect(slider, &QSlider::valueChanged, this,
                &EqualizerWidget::onSliderChanged);
        m_sliders.append(slider);
        grid->addWidget(slider, 0, i);
        QLabel *label = new QLabel(bandLabel(i));
        label->setObjectName(QStringLiteral("bandLabel%1").arg(i));
        label->setAlignment(Qt::AlignHCenter);
        grid->addWidget(label, 1, i);
        grid->setColumnStretch(i, 1);
    }
    grid->setRowStretch(0, 1);
    outer->addLayout(grid);

    // The toolbar: on/off, the presets, a reset to flat.
    QToolBar *toolbar = createToolbar(true);
    m_enable = new QToolButton(toolbar);
    m_enable->setObjectName(QStringLiteral("enableButton"));
    m_enable->setText(QStringLiteral("EQ"));
    m_enable->setCheckable(true);
    connect(m_enable, &QToolButton::toggled, this,
            &EqualizerWidget::onEnableToggled);
    toolbar->addWidget(m_enable);

    QToolButton *presets = new QToolButton(toolbar);
    presets->setObjectName(QStringLiteral("presetsButton"));
    presets->setText(QStringLiteral("Presets"));
    QMenu *menu = new QMenu(presets);
    for (const QPair<QString, QList<double>> &preset
         : presetCatalog()) {
        QAction *action = menu->addAction(preset.first);
        connect(action, &QAction::triggered, this,
                [this, gains = preset.second] { applyGains(gains); });
    }
    presets->setMenu(menu);
    presets->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(presets);

    QToolButton *reset = new QToolButton(toolbar);
    reset->setObjectName(QStringLiteral("resetButton"));
    reset->setText(QStringLiteral("Reset"));
    connect(reset, &QToolButton::clicked, this,
            &EqualizerWidget::onReset);
    toolbar->addWidget(reset);

    connect(m_settings, &SettingsManager::changed, this,
            &EqualizerWidget::onSettingChanged);
    connect(DspRegistry::getInstance(), &DspRegistry::chainChanged, this,
            &EqualizerWidget::onChainChanged);

    // Initialize from the current state: signals only fire for future
    // changes, so a widget inserted while gains are already set would
    // otherwise start flat.
    refresh();
}

void EqualizerWidget::refresh()
{
    m_updating = true;
    // Available only while the driver that owns the effect is loaded
    // (its DSP registered); with no driver there is nothing to shape,
    // no matter what the stored gains say.
    const bool available = DspRegistry::getInstance()->contains(eq::Id);
    for (QSlider *slider : m_sliders)
        slider->setEnabled(available);
    // The toolbar's buttons are child widgets, not actions (a QToolBar
    // added to by construction), so they are found, not listed.
    if (QToolBar *strip = toolbar())
        for (QToolButton *button : strip->findChildren<QToolButton *>())
            button->setEnabled(available);
    if (available) {
        slidersFromGains(
            m_settings->value(QString::fromLatin1(eq::GainsKey)).toList());
        m_enable->setChecked(DspRegistry::getInstance()->isEnabled(eq::Id));
    }
    m_updating = false;
}

void EqualizerWidget::onSliderChanged()
{
    if (m_updating)
        return;
    m_settings->setValue(QString::fromLatin1(eq::GainsKey), gainsFromSliders());
}

void EqualizerWidget::onSettingChanged(const QString &key,
                                       const QVariant &value)
{
    if (key == QLatin1String(eq::GainsKey)) {
        m_updating = true;
        slidersFromGains(value.toList());
        m_updating = false;
    }
}

void EqualizerWidget::onEnableToggled(bool enabled) const
{
    if (m_updating)
        return;
    DspRegistry::getInstance()->setEnabled(eq::Id, enabled);
}

void EqualizerWidget::onChainChanged()
{
    refresh();
}

void EqualizerWidget::onReset()
{
    // Flat: the sliders to 0 (the valueChanged writes the setting).
    for (QSlider *slider : m_sliders)
        if (slider->value() != 0)
            slider->setValue(0);
}

void EqualizerWidget::applyGains(const QList<double> &gains)
{
    QVariantList list;
    for (const double gain : gains)
        list << gain;
    m_updating = true;
    slidersFromGains(list);
    m_updating = false;
    // The write goes through the sliders (their step quantization is
    // what the store keeps).
    m_settings->setValue(QString::fromLatin1(eq::GainsKey), gainsFromSliders());
}

QVariantList EqualizerWidget::gainsFromSliders() const
{
    QVariantList gains;
    for (QSlider *slider : m_sliders)
        gains << slider->value() * eq::GainStepDb;
    return gains;
}

void EqualizerWidget::slidersFromGains(const QVariantList &gains)
{
    for (int i = 0; i < eq::BandCount; ++i)
        m_sliders[i]->setValue(i < gains.size()
                                   ? toSliderStep(gains.at(i).toDouble())
                                   : 0);
}
