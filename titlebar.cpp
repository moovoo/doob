#include "titlebar.h"

#include "playbackmanager.h"

#include <QApplication>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QWindow>

TitleBarWidget::TitleBarWidget(QWidget *parent)
    : BaseWidget(parent)
{
    PlaybackManager* pm = PlaybackManager::getInstance();

    // The app's icon (the embedded one when the application has none —
    // a test's app).
    QIcon appIcon = QApplication::windowIcon();
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral(":/icons/doob.svg"));

    m_icon = new QLabel(this);
    m_icon->setObjectName("titleBarIcon");
    m_icon->setPixmap(appIcon.pixmap(QSize(16, 16)));

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("titleLabel");

    m_minButton = new QPushButton(QStringLiteral("—"), this);
    m_minButton->setObjectName("minButton");
    m_minButton->setToolTip(QStringLiteral("Minimize"));
    m_maxButton = new QPushButton(QStringLiteral("□"), this);
    m_maxButton->setObjectName("maxButton");
    m_maxButton->setToolTip(QStringLiteral("Maximize"));
    m_closeButton = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), this);
    m_closeButton->setObjectName("closeButton");
    m_closeButton->setToolTip(QStringLiteral("Close"));

    QHBoxLayout* box = qobject_cast<QHBoxLayout*>(layout());
    box->addWidget(m_icon);
    box->addWidget(m_titleLabel, 1);
    box->addWidget(m_minButton);
    box->addWidget(m_maxButton);
    box->addWidget(m_closeButton);

    connect(m_minButton, &QPushButton::clicked, this, &TitleBarWidget::minimizeWindow);
    connect(m_maxButton, &QPushButton::clicked, this, &TitleBarWidget::toggleMaximize);
    connect(m_closeButton, &QPushButton::clicked, this, &TitleBarWidget::closeWindow);

    connect(pm, &PlaybackManager::playingItemChanged, this, &TitleBarWidget::updateTitle);
    connect(pm, &PlaybackManager::stateChanged, this, &TitleBarWidget::updateTitle);

    // The strip's height (the "x-titlebar-height" property): a floor the
    // splitter cannot shrink past (the self-painted surface's own job —
    // never 0).
    setMinimumHeight(styleInt(QStringLiteral("x-titlebar-height"),
                              kDefaultHeight, 16, 128));

    updateControls();
    // A window that is already playing when the widget is inserted must
    // not start blank (the signals only fire for future changes).
    updateTitle();
}

QList<StyleProperty> TitleBarWidget::styleProperties()
{
    return {
        {
            QStringLiteral("x-titlebar-height"),
            title,
            QStringLiteral("The strip's height, in pixels."),
            QStringLiteral("32"),
            QStringLiteral("px"),
            16,
            128,
        },
        {
            QStringLiteral("x-titlebar-color"),
            title,
            QStringLiteral("The strip's background color."),
            QStringLiteral("auto"),
            QStringLiteral("color"),
            0,
            0,
        },
        {
            QStringLiteral("x-titlebar-text-color"),
            title,
            QStringLiteral("The strip's title text color."),
            QStringLiteral("auto"),
            QStringLiteral("color"),
            0,
            0,
        },
    };
}

void TitleBarWidget::setShowMinimizeButton(bool show)
{
    m_showMin = show;
    updateControls();
}

void TitleBarWidget::setShowMaximizeButton(bool show)
{
    m_showMax = show;
    updateControls();
}

void TitleBarWidget::setShowCloseButton(bool show)
{
    m_showClose = show;
    updateControls();
}

WidgetConfiguration TitleBarWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    // Only non-default states are persisted, so a fresh layout document
    // (or a copy of the widget) cannot pin the defaults.
    if (!m_showMin || !m_showMax || !m_showClose) {
        QVariantMap controls;
        controls.insert("min", m_showMin);
        controls.insert("max", m_showMax);
        controls.insert("close", m_showClose);
        config.insert("controls", controls);
    }
    return config;
}

void TitleBarWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    const QVariantMap controls = config.value("controls").toMap();
    if (controls.contains("min"))
        m_showMin = controls.value("min").toBool();
    if (controls.contains("max"))
        m_showMax = controls.value("max").toBool();
    if (controls.contains("close"))
        m_showClose = controls.value("close").toBool();
    updateControls();
    updateTitle();
}

void TitleBarWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const QColor background = styleColorValue(
        QStringLiteral("x-titlebar-color"), palette().color(QPalette::Window));
    const QColor text = styleColorValue(
        QStringLiteral("x-titlebar-text-color"), palette().color(QPalette::WindowText));
    // The label's palette is the text color's surface (idempotent).
    if (m_titleLabel->palette().color(QPalette::WindowText).rgb() != text.rgb()) {
        QPalette pal = m_titleLabel->palette();
        pal.setColor(QPalette::WindowText, text);
        m_titleLabel->setPalette(pal);
    }
    QPainter p(this);
    p.fillRect(rect(), background);
}

void TitleBarWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        startSystemMove();
    QWidget::mousePressEvent(event);
}

void TitleBarWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        toggleMaximize();
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBarWidget::startSystemMove()
{
    if (windowHandle() != nullptr)
        windowHandle()->startSystemMove();
}

void TitleBarWidget::minimizeWindow()
{
    window()->showMinimized();
}

void TitleBarWidget::toggleMaximize()
{
    QWidget* top = window();
    if (top->isMaximized())
        top->showNormal();
    else
        top->showMaximized();
}

void TitleBarWidget::closeWindow()
{
    window()->close();
}

void TitleBarWidget::updateTitle()
{
    // The window title's computation (the "Title — Artist" while playing
    // or paused, the bare app name on stop).
    PlaybackManager* pm = PlaybackManager::getInstance();
    const MediaItem item = pm->playingItem();
    QString text = QStringLiteral("doob");
    if (item.isValid() && pm->state() != PlaybackManager::Stopped) {
        text = item.title;
        if (text.isEmpty())
            text = QFileInfo(item.source.toLocalFile()).fileName();
        if (!item.artist.isEmpty())
            text += QStringLiteral(" — ") + item.artist;
    }
    m_titleLabel->setText(text);
}

void TitleBarWidget::updateControls()
{
    m_minButton->setVisible(m_showMin);
    m_maxButton->setVisible(m_showMax);
    m_closeButton->setVisible(m_showClose);
}
