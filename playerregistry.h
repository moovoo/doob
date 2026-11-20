#ifndef PLAYERREGISTRY_H
#define PLAYERREGISTRY_H

#include <QList>
#include <QPair>
#include <QString>

#include <functional>

class AudioPlayer;
class QObject;

// The registry of playback drivers (the core-side half of a driver
// plugin). A creator receives the parent the created player is given: a
// parented player is owned (and deleted) by the caller of create() (the
// PlaybackManager), an unparented one is owned by whoever registered it
// (a plugin) — the PlaybackManager deletes only what it created.
class PlayerRegistry
{
public:
    using Creator = std::function<AudioPlayer*(QObject *parent)>;

    static PlayerRegistry* getInstance();

    // Fails (false, a qWarning) when the id is already taken or the
    // creator is empty.
    bool registerPlayer(const QString &id, const QString &title, Creator creator);
    // Drops the registration (a plugin's shutdown() — the creator's
    // player objects are owned by whoever created them).
    void unregisterPlayer(const QString &id);

    // The preferred default driver: the one PlaybackManager resolves to
    // when the user has no explicit selection (an empty
    // "playback.player") — a plugin advertises its driver in
    // initialize() and clears it (an empty id) in shutdown(). A
    // non-empty id that is not registered is refused (false, a
    // qWarning); a preference left stale by an unregister is harmless —
    // create() answers nullptr for it and the resolution falls through
    // to the built-in default.
    bool setPreferredDefault(const QString &id);
    QString preferredDefault() const { return m_preferredDefault; }

    // (id, title) pairs, registration order.
    QList<QPair<QString, QString>> options() const;
    bool contains(const QString &id) const;
    // Creates the driver with the id, or nullptr when unknown.
    AudioPlayer* create(const QString &id, QObject *parent = nullptr) const;

    // Test seam: drops the registrations and the preferred default.
    void clear()
    {
        m_players.clear();
        m_preferredDefault.clear();
    }

private:
    PlayerRegistry() = default;

    struct Entry
    {
        QString id;
        QString title;
        Creator creator;
    };
    QList<Entry> m_players;
    // The preferred default driver's id ("" = none — the built-in
    // default applies).
    QString m_preferredDefault;
    inline static PlayerRegistry* m_instance;
};

#endif // PLAYERREGISTRY_H
