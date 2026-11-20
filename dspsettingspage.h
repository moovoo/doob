#ifndef DSPSETTINGSPAGE_H
#define DSPSETTINGSPAGE_H

#include <QWidget>

class DspRegistry;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// The "DSP" topic page of the preferences dialog: the chain — every
// registered processor in its running order, with its switch and
// up/down rearrange (both written through by the DspRegistry), and a
// Configure button for the processors that ship a settings page.
class DspSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit DspSettingsPage(QWidget *parent = nullptr);

private slots:
    void onCurrentRowChanged(int row);
    void onMoveUp();
    void onMoveDown();
    void onConfigure();
    void onChainChanged();

private:
    void onItemChanged(QListWidgetItem* item);
    void refresh();
    QString selectedId() const;

    DspRegistry* m_registry = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_up = nullptr;
    QPushButton* m_down = nullptr;
    QPushButton* m_configure = nullptr;
    bool m_updating = false;
};

#endif // DSPSETTINGSPAGE_H
