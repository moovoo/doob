#include "settingsmanager.h"
#include "jsonfile.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>

#include <algorithm>

SettingsManager* SettingsManager::getInstance()
{
    if (m_instance == nullptr)
        m_instance = new SettingsManager;
    return m_instance;
}

void SettingsManager::reset()
{
    delete m_instance;
    m_instance = nullptr;
}

SettingsManager::SettingsManager() = default;

void SettingsManager::setSettingsPath(const QString &path)
{
    m_settingsPath = path;
}

void SettingsManager::load()
{
    loadStore(m_settingsPath, m_settings);

    // A JSON round trip changes container types (an array comes back as a
    // QVariantList, an object as a QVariantMap): coerce the stored values
    // of the registered settings to the registered type, so value()
    // returns a stable type.
    for (const Setting &s : m_registry) {
        if (!m_settings.contains(s.key))
            continue;
        m_settings.insert(s.key, coerceToType(m_settings.value(s.key),
                                              s.defaultValue));
    }
}

bool SettingsManager::registerSetting(const Setting &s)
{
    if (!s.isValid()) {
        qWarning("SettingsManager::registerSetting: empty key");
        return false;
    }
    if (setting(s.key).isValid()) {
        qWarning("SettingsManager::registerSetting: key %s is already "
                 "registered", qPrintable(s.key));
        return false;
    }
    m_registry.append(s);
    return true;
}

void SettingsManager::unregisterSetting(const QString &key)
{
    for (int i = 0; i < m_registry.size(); ++i) {
        if (m_registry.at(i).key == key) {
            m_registry.removeAt(i);
            return;
        }
    }
}

Setting SettingsManager::setting(const QString &key) const
{
    for (const Setting &s : m_registry)
        if (s.key == key)
            return s;
    return {};
}

QList<Setting> SettingsManager::settings() const
{
    return m_registry;
}

QVariant SettingsManager::value(const QString &key) const
{
    if (m_settings.contains(key))
        return m_settings.value(key);
    const Setting s = setting(key);
    return s.isValid() ? s.defaultValue : QVariant();
}

QVariant SettingsManager::defaultValue(const QString &key) const
{
    const Setting s = setting(key);
    return s.isValid() ? s.defaultValue : QVariant();
}

bool SettingsManager::isModified(const QString &key) const
{
    return m_settings.contains(key);
}

void SettingsManager::setValue(const QString &key, const QVariant &value)
{
    const Setting s = setting(key);
    if (s.isValid() && value.userType() != s.defaultValue.userType()) {
        qWarning("SettingsManager::setValue: %s is a %s, not a %s",
                 qPrintable(key),
                 qPrintable(QMetaType(value.userType()).name()),
                 qPrintable(QMetaType(s.defaultValue.userType()).name()));
        return;
    }
    if (m_settings.value(key) == value)
        return; // unchanged: nothing to write, no signal
    m_settings.insert(key, value);
    saveStore(m_settingsPath, m_settings);
    emit changed(key, value);
}

void SettingsManager::resetToDefault(const QString &key)
{
    if (m_settings.remove(key) == 0)
        return; // not stored: the default already applies
    saveStore(m_settingsPath, m_settings);
    emit changed(key, defaultValue(key));
}

QStringList SettingsManager::storedKeys() const
{
    QStringList keys = m_settings.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool SettingsManager::loadStore(const QString &path, QVariantMap &store)
{
    store.clear();
    if (path.isEmpty())
        return true; // memory-only (a test that set no path)

    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return true; // nothing saved yet: keep the (empty) state

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        qWarning("SettingsManager: %s is not a JSON object", qPrintable(path));
        return false;
    }
    store = doc.object().toVariantMap();
    return true;
}

bool SettingsManager::saveStore(const QString &path, const QVariantMap &store)
{
    if (path.isEmpty())
        return true; // memory-only (a test that set no path)

    const QByteArray data =
        QJsonDocument::fromVariant(store).toJson(QJsonDocument::Indented);
    return atomicWrite(path, data, "SettingsManager::saveStore");
}

QVariant SettingsManager::coerceToType(const QVariant &stored,
                                      const QVariant &type)
{
    if (stored.userType() == type.userType())
        return stored;
    QVariant coerced = stored;
    return coerced.convert(QMetaType(type.userType())) ? coerced : stored;
}
