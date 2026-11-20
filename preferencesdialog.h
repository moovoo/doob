#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>

class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class UiState;

// The preferences dialog (File -> Preferences): the list of settings
// topics on the left (the core's topics under "Main", the plugins' under
// "Plugins") and the selected topic's settings page on the right. A
// plugin that registered no topics is still listed, with a "no settings"
// note in place of a page. The topics tree is fully expanded on dialog
// show. OK closes the dialog; the pages own their state (they apply it
// themselves).
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    // The id of the selected topic ("" when the dialog has no topics).
    QString selectedTopicId() const;

private:
    void buildTopics();
    QTreeWidgetItem* addEntry(QTreeWidgetItem *group, const QString &title,
                              const QString &topicId, QWidget *page);

    QTreeWidget *m_tree;
    QStackedWidget *m_pages;
    UiState* m_state = nullptr;
};

#endif // PREFERENCESDIALOG_H
