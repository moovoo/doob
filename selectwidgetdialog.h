#ifndef SELECTWIDGETDIALOG_H
#define SELECTWIDGETDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QWidget>

#include "widgetregistry.h"

class SelectWidgetDialog: public QDialog
{
    Q_OBJECT
public:
    explicit SelectWidgetDialog(const QList<WidgetRegistry::WidgetOption>& widgets, const QString &selected, QWidget* parent = nullptr);
    QString value() const;
private slots:
    void onListDblClick(QModelIndex index);
private:
    QListWidget *m_list;
};

#endif // SELECTWIDGETDIALOG_H
