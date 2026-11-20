#include "windowtogglewidget.h"

#include "windowmanager.h"
#include "windowsdialog.h"

#include <QBoxLayout>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>

WindowToggleWidget::WindowToggleWidget(QWidget *parent)
    : BaseWidget(parent)
{
    m_button = new QPushButton(this);
    m_button->setObjectName("toggleButton");
    m_button->setCheckable(true);
    // The base's box layout: the button sits centered between stretches.
    QBoxLayout* box = qobject_cast<QBoxLayout*>(layout());
    box->addStretch(1);
    box->addWidget(m_button);
    box->addStretch(1);

    connect(m_button, &QPushButton::clicked, this, &WindowToggleWidget::onButtonClicked);

    // The window's live visibility is the single source of truth: a
    // change from anywhere (the windows editor, the window's own close
    // button, another toggle of the same window) is mirrored here.
    WindowManager* wm = WindowManager::getInstance();
    connect(wm, &WindowManager::windowVisibleChanged, this,
            [this](const QString &id, bool visible) {
        if (id == m_windowId)
            m_button->setChecked(visible);
    });
    // A bound window that was deleted turns the button into "?".
    connect(wm, &WindowManager::windowsChanged, this, [this] {
        updateButton();
    });

    // A widget inserted while a window is already visible must start
    // in sync (the signals only fire for future changes).
    updateButton();
}

QMenu* WindowToggleWidget::widgetContextMenu()
{
    QMenu* menu = BaseWidget::widgetContextMenu();

    menu->addSeparator();

    QAction* caption = menu->addAction("Edit caption...");
    connect(caption, &QAction::triggered, this, [this] {
        const QString text = promptCaption(m_caption).trimmed();
        if (!text.isEmpty()) {
            m_caption = text;
            updateButton();
        }
    });

    QAction* choose = menu->addAction("Choose window...");
    connect(choose, &QAction::triggered, this, [this] {
        openWindowPicker(m_windowId);
    });

    return menu;
}

void WindowToggleWidget::openWindowPicker(const QString &selectedId)
{
    WindowsDialog dialog(WindowsDialog::Mode::Picker, this, selectedId);
    if (dialog.exec() == QDialog::Accepted)
        setWindowId(dialog.selectedId());
}

QString WindowToggleWidget::promptCaption(const QString &current)
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, "Window toggle",
                                               "Button caption:",
                                               QLineEdit::Normal, current, &ok);
    return ok ? text : QString();
}

WidgetConfiguration WindowToggleWidget::storeWidget()
{
    WidgetConfiguration config = BaseWidget::storeWidget();
    if (!m_windowId.isEmpty())
        config.insert("window_id", m_windowId);
    if (!m_caption.isEmpty())
        config.insert("caption", m_caption);
    return config;
}

void WindowToggleWidget::restoreWidget(WidgetConfiguration &config)
{
    BaseWidget::restoreWidget(config);
    m_windowId = config.value("window_id").toString();
    m_caption = config.value("caption").toString();
    updateButton();
}

void WindowToggleWidget::setWindowId(const QString &id)
{
    m_windowId = id;
    // A (re)selection seeds the caption with the window's title (a
    // rename of the window afterwards does not touch it).
    if (WindowManager::getInstance()->hasWindow(id))
        m_caption = WindowManager::getInstance()->windowTitle(id);
    updateButton();
}

void WindowToggleWidget::setCaption(const QString &caption)
{
    m_caption = caption;
    updateButton();
}

void WindowToggleWidget::onButtonClicked()
{
    WindowManager* wm = WindowManager::getInstance();
    if (!wm->hasWindow(m_windowId)) {
        // Nothing bound (or the window was deleted): pick one.
        openWindowPicker(QString());
        return;
    }
    // The visibility signal mirrors the result back onto the button.
    wm->setWindowVisible(m_windowId, !wm->windowVisible(m_windowId));
}

void WindowToggleWidget::updateButton()
{
    WindowManager* wm = WindowManager::getInstance();
    if (wm->hasWindow(m_windowId)) {
        m_button->setText(m_caption.isEmpty()
            ? wm->windowTitle(m_windowId)
            : m_caption);
        m_button->setChecked(wm->windowVisible(m_windowId));
    } else {
        m_button->setText("?");
        m_button->setChecked(false);
    }
}
