#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QList>
#include <QPoint>

#include "basewidget.h"
#include "styleproperty.h"

class QLabel;
class QPaintEvent;
class QMouseEvent;
class QPushButton;

// The frameless window's replacement title strip: the app's icon, the
// live title (the same "Title — Artist" the window title shows,
// watched through playingItemChanged + stateChanged like the other
// playback displays) and the optional min/max/close buttons (per-
// instance state, non-default only — the default is all three shown).
// A left press on the strip starts the system move, a double-click
// toggles the maximize (both behind virtual seams — offscreen a no-op,
// the tests count the calls); the buttons route to showMinimized / the
// maximize toggle / close() (the window's closeEvent applies its
// tray/exit decision). The strip's paint parameters are "x-" style
// properties (the height, the colors — the barpaint/oscillogram
// convention).
class TitleBarWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "titlebar";
    inline static const QString title = "Title bar";
    inline static const QString tag = widgetTags::layout;

    // The strip's default height (the "x-titlebar-height" fallback).
    static constexpr int kDefaultHeight = 32;

    // The "x-" style properties this widget reads (see StyleManager and
    // the class comment): the style sheet editor's help documents them
    // from here.
    static QList<StyleProperty> styleProperties();

    explicit TitleBarWidget(QWidget *parent = nullptr);
    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    // The min/max/close buttons' visibility (the default is all shown).
    bool showMinimizeButton() const { return m_showMin; }
    bool showMaximizeButton() const { return m_showMax; }
    bool showCloseButton() const { return m_showClose; }
    void setShowMinimizeButton(bool show);
    void setShowMaximizeButton(bool show);
    void setShowCloseButton(bool show);

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration &config) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    // The window operations (the test seams: offscreen the QWindow calls
    // are no-ops — the tests count the calls; the top-level is the
    // widget's window(), so the widget works in the main window and in a
    // child standalone window alike).
    virtual void startSystemMove();
    virtual void minimizeWindow();
    virtual void toggleMaximize();
    virtual void closeWindow();

private:
    // The live title (the window title's "Title — Artist" computation).
    void updateTitle();
    void updateControls();

    QLabel* m_icon;
    QLabel* m_titleLabel;
    QPushButton* m_minButton;
    QPushButton* m_maxButton;
    QPushButton* m_closeButton;
    bool m_showMin = true;
    bool m_showMax = true;
    bool m_showClose = true;
};

#endif // TITLEBAR_H
