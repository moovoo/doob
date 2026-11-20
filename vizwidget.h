#ifndef VIZWIDGET_H
#define VIZWIDGET_H

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QTimer>

#include <functional>
#include <initializer_list>

#include "playbackmanager.h"

// The audio visualizations' shared scaffolding (the oscillogram's, the
// peak meter's, the spectrum's, the spectrogram's): the edit-mode
// menu's mechanics (the base menu's fallback and the exclusive option
// groups) and the playback state's timer half (the paint/decay clock).
// The widgets stay individual classes (their taps, their state and
// their rendering differ); this is what they all do the same way.
namespace viz {

// The edit-mode menu the widget's own actions are added to: the base
// class' menu (the "Style this widget..." entry and the toolbar's
// entries, when a toolbar exists) or a fresh one.
inline QMenu *editMenu(QMenu *base, QWidget *owner, const QString &title)
{
    if (base == nullptr)
        return new QMenu(title, owner);
    return base;
}

// One option group of the edit menu: an exclusive checkable group, one
// action per `options` entry (its `label`, checked on `current`), each
// triggering `apply` with its value — rebuilt from the current state on
// every menu, like the other option menus (TabPosition::positionMenu
// does the same). `T` is the option's type (passed explicitly — the
// label and apply are callables, not `T`s).
template <typename T, typename Container>
void optionGroup(QObject *owner, QMenu *menu, const Container &options,
                 const std::function<QString(const T &)> &label,
                 const T &current,
                 const std::function<void(const T &)> &apply)
{
    QActionGroup *group = new QActionGroup(menu);
    group->setExclusive(true);
    for (const T &option : options) {
        QAction *action = menu->addAction(label(option));
        action->setCheckable(true);
        action->setChecked(option == current);
        action->setActionGroup(group);
        QObject::connect(action, &QAction::triggered, owner,
                         [owner, option, apply] { apply(option); });
    }
}

// The braced-list form of the options (a braced-init-list deduces an
// initializer_list only; the Container form takes a std::array like
// kBandOptions).
template <typename T>
void optionGroup(QObject *owner, QMenu *menu, std::initializer_list<T> options,
                 const std::function<QString(const T &)> &label,
                 const T &current,
                 const std::function<void(const T &)> &apply)
{
    optionGroup<T, std::initializer_list<T>>(owner, menu, options, label,
                                             current, apply);
}

// The playback state's timer half (the visualizations' paint/decay
// clock): while Playing the timer runs, a pause or stop stops it (the
// caller then clears its state). True while playing.
inline bool runningFor(PlaybackManager::State state, QTimer *timer)
{
    if (state == PlaybackManager::Playing) {
        timer->start();
        return true;
    }
    timer->stop();
    return false;
}

} // namespace viz

#endif // VIZWIDGET_H
