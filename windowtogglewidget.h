#ifndef WINDOWTOGGLEWIDGET_H
#define WINDOWTOGGLEWIDGET_H

#include "basewidget.h"

class QPushButton;

// A single toggle button that controls the visibility of one of the
// layout's windows (the WindowManager's). The controlled window's id
// and the button's caption are the widget's own state; the checked
// state follows the window's live visibility (a window hidden
// elsewhere — the windows editor, the window's own close button —
// unchecks the button, and vice versa). When no window is bound (or
// the bound window was deleted) the button shows "?" and a click opens
// the window picker. In edit mode the context menu offers "Edit
// caption..." and "Choose window...".
class WindowToggleWidget : public BaseWidget
{
    Q_OBJECT
public:
    inline static const QString id = "windowtoggle";
    inline static const QString title = "Window toggle";
    inline static const QString tag = widgetTags::layout;

    explicit WindowToggleWidget(QWidget *parent = nullptr);

    QString widgetId() const override { return id; }
    QString widgetTitle() const override { return title; }

    WidgetConfiguration storeWidget() override;
    void restoreWidget(WidgetConfiguration &config) override;

    // The controlled window's id ("" when none is bound) and its
    // caption (empty until the first (re)selection, which seeds it
    // with the window's title).
    QString windowId() const { return m_windowId; }
    QString caption() const { return m_caption; }
    // (Re)selects the controlled window; the caption is seeded with
    // the window's title.
    void setWindowId(const QString &id);
    void setCaption(const QString &caption);

    // The edit-mode context menu: the base's entries plus the caption
    // editor and the window picker.
    QMenu* widgetContextMenu() override;

protected:

    // Test seams (the modal dialogs are untestable offscreen).
    virtual void openWindowPicker(const QString &selectedId);
    virtual QString promptCaption(const QString &current);

private:
    void onButtonClicked();
    void updateButton();

    QPushButton* m_button;
    QString m_windowId;
    QString m_caption;
};

#endif // WINDOWTOGGLEWIDGET_H
