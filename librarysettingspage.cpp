#include "librarysettingspage.h"

#include "librarywatcher.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

LibrarySettingsPage::LibrarySettingsPage(QWidget *parent)
    : QWidget(parent)
    , m_watchDirs(nullptr)
    , m_add(nullptr)
    , m_remove(nullptr)
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* title = new QLabel("Watch directories", this);
    layout->addWidget(title);

    m_watchDirs = new QListWidget(this);
    m_watchDirs->setObjectName("watchDirs");
    layout->addWidget(m_watchDirs, 1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_add = new QPushButton("Add...", this);
    m_add->setObjectName("addWatchDirButton");
    buttonLayout->addWidget(m_add);
    m_remove = new QPushButton("Remove", this);
    m_remove->setObjectName("removeWatchDirButton");
    buttonLayout->addWidget(m_remove);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);

    connect(m_add, &QPushButton::clicked, this, &LibrarySettingsPage::onAddClicked);
    connect(m_remove, &QPushButton::clicked, this, &LibrarySettingsPage::onRemoveClicked);
    connect(LibraryWatcher::getInstance(), &LibraryWatcher::watchDirsChanged,
            this, &LibrarySettingsPage::onWatchDirsChanged);

    // Initialize from the current watcher state (the signal only covers
    // future changes).
    refresh();
}

QStringList LibrarySettingsPage::watchDirs() const
{
    return LibraryWatcher::getInstance()->watchDirs();
}

QString LibrarySettingsPage::chooseDir()
{
    return QFileDialog::getExistingDirectory(this, "Watch directory");
}

void LibrarySettingsPage::onAddClicked()
{
    const QString dir = chooseDir();
    if (!dir.isEmpty())
        LibraryWatcher::getInstance()->addWatchDir(dir);
}

void LibrarySettingsPage::onRemoveClicked()
{
    if (QListWidgetItem* item = m_watchDirs->currentItem())
        LibraryWatcher::getInstance()->removeWatchDir(item->text());
}

void LibrarySettingsPage::onWatchDirsChanged()
{
    refresh();
}

void LibrarySettingsPage::refresh()
{
    m_watchDirs->clear();
    for (const QString &dir : LibraryWatcher::getInstance()->watchDirs())
        m_watchDirs->addItem(dir);
    m_remove->setEnabled(m_watchDirs->count() > 0);
}
