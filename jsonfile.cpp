#include "jsonfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

bool atomicWrite(const QString &path, const QByteArray &data, const char *who)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString tmp = path + ".tmp";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("%s: cannot write %s", who, qPrintable(tmp));
        return false;
    }
    if (f.write(data) != data.size()) {
        qWarning("%s: cannot write %s", who, qPrintable(tmp));
        f.close();
        QFile::remove(tmp);
        return false;
    }
    f.close();

    // QFile::rename refuses to replace an existing file (a previous
    // session's), so the target is removed first.
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        qWarning("%s: cannot move %s to %s", who, qPrintable(tmp),
                 qPrintable(path));
        QFile::remove(tmp);
        return false;
    }
    return true;
}
