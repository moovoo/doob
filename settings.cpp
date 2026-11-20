#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

Settings::Settings()
{
    const QString envDir = QProcessEnvironment::systemEnvironment().value("DOOB_DATA_DIR");
    if (!envDir.isEmpty()) {
        m_dataDir = envDir;
    } else {
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    // The user's layouts and styles, next to the other data.
    QDir().mkpath(m_dataDir + "/layouts");
    QDir().mkpath(m_dataDir + "/styles");

    // The read-only presets (the shipped layouts and styles, installed
    // by the package manager), first existing candidate wins — the
    // same search shape as the plugin directories. <app dir>/share/
    // doob is the dev-build location (CMake copies the repo's presets/
    // tree there, next to the executable); <app dir>/../share/doob is
    // the split layout (FHS install and AppImage: the binary in
    // usr/bin, the resources in usr/share); the standard data
    // locations cover the rest. The data directory itself is never a
    // preset source (it holds the user's own layouts/ and styles/).
    const QStringList candidates = {
        QProcessEnvironment::systemEnvironment().value("DOOB_PRESETS_DIR"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/share/doob"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/doob"),
    };
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QDir(candidate).exists()) {
            m_presetsDir = candidate;
            break;
        }
    }
    if (m_presetsDir.isEmpty()) {
        for (const QString &location :
             QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
            const QString candidate = location + QStringLiteral("/doob");
            if (candidate == m_dataDir)
                continue;
            if (QDir(candidate).exists()) {
                m_presetsDir = candidate;
                break;
            }
        }
    }
}

Settings* Settings::instance()
{
    if (m_instance == nullptr)
        m_instance = new Settings();
    return m_instance;
}

void Settings::reset()
{
    delete m_instance;
    m_instance = nullptr;
}
