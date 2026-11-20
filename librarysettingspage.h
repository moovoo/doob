#ifndef LIBRARYSETTINGSPAGE_H
#define LIBRARYSETTINGSPAGE_H

#include <QStringList>
#include <QWidget>

class QListWidget;
class QPushButton;

// The "Library" topic page of the preferences dialog: the list of
// watched directories, with add/remove. The list is the
// LibraryWatcher's watch list (persisted as library.json); the watcher
// imports the contents of an added directory and keeps it in sync.
class LibrarySettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit LibrarySettingsPage(QWidget *parent = nullptr);

    // The watched directories.
    QStringList watchDirs() const;

protected:
    // Test seam (QFileDialog is untestable offscreen): pick the
    // directory to add.
    virtual QString chooseDir();

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onWatchDirsChanged();

private:
    void refresh();

    QListWidget *m_watchDirs;
    QPushButton *m_add;
    QPushButton *m_remove;
};

#endif // LIBRARYSETTINGSPAGE_H
