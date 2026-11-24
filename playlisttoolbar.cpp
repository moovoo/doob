#include "playlisttoolbar.h"
#include "librarymanager.h"
#include "playlistmanager.h"

#include <QAction>
#include <QChar>
#include <QFileDialog>
#include <QObject>
#include <QInputDialog>
#include <QLineEdit>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWidget>

PlaylistToolbarButtons createPlaylistToolbar(QToolBar *toolbar,
                                             const QString &target)
{
    PlaylistToolbarButtons buttons;

    buttons.addButton = new QToolButton(toolbar);
    buttons.addButton->setObjectName("addButton");
    buttons.addButton->setText("+");
    buttons.addButton->setToolTip(QStringLiteral("Add items to %1").arg(target));
    buttons.addButton->setPopupMode(QToolButton::InstantPopup);
    buttons.addMenu = new QMenu(buttons.addButton);
    buttons.addButton->setMenu(buttons.addMenu);
    buttons.addFilesAction = buttons.addMenu->addAction("Add files...");
    buttons.addFilesAction->setObjectName("addFilesAction");
    buttons.addUrlAction = buttons.addMenu->addAction("Add by URL...");
    buttons.addUrlAction->setObjectName("addUrlAction");
    toolbar->addWidget(buttons.addButton);

    buttons.removeButton = new QToolButton(toolbar);
    buttons.removeButton->setObjectName("removeButton");
    buttons.removeButton->setText(QString(QChar(0x2212)));
    buttons.removeButton->setToolTip(QStringLiteral("Remove the selection from %1").arg(target));
    toolbar->addWidget(buttons.removeButton);

    return buttons;
}

void addToolbarSpacer(QToolBar *toolbar)
{
    QWidget *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
}

QString audioFileChooserFilter()
{
    QString extensions;
    for (const QString &extension : LibraryManager::supportedExtensions()) {
        extensions += QLatin1Char(' ');
        extensions += QLatin1Char('*');
        extensions += extension;
    }
    return QStringLiteral("Audio files (%1);;All files (*)")
        .arg(extensions.trimmed());
}

QStringList chooseAudioFiles(QWidget *parent)
{
    return QFileDialog::getOpenFileNames(parent, "Add audio files", QString(),
                                         audioFileChooserFilter());
}

QString choosePlaylistExportPath(QWidget *parent, const QString &suggested)
{
    return QFileDialog::getSaveFileName(
        parent, "Export playlist", suggested,
        "Playlist files (*.m3u *.m3u8 *.pls);;All files (*)");
}

QString askPlaylistRename(QWidget *parent, const QString &current)
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(parent, "Rename playlist", "Name:",
                              QLineEdit::Normal, current, &ok);
    return ok ? name : QString();
}

void addPlaylistVerbActions(QMenu *menu, quint64 playlistId,
                            const std::function<QString(const QString &)> &askName,
                            const std::function<QString(const QString &)> &chooseExport)
{
    PlaylistManager *pl = PlaylistManager::getInstance();

    QAction *rename = menu->addAction("Rename...");
    QObject::connect(rename, &QAction::triggered, [pl, playlistId, askName] {
        const Playlist current = pl->playlist(playlistId);
        if (!current.isValid())
            return;
        const QString name = askName(current.name).trimmed();
        if (!name.isEmpty())
            pl->renamePlaylist(playlistId, name);
    });

    QAction *exportAction = menu->addAction("Export to file...");
    QObject::connect(exportAction, &QAction::triggered, [pl, playlistId, chooseExport] {
        const Playlist current = pl->playlist(playlistId);
        if (!current.isValid())
            return;
        const QString path =
            chooseExport(current.name + QStringLiteral(".m3u"));
        if (!path.isEmpty())
            pl->exportPlaylist(playlistId, path);
    });

    // A save writes the imported playlist back to the file it came
    // from.
    QAction *save = menu->addAction("Save");
    const Playlist selected = pl->playlist(playlistId);
    save->setEnabled(selected.isValid() && selected.imported
                     && !selected.sourceFile.isEmpty());
    QObject::connect(save, &QAction::triggered, [pl, playlistId] {
        const Playlist current = pl->playlist(playlistId);
        if (current.isValid() && current.imported
            && !current.sourceFile.isEmpty())
            pl->savePlaylist(playlistId,
                             QUrl(current.sourceFile).toLocalFile());
    });
}
