#ifndef PLAYBACKCONTROL_H
#define PLAYBACKCONTROL_H

#include <QList>
#include <QPair>
#include <QStringList>

#include "basewidget.h"
#include "playbackmanager.h"

class QPushButton;
class QMenu;
class PlaylistManager;

class PlaybackControlWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "controls";
    inline static const QString title = "Playback";
    inline static const QString tag = widgetTags::playback;

    explicit PlaybackControlWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    QMenu* widgetContextMenu() override;
    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration& config) override;

    enum class Orientation { Horizontal, Vertical };

    // Every control button as a (display label, config key) pair, in the
    // default order; offered by the "Buttons..." edit-mode dialog.
    QList<QPair<QString, QString>> buttonCatalog() const;
    // The display sequence: the visible button keys and the stretchers
    // ("stretch"), in display order.
    QStringList sequence() const;

    // The buttons' size policy: an on axis stretches the buttons across
    // it (Expanding plus an explicit policy stretch, so they share the
    // surplus with any stretchers), an off axis keeps their natural size
    // (Fixed — a QBoxLayout also hands the surplus to Preferred items,
    // so Preferred would stretch like Expanding). Both are on by default;
    // only the non-default states persist.
    bool stretchHorizontal() const { return m_stretchHorizontal; }
    bool stretchVertical() const { return m_stretchVertical; }
    void setStretchHorizontal(bool on);
    void setStretchVertical(bool on);

    // The row/column orientation (default horizontal; only the
    // non-default orientation persists).
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation orientation);

private slots:
    void onPlayPauseClicked();
    void onNextClicked();
    void onPreviousClicked();
    void onStopClicked();
    void onShuffleToggled(bool checked);
    void onRepeatClicked();
    void onStateChanged(PlaybackManager::State state);
    void onPlaylistChanged();
    void onShuffleChanged(bool shuffle);
    void onRepeatChanged(PlaybackManager::RepeatMode mode);
    void onEditButtons();

private:
    struct ControlButton {
        QString key;
        QPushButton* button;
    };
    struct SequenceItem {
        QString key; // the button's key, or the stretcher's
        QPushButton* button; // the button, nullptr for a stretcher
    };

    QPushButton* addButton(const QString& key, const QString& objectName);
    void updateButtons();
    void updateShuffleButton();
    void updateRepeatButton();
    void rebuildLayout();
    void applySequence(const QStringList& keys);
    void applyStretchPolicies();
    QPushButton* buttonFor(const QString& key) const;

    PlaybackManager* m_pm;
    PlaylistManager* m_pl;
    QPushButton* m_previous;
    QPushButton* m_playPause;
    QPushButton* m_stop;
    QPushButton* m_next;
    QPushButton* m_shuffle;
    QPushButton* m_repeat;
    QList<ControlButton> m_buttons; // all buttons, default order
    QList<SequenceItem> m_sequence; // the display order (buttons and stretchers)
    bool m_stretchHorizontal = true;
    bool m_stretchVertical = true;
    Orientation m_orientation = Orientation::Horizontal;
    // True while the constructor reflects the manager's state into the
    // shuffle button: its toggled must not feed back into the manager.
    bool m_syncing = false;
};

#endif // PLAYBACKCONTROL_H
