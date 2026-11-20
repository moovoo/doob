#include "dspregistry.h"

#include <QDebug>
#include <QVariantMap>

#include <algorithm>

#include "audioprocessor.h"
#include "settingsmanager.h"
#include "silenceskip.h"

namespace
{
// The chain's user state (the core's settings, written through like
// any setting): the order is the ids top to bottom (the absent ids
// run last, registration order), the map holds the disabled entries
// (the absent ones are on). The built-in's parameters ride along, so
// every dsp.* key is registered in one place (the registry's ctor).
const QString OrderKey = QStringLiteral("dsp.order");
const QString EnabledKey = QStringLiteral("dsp.enabled");
const QString SilenceThresholdKey = QStringLiteral("dsp.silence.threshold_db");
const QString SilenceMaxKey = QStringLiteral("dsp.silence.max_seconds");
}

DspRegistry* DspRegistry::getInstance(QObject *parent)
{
    if (m_instance == nullptr)
        m_instance = new DspRegistry(parent);
    return m_instance;
}

void DspRegistry::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

DspRegistry::DspRegistry(QObject *parent)
    : QObject(parent)
{
    SettingsManager* settings = SettingsManager::getInstance();
    settings->registerSetting(
        { OrderKey, QStringLiteral("DSP order"),
          QStringLiteral("The chain's order (the processor ids, top to "
                         "bottom); the absent ids run last, in "
                         "registration order."),
          QStringList(), QString(), });
    settings->registerSetting(
        { EnabledKey, QStringLiteral("DSP enabled"),
          QStringLiteral("The chain's switches (id to on/off); the "
                         "absent ids are on."),
          QVariantMap(), QString(), });
    settings->registerSetting(
        { SilenceThresholdKey,
          QStringLiteral("Silence threshold (dBFS)"),
          QStringLiteral("The leading-skip drops a chunk while its peak "
                         "stays under this level (the default -40 "
                         "dBFS)."),
          -40.0, QString(), });
    settings->registerSetting(
        { SilenceMaxKey, QStringLiteral("Max silence to skip (s)"),
          QStringLiteral("The leading-skip gives up after this much "
                         "dropped silence (a silent track starts "
                         "anyway)."),
          60.0, QString(), });
    // The chain state edited outside this registry (the config
    // browser) still changes the chain: the running chains and the
    // DSP preferences page refresh through the SettingsManager's signal.
    connect(settings, &SettingsManager::changed, this,
            [this](const QString &key, const QVariant &) {
        if (key == OrderKey || key == EnabledKey)
            emit chainChanged();
    });
    registerBuiltins();
}

void DspRegistry::registerBuiltins()
{
    // The core's built-in, parented to the registry (it lives for the
    // whole session, like the registry itself). The analyzer does not
    // see Qt's parent-child ownership, so it reports a leak: the parent
    // owns the processor, so there is none.
    registerDsp(new SilenceSkipProcessor(this), QString());
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

bool DspRegistry::registerDsp(AudioProcessor *processor, const QString &owner,
                              PageCreator &&page)
{
    if (processor == nullptr) {
        qWarning("DspRegistry: registerDsp with a null processor");
        return false;
    }
    const QString id = processor->id();
    if (contains(id)) {
        qWarning("DspRegistry: the DSP id %s is already taken",
                 qPrintable(id));
        return false;
    }
    m_entries.append(Entry{ id, processor->title(), owner, processor,
                            std::move(page), });
    emit chainChanged();
    return true;
}

void DspRegistry::unregisterDsp(const QString &id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            m_entries.removeAt(i);
            emit chainChanged();
            return;
        }
    }
}

bool DspRegistry::contains(const QString &id) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [&id](const Entry &entry) { return entry.id == id; });
}

QList<QPair<QString, QString>> DspRegistry::options() const
{
    QList<QPair<QString, QString>> out;
    for (const QString &id : orderedIds())
        for (const Entry &entry : m_entries)
            if (entry.id == id)
                out.append(qMakePair(entry.id, entry.title));
    return out;
}

QString DspRegistry::owner(const QString &id) const
{
    for (const Entry &entry : m_entries)
        if (entry.id == id)
            return entry.owner;
    return {};
}

DspRegistry::PageCreator DspRegistry::page(const QString &id) const
{
    for (const Entry &entry : m_entries)
        if (entry.id == id)
            return entry.page;
    return {};
}

bool DspRegistry::isEnabled(const QString &id) const
{
    const QVariantMap stored =
        SettingsManager::getInstance()->value(EnabledKey).toMap();
    return stored.value(id, true).toBool();
}

void DspRegistry::setEnabled(const QString &id, bool enabled) const
{
    if (!contains(id))
        return;
    QVariantMap stored =
        SettingsManager::getInstance()->value(EnabledKey).toMap();
    stored.insert(id, enabled);
    // The write-through emits the SettingsManager's changed() (only when the
    // value moves), which is the single chainChanged() source for the
    // state edits (the register/unregister above are the only ones that
    // emit directly — they change no stored state).
    SettingsManager::getInstance()->setValue(EnabledKey, stored);
}

void DspRegistry::moveUp(const QString &id) const
{
    QStringList order = orderedIds();
    const qsizetype i = order.indexOf(id);
    if (i <= 0)
        return;
    order.swapItemsAt(i, i - 1);
    SettingsManager::getInstance()->setValue(OrderKey, order);
}

void DspRegistry::moveDown(const QString &id) const
{
    QStringList order = orderedIds();
    const qsizetype i = order.indexOf(id);
    if (i < 0 || i >= order.size() - 1)
        return;
    order.swapItemsAt(i, i + 1);
    SettingsManager::getInstance()->setValue(OrderKey, order);
}

QList<AudioProcessor*> DspRegistry::chain() const
{
    QList<AudioProcessor*> out;
    for (const QString &id : orderedIds())
        if (isEnabled(id))
            for (const Entry &entry : m_entries)
                if (entry.id == id)
                    out.append(entry.processor);
    return out;
}

QStringList DspRegistry::orderedIds() const
{
    // The user's stored order, filtered to the registered ids, then
    // the registrations not in it (registration order).
    QStringList stored =
        SettingsManager::getInstance()->value(OrderKey).toStringList();
    QStringList out;
    for (const QString &id : stored)
        if (contains(id))
            out.append(id);
    for (const Entry &entry : m_entries)
        if (!out.contains(entry.id))
            out.append(entry.id);
    return out;
}

void DspRegistry::clear()
{
    m_entries.clear();
}
