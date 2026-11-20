#ifndef SETTINGREGISTRY_H
#define SETTINGREGISTRY_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

// One settings topic of the preferences dialog: the page the dialog shows
// when the topic is selected. owner is empty for the core's topics (the
// "Main" group) and the id of the plugin that registered the topic (the
// "Plugins" group).
struct SettingsTopic
{
    QString id;
    QString title;
    QString owner;
    std::function<QWidget *()> creator;
};

// The registry of settings topics (the core-side half of a plugin's
// settings): id -> (title, owner, page creator). The core's topics
// (owner empty) register from the PreferencesDialog's constructor; a
// plugin registers its own in initialize() (and unregisters them in
// shutdown()). The PreferencesDialog is a thin lookup into this registry,
// so a plugin's topics show up in the dialog for free.
class SettingsRegistry
{
public:
    using PageCreator = std::function<QWidget *()>;

    static SettingsRegistry* getInstance();

    // Fails (false, a qWarning) when the id is already taken or the
    // creator is empty.
    bool registerTopic(const QString &id, const QString &title,
                       const QString &owner, PageCreator creator);
    void unregisterTopic(const QString &id);
    bool contains(const QString &id) const;

    // A plugin that exports no topics declares itself (optionally with a
    // display title), so the preferences dialog can still list it (with a
    // "no settings" note in place of a page).
    void registerOwner(const QString &owner, const QString &title = QString());
    void unregisterOwner(const QString &owner);
    // The display title of a declared owner (the owner id when none given).
    QString ownerTitle(const QString &owner) const;

    // All topics, registration order.
    QList<SettingsTopic> topics() const;
    // The topics of one owner (a plugin id), registration order.
    QList<SettingsTopic> topicsForOwner(const QString &owner) const;
    // The non-empty owners (declared owners plus the owners of the
    // registered topics), sorted.
    QStringList owners() const;

    // Test seam: drops the registrations.
    void clear();

private:
    SettingsRegistry() = default;

    QList<SettingsTopic> m_topics;    // registration order
    QHash<QString, QString> m_owners; // declared owners -> display title
    inline static SettingsRegistry* m_instance;
};

#endif // SETTINGREGISTRY_H
