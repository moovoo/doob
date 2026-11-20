#include "doobapi.h"

#include <QMainWindow>

#include "layoutmanager.h"
#include "mainwindow.h"
#include "settings.h"

QMainWindow *DoobApi::window() const
{
    return LayoutManager::getInstance()->window();
}

QString DoobApi::pluginDataPath(const QString &pluginName) const
{
    return Settings::instance()->pluginDataPath(pluginName);
}
