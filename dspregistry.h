#ifndef DSPREGISTRY_H
#define DSPREGISTRY_H

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>

class AudioProcessor;
class QWidget;

// The registry of DSPs (audio processors) — the extension point a
// plugin registers its processors into (initialize(): registerDsp,
// shutdown(): unregisterDsp) and the core registers its built-ins
// from. It owns the user's chain state too: which processors are
// enabled and in which order they run — the DSP preferences page
// edits it, and it is written through to the dsp.enabled / dsp.order
// settings, so it survives the app and the removal of a plugin (an
// orphaned entry is ignored until the processor is registered again).
//
// The registration is non-owning: the processor is parented to
// whoever registered it (the plugin) and dies with it. Everything is
// called on the GUI thread (registration, the page, the chains'
// refresh), so the registry keeps no locks of its own.
class DspRegistry : public QObject
{
    Q_OBJECT
public:
    // The optional per-DSP settings page (the page the DSP preferences
    // topic's Configure button shows, in a dialog of its own).
    using PageCreator = std::function<QWidget *()>;

    static DspRegistry* getInstance(QObject *parent = nullptr);
    // Test seam: drop the instance (and the built-ins it parented) so
    // the next getInstance() re-runs the ctor against the current
    // SettingsManager.
    static void reset();

    // Registers the processor (the id and title are the processor's
    // own). owner is empty for the core's built-ins and the plugin's
    // id otherwise. Fails (false, a qWarning) when the id is already
    // taken or the processor is null.
    bool registerDsp(AudioProcessor *processor, const QString &owner,
                     PageCreator &&page = {});
    // Drops the registration (a plugin's shutdown() — the processor
    // itself is owned by whoever parented it).
    void unregisterDsp(const QString &id);
    bool contains(const QString &id) const;

    // The entries (id, title), in the effective order: the user's
    // stored order, then the registrations not in it (registration
    // order).
    QList<QPair<QString, QString>> options() const;
    // The owner of an entry (empty for the core's built-ins).
    QString owner(const QString &id) const;
    // The page creator of an entry (empty when the DSP has none).
    PageCreator page(const QString &id) const;

    // A newly registered processor is enabled; the state is written
    // through to dsp.enabled (a map of id to on/off, the absent
    // entries on).
    bool isEnabled(const QString &id) const;
    void setEnabled(const QString &id, bool enabled) const;
    // Moves the entry one slot toward the top / the bottom of the
    // chain (written through to dsp.order, the effective order of
    // every registered id). A no-op at the ends.
    void moveUp(const QString &id) const;
    void moveDown(const QString &id) const;

    // The processors a chain runs: the enabled ones, effective order.
    QList<AudioProcessor*> chain() const;

    // Test seam: drops the registrations (the built-ins among them).
    void clear();

signals:
    // The chain changed (a registration, an enable/disable, a move):
    // the running chains and the DSP preferences page refresh.
    void chainChanged();

private:
    explicit DspRegistry(QObject *parent = nullptr);
    void registerBuiltins();
    // The effective order of the registered ids.
    QStringList orderedIds() const;

    struct Entry
    {
        QString id;
        QString title;
        QString owner;
        AudioProcessor *processor = nullptr;
        PageCreator page;
    };
    QList<Entry> m_entries; // registration order
    inline static DspRegistry* m_instance;
};

#endif // DSPREGISTRY_H
