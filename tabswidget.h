#ifndef TABSWIDGET_H
#define TABSWIDGET_H

#include <QTabWidget>
#include <QList>
#include "basewidget.h"

class WidgetContainer;
class TabsWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "tabs";
    inline static const QString title = "Tabs";
    inline static const QString tag = widgetTags::layout;

    explicit TabsWidget(QWidget *parent = nullptr);
    QString widgetId() const override {return id;}
    QString widgetTitle() const override {return title;}

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    QMenu* widgetContextMenu() override;

private slots:
    void onAddTab(bool checked);
    void onRemoveTab(bool checked);
    void onRenameTab(bool checked);
    void onTabDoubleClicked(int index);
    void onToggleMovable(bool checked);
    void onTabMoved(int from, int to);

private:
    void addTab(const QString& name);
    void removeTab(int index);
    void renameTabAt(int index);

    QTabWidget* m_tabs;
    QList<WidgetContainer*> m_containers;
};

#endif // TABSWIDGET_H
