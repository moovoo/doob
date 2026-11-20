#ifndef ENTRystore_H
#define ENTRystore_H

#include <QDir>
#include <QFile>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>

#include "jsonfile.h"
#include "names.h"
#include "settingsmanager.h"

// The shared core of the named file stores (the layouts' and the
// styles' directories): the user's files (userDir/<name>.ext), the
// read-only presets (presetDir/<name>.ext), and the active entry,
// held as the session-state setting currentKey. An entry's content is
// a ContentOps::Value (the layout's JSON document, the style's QSS
// text); the store sees the content only through the operations
// ContentOps provides: how a file becomes a content, a content +
// origin become a file's bytes, the origin a file records, the title
// a content carries, and how two contents compare. The operations
// that touch the live state (the load, the new, the delete's
// fallback) stay the caller's: the store settles the current and
// hands back the content to apply.
template <typename ContentOps>
class EntryStore
{
public:
    using Value = typename ContentOps::Value;

    // An entry in the listing: its name (the file's stem), the title
    // the menus and dialogs show, and whether it is read-only (a
    // preset).
    struct Info {
        QString name;
        QString title;
        bool readOnly = false;
    };

    struct Config {
        // The directories, resolved on every use (a test's
        // Settings::reset() may move them).
        std::function<QString()> userDir;
        std::function<QString()> presetDir;
        // The file extension, including the dot (".json", ".qss").
        QString extension;
        // The session-state setting key ("layout.current", "style.current").
        QString currentKey;
        // The fork's file name (the extension included) when the save
        // of a preset current takes a copy: the layouts' collision-
        // free "<name>.modified", the styles' fixed "<name>.modified"
        // (written over).
        std::function<QString(const QString &dir, const QString &name)> forkName;
    };

    explicit EntryStore(Config config)
        : m_config{std::move(config)} {}

    QString userDir() const { return m_config.userDir(); }
    QString presetDir() const { return m_config.presetDir(); }

    // The paths ("" when the entry has no such file).
    QString userPath(const QString &name) const
    {
        if (name.isEmpty())
            return {};
        const QString path = userDir() + QLatin1Char('/') + name + m_config.extension;
        return QFile::exists(path) ? path : QString();
    }

    QString presetPath(const QString &name) const
    {
        if (name.isEmpty())
            return {};
        const QString dir = presetDir();
        if (dir.isEmpty())
            return {};
        const QString path = dir + QLatin1Char('/') + name + m_config.extension;
        return QFile::exists(path) ? path : QString();
    }

    // True when `name` is a read-only preset (no user file shadows it).
    bool isPreset(const QString &name) const
    {
        return userPath(name).isEmpty() && !presetPath(name).isEmpty();
    }

    // True when the entry has a file anywhere (the switch's target
    // check).
    bool available(const QString &name) const
    {
        return !userPath(name).isEmpty() || !presetPath(name).isEmpty();
    }

    // The first user file by name ("" when there are none): the
    // fallback when the current entry's file is gone.
    QString firstUser() const
    {
        QDir dir(userDir());
        const QStringList files = dir.entryList({filter()}, QDir::Files, QDir::Name);
        if (files.isEmpty())
            return {};
        return files.first().left(files.first().size() - m_config.extension.size());
    }

    // Nothing to restore: no current (the setting unset) and no file
    // in the user's directory.
    bool firstRun() const
    {
        if (!current().isEmpty())
            return false;
        return QDir(userDir()).entryList({filter()}, QDir::Files).isEmpty();
    }

    // The session state: the active entry's name (empty = no current).
    QString current() const
    {
        return SettingsManager::getInstance()
            ->value(m_config.currentKey).toString();
    }

    void setCurrent(const QString &name)
    {
        // An empty name unsets the key (the default applies again: no
        // current), so the first-run detection (no current, no user
        // files) keeps working.
        SettingsManager *settings = SettingsManager::getInstance();
        if (name.isEmpty())
            settings->resetToDefault(m_config.currentKey);
        else
            settings->setValue(m_config.currentKey, name);
    }

    // The entry's content: the user's file, else the preset, else an
    // empty value (stale names).
    Value content(const QString &name) const
    {
        const QString path = userPath(name);
        const QString file = path.isEmpty() ? presetPath(name) : path;
        if (file.isEmpty())
            return Value{};
        return ContentOps::read(file);
    }

    // The origin ("preset:<name>") the entry's file records ("" when
    // it has none).
    QString originOf(const QString &name) const
    {
        const QString path = userPath(name);
        const QString file = path.isEmpty() ? presetPath(name) : path;
        if (file.isEmpty())
            return {};
        return ContentOps::origin(file);
    }

    // The listing: the user's files, then the presets a user file does
    // not shadow — sorted by title, case-insensitive (the name when
    // the content carries no title).
    QList<Info> list() const
    {
        QList<Info> all;
        QSet<QString> listed;
        const QString userDirPath = userDir();
        const QDir userDir{userDirPath};
        for (const QString &file : userDir.entryList({filter()}, QDir::Files, QDir::Name)) {
            const QString name = file.left(file.size() - m_config.extension.size());
            listed.insert(name);
            const QString title =
                ContentOps::title(ContentOps::read(userDir.absoluteFilePath(file)));
            all.append(Info{name, title.isEmpty() ? name : title, false});
        }
        const QString presetDirPath = presetDir();
        if (!presetDirPath.isEmpty()) {
            const QDir presets{presetDirPath};
            for (const QString &file :
                 presets.entryList({filter()}, QDir::Files, QDir::Name)) {
                const QString name = file.left(file.size() - m_config.extension.size());
                if (listed.contains(name))
                    continue; // the user's file shadows the preset
                const QString title =
                    ContentOps::title(ContentOps::read(presets.absoluteFilePath(file)));
                all.append(Info{name, title.isEmpty() ? name : title, true});
            }
        }
        std::sort(all.begin(), all.end(), [](const Info &a, const Info &b) {
            return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        });
        return all;
    }

