#ifndef ARTSOURCE_H
#define ARTSOURCE_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

#include <functional>

#include "mediaitem.h"

// The kind of enrichment an ArtSource serves: Art now, Lyrics
// reserved — the fetch is keyed on the kind, so a later kind (and a
// source for it) fits the interface without an ABI break.
enum class ArtKind
{
    Art,
    Lyrics
};

// An enrichment source: the lookup of an item's enrichment, keyed on
// the item's display identity (artist/title/album) and independent of
// the item's source. The registration is non-owning: the source is
// parented to whoever registered it (a plugin) and dies with it; the
// registry keeps only the pointer. Everything runs on the GUI thread.
class ArtSource : public QObject
{
    Q_OBJECT
public:
    // The result: the mime type and the bytes (an empty mime and
    // data = none found). cb is invoked exactly once, on the GUI
    // thread.
    using Callback =
        std::function<void(const QString &mime, const QByteArray &data)>;

    explicit ArtSource(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~ArtSource() override = default;

    // The identity among the registered sources (the value the
    // art.fallback setting names) and the display title.
    virtual QString id() const = 0;
    virtual QString title() const = 0;

    // Looks up the item's kind enrichment; cb is invoked exactly once,
    // on the GUI thread (an empty result = none found). A source that
    // does not serve the kind answers empty.
    virtual void fetch(const MediaItem &item, ArtKind kind,
                       const Callback &cb) = 0;
};

// The registry of enrichment sources — the extension point a plugin
// registers its sources into (initialize(): registerSource,
// shutdown(): unregisterSource) and the core registers its built-ins
// from. Non-owning like the DspRegistry: a source is parented to its
// registrant and dies with it. Everything is called on the GUI thread
// (registration, lookups), so the registry keeps no locks of its own.
class ArtRegistry : public QObject
{
    Q_OBJECT
public:
    static ArtRegistry* getInstance(QObject *parent = nullptr);
    // Test seam: drop the instance (and the built-ins it parented) so
    // the next getInstance() re-runs the ctor.
    static void reset();

    // Registers the source (the id and title are the source's own).
    // owner is empty for the core's built-ins and the plugin's id
    // otherwise. Fails (false, a qWarning) when the id is already
    // taken or the source is null.
    bool registerSource(ArtSource *source, const QString &owner);
    // Drops the registration (a plugin's shutdown() — the source
    // itself is owned by whoever parented it).
    void unregisterSource(const QString &id);
    bool contains(const QString &id) const;

    // The source under an id (nullptr when unregistered).
    ArtSource* source(const QString &id) const;
    // The entries (id, title), in registration order.
    QList<QPair<QString, QString>> options() const;
    // The owner of an entry (empty for the core's built-ins).
    QString owner(const QString &id) const;

    // Test seam: drops the registrations (the built-ins among them).
    void clear();

signals:
    // The sources changed (a registration, an unregistration).
    void sourcesChanged();

private:
    explicit ArtRegistry(QObject *parent = nullptr);
    void registerBuiltins();

    struct Entry
    {
        ArtSource *source = nullptr;
        QString owner;
    };
    QList<Entry> m_entries; // registration order
    inline static ArtRegistry* m_instance;
};

#endif // ARTSOURCE_H
