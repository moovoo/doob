#include "uistate.h"
#include "settingsmanager.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QScreen>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace {
// The standard fields (see the class comment for their shapes).
const char* const kGeometry = "geometry";
const char* const kColumns = "columns";
}

UiState::UiState(QString key, QObject *parent)
    : QObject{parent}
    , m_key{std::move(key)}
{
    // The key is registered on demand (idempotent — a second UiState of
    // the same scope just shares the registration, so no warning): the
    // default is an empty map, so the setting's type is "a map of
    // fields" and a stale file is read as-is.
    SettingsManager* settings = SettingsManager::getInstance();
    if (!settings->setting(m_key).isValid())
        settings->registerSetting(
            { m_key,
              m_key,
              QStringLiteral("The persisted UI state (the window's "
                             "geometry, the table column widths, ...) of "
                             "the widget that registered the key — "
                             "written by the app on hide/close."),
              QVariantMap(),
              QString(), });
}

QVariantMap UiState::load() const
{
    return SettingsManager::getInstance()->value(m_key).toMap();
}

void UiState::save(const QVariantMap &state) const
{
    SettingsManager::getInstance()->setValue(m_key, state);
}

QVariant UiState::field(const QString &name) const
{
    return load().value(name);
}

void UiState::set(const QString &name, const QVariant &value) const
{
    QVariantMap state = load();
    state.insert(name, value);
    save(state);
}

bool UiState::onAnyScreen(const QRect &geo)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    return std::any_of(screens.constBegin(), screens.constEnd(),
                       [&geo](QScreen *screen) {
                           return screen->geometry().intersects(geo);
                       });
}

bool UiState::fitsSomeScreen(const QSize &size)
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    return std::any_of(screens.constBegin(), screens.constEnd(),
                       [&size](QScreen *screen) {
                           return screen->geometry().width() >= size.width()
                              && screen->geometry().height() >= size.height();
                       });
}

QRect UiState::restoredGeometry(const QVariantMap &geo)
{
    const int width = geo.value("width").toInt();
    const int height = geo.value("height").toInt();
    if (width <= 0 || height <= 0)
        return {}; // a missing or degenerate size keeps the default
    if (!fitsSomeScreen(QSize(width, height)))
        return {}; // a size no screen can hold keeps the default
    QRect r(geo.value("x").toInt(), geo.value("y").toInt(), width, height);
    if (!onAnyScreen(r) && QGuiApplication::primaryScreen() != nullptr)
        r.moveCenter(QGuiApplication::primaryScreen()->geometry().center());
    return r;
}

void UiState::saveGeometry(const QWidget *window) const
{
    // The normal geometry (a maximized window stores its un-maximized
    // rect) plus the flag — the layout document's "window" shape.
    const QRect r = window->normalGeometry();
    QVariantMap geo;
    geo.insert("x", r.x());
    geo.insert("y", r.y());
    geo.insert("width", r.width());
    geo.insert("height", r.height());
    geo.insert("maximized",
               static_cast<bool>(window->windowState() & Qt::WindowMaximized));
    QVariantMap state = load();
    state.insert(kGeometry, geo);
    save(state);
}

void UiState::restoreGeometry(QWidget *window)
{
    const QVariantMap geo = load().value(kGeometry).toMap();
    const QRect r = restoredGeometry(geo);
    if (!r.isValid())
        return; // a missing, degenerate or oversized size keeps the default
    window->setGeometry(r);
    // The flag is applied on the first show (track), a maximized window
    // cannot be maximized before it is shown.
    m_pendingMaximized = geo.value("maximized", false).toBool();
}

void UiState::saveColumns(QHeaderView *header, const QStringList &names)
    const
{
    QVariantMap columns = load().value(kColumns).toMap();
    const int count = std::min<int>(static_cast<int>(names.size()),
                                    header->count());
    for (int i = 0; i < count; ++i)
        columns.insert(names.at(i), header->sectionSize(i));
    QVariantMap state = load();
    state.insert(kColumns, columns);
    save(state);
}

void UiState::restoreColumns(QHeaderView *header,
                             const QStringList &names) const
{
    const QVariantMap columns = load().value(kColumns).toMap();
    if (columns.isEmpty())
        return;
    const int count = std::min<int>(static_cast<int>(names.size()),
                                    header->count());
    for (int i = 0; i < count; ++i) {
        const int width = columns.value(names.at(i)).toInt();
        if (width > 0)
            header->resizeSection(i, width);
    }
}

void UiState::track(QWidget *window)
{
    m_window = window;
    window->installEventFilter(this);
}

bool UiState::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window) {
        switch (event->type()) {
        case QEvent::Hide:
            // A close is a hide: the last session's state is what a
            // fresh instance restores (and an unchanged save is a
            // no-op — no write, no signal).
            saveGeometry(m_window);
            break;
        case QEvent::Show:
            if (m_pendingMaximized) {
                m_pendingMaximized = false;
                m_window->showMaximized();
            }
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}