    // The "modified" marker: the entry records an origin preset and
    // its content (its provenance aside) differs from that preset's.
    bool isModified(const QString &name) const
    {
        const QString origin = originOf(name);
        const QLatin1String prefix{"preset:"};
        if (!origin.startsWith(prefix))
            return false;
        const QString preset = presetPath(origin.mid(prefix.size()));
        if (preset.isEmpty())
            return false; // the origin preset is gone: nothing to compare against
        return !ContentOps::equivalent(
            ContentOps::read(preset),
            ContentOps::live(ContentOps::read(userPath(name))));
    }

    // The save: the live content into the current entry's file — a
    // plain write for a user file (the file's recorded origin carried
    // over), a fork for a preset current that the content has diverged
    // from, a recreation for a name whose file vanished. Returns the
    // (possibly new) current name; a no-op for an unset current and an
    // unedited preset.
    QString saveCurrent(const Value &live)
    {
        QString name = current();
        if (name.isEmpty())
            return name; // no current: there is no file to write to

        const QString user = userPath(name);
        if (!user.isEmpty()) {
            atomicWrite(user, ContentOps::write(live, originOf(name)),
                        "EntryStore::saveCurrent");
            return name;
        }

        if (isPreset(name)) {
            // A preset current: the read-only preset is never
            // rewritten. The copy-on-write: a diverged live content is
            // forked into the user's directory (the origin names the
            // preset) and the fork becomes current.
            if (ContentOps::equivalent(content(name), live))
                return name; // unedited: the read-only preset stays
            const QString forkFile = m_config.forkName(userDir(), name);
            if (!atomicWrite(userDir() + QLatin1Char('/') + forkFile,
                             ContentOps::write(live, QStringLiteral("preset:%1").arg(name)),
                             "EntryStore::saveCurrent"))
                return name; // a failed write leaves the preset current
            const QString fork = forkFile.left(forkFile.size() - m_config.extension.size());
            setCurrent(fork);
            return fork;
        }

        // No user file and no preset: a name whose file vanished behind
        // the app's back — recreated with the live content.
        atomicWrite(userDir() + QLatin1Char('/') + name + m_config.extension,
                    ContentOps::write(live, QString()), "EntryStore::saveCurrent");
        setCurrent(name);
        return name;
    }

    // The current, settled: a stale name (no file anywhere) falls back
    // to the first user file, or to no current ("" — the caller keeps
    // its live state as-is) when there are none.
    QString resolveCurrent()
    {
        QString name = current();
        if (name.isEmpty())
            return {};
        if (userPath(name).isEmpty() && presetPath(name).isEmpty()) {
            const QString first = firstUser();
            if (first.isEmpty()) {
                setCurrent(QString());
                return {};
            }
            setCurrent(first);
            name = first;
        }
        return name;
    }

    // The live content under `name` (an existing file is overwritten),
    // made current. The provenance of the current entry carries over:
    // a user file's recorded origin, a preset's name, nothing for the
    // rest. False for a bad name.
    bool saveAs(const QString &name, const Value &live)
    {
        const QString clean = names::sanitize(name);
        if (clean.isEmpty())
            return false;
        QString origin;
        const QString currentName = current();
        if (!currentName.isEmpty() && userPath(currentName) != QString())
            origin = originOf(currentName);
        else if (!currentName.isEmpty() && isPreset(currentName))
            origin = QStringLiteral("preset:%1").arg(currentName);
        atomicWrite(userDir() + QLatin1Char('/') + clean + m_config.extension,
                    ContentOps::write(live, origin), "EntryStore::saveAs");
        setCurrent(clean);
        return true;
    }

    // A fresh empty entry under `name` (an existing file is
    // overwritten), made current — the current is saved as-is first (a
    // no-op for an unset current), so the content the owner was working
    // on is never lost. `applyEmpty` applies the empty state to the
    // live UI. False for a bad name.
    bool create(const QString &name, const Value &live,
                const std::function<void()> &applyEmpty)
    {
        const QString clean = names::sanitize(name);
        if (clean.isEmpty())
            return false;
        saveCurrent(live);
        if (!atomicWrite(userDir() + QLatin1Char('/') + clean + m_config.extension,
                         ContentOps::write(ContentOps::empty(), QString()),
                         "EntryStore::create"))
            return false;
        setCurrent(clean);
        applyEmpty();
        return true;
    }

    // A user entry only (the presets are read-only). `*applyName` (when
    // the argument is not null) is the content the owner must apply
    // after the delete: the first remaining user file when the deleted
    // entry was the current, "" (the owner's empty state) when there
    // are none.
    bool remove(const QString &name, QString *applyName)
    {
        const QString path = userPath(name);
        if (path.isEmpty())
            return false;
        if (!QFile::remove(path))
            return false;
        if (applyName != nullptr && name == current()) {
            *applyName = firstUser();
            setCurrent(*applyName);
        }
        return true;
    }

    // The export: the live content under the picked file, the current
    // entry's provenance carried over.
    bool exportTo(const QString &path, const Value &live) const
    {
        return atomicWrite(path, ContentOps::write(live, originOf(current())),
                           "EntryStore::exportTo");
    }

private:
    QString filter() const { return QStringLiteral("*") + m_config.extension; }

    Config m_config;
};

#endif // ENTRystore_H
