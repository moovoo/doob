#ifndef SPLITTERWIDGET_H
#define SPLITTERWIDGET_H

#include <QSplitter>
#include <QWidget>
#include <QPointer>
#include <QList>
#include "basewidget.h"

class WidgetContainer;
class SplitterWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "splitter";
    inline static const QString title = "Splitter";
    inline static const QString tag = widgetTags::layout;

    explicit SplitterWidget(QWidget *parent = nullptr);
    QString widgetId() const override {return id;}
    QString widgetTitle() const override {return title;}

    // The child containers (the split's sections, in order).
    QList<WidgetContainer*> containers() const { return m_containers; }

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    QMenu* widgetContextMenu() override;

private:
    QSplitter* m_splitter;

    QList<WidgetContainer*> m_containers;

private slots:
    void onToggleOrientation(bool checked);
    void onSetSections(bool checked);

private:
    void appendSlot();
    void removeSlot();
};

#endif // SPLITTERWIDGET_H
