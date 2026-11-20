#ifndef WIDGETCONTAINEROVERLAY_H
#define WIDGETCONTAINEROVERLAY_H

#include <QPointer>
#include <QWidget>

class WidgetContainer;

class WidgetContainerOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit WidgetContainerOverlay(WidgetContainer *parent = nullptr);
public slots:
    void grayed(bool val);
protected:
    WidgetContainer* m_parentContainer;
    bool m_grayed;
    void paintEvent(QPaintEvent * event) override;
};

#endif // WIDGETCONTAINEROVERLAY_H
