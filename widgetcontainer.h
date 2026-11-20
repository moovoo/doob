#ifndef WIDGETCONTAINER_H
#define WIDGETCONTAINER_H

#include <QFrame>
#include <QHBoxLayout>
#include <QPointer>

#include "basewidget.h"
#include "widgetcontaineroverlay.h"

class WidgetContainer : public QFrame
{
    Q_OBJECT
public:
    explicit WidgetContainer(QWidget *parent = nullptr);

    void setWidget(BaseWidget* widget, bool deleteOld = true);
    BaseWidget* getWidget() const;
    WidgetContainerOverlay* getOverlayWidget() const;

    void forceInstallEventFilter();

    WidgetConfiguration storeContainer();
    void restoreContainer(WidgetConfiguration& config);

    // Public to match QObject::eventFilter() (it is public in Qt 6).
    bool eventFilter(QObject *obj, QEvent *event) override;

protected:
    QHBoxLayout* m_layout;
    WidgetContainerOverlay* m_overlayWidget;
    QPointer<BaseWidget> m_childWidget;
    // The current right-click's decision (the flip-flop's state at the
    // press; see eventFilter). The release and the synthesized context-
    // menu event follow it, so a click's events act on one decision even
    // when the platform delivers them in a different order.
    bool m_thisClickPassThrough = false;

    void childEvent(QChildEvent * event) override;
    void resizeEvent(QResizeEvent * event) override;
    void paintEvent(QPaintEvent * event) override;
    void mouseDoubleClickEvent(QMouseEvent * event) override;

};

#endif // WIDGETCONTAINER_H
