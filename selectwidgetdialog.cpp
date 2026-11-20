#include "selectwidgetdialog.h"

#include <QFont>
#include <QGridLayout>
#include <QPushButton>
#include <QSet>

namespace {
// The group heading of a tag: the tag with a capital first letter
// ("layout" -> "Layout").
QString heading(const QString &tag)
{
    QString out = tag;
    if (!out.isEmpty())
        out[0] = out.at(0).toUpper();
    return out;
}

// The groups' display order: the core's groups (the widgetTags' order),
// then the unknown ones (a plugin's own group) alphabetically, and
// "other" last. A group with no widget is not listed (it would jump
// around as widgets are added and removed).
QStringList groupOrder(const QList<WidgetRegistry::WidgetOption> &widgets)
{
    QStringList out = { widgetTags::layout, widgetTags::playback,
                        widgetTags::visualization, widgetTags::library, };
    QSet<QString> listed;
    for (const QString &tag : out)
        listed.insert(tag);
    QStringList unknown;
    for (const WidgetRegistry::WidgetOption &widget : widgets) {
        const QString tag =
            widget.tag.isEmpty() ? widgetTags::other : widget.tag;
        if (tag != widgetTags::other && !listed.contains(tag)) {
            listed.insert(tag);
            unknown.append(tag);
        }
    }
    unknown.sort();
    out += unknown;
    out.append(widgetTags::other);
    return out;
}
}

SelectWidgetDialog::SelectWidgetDialog(const QList<WidgetRegistry::WidgetOption> &widgets, const QString& selected, QWidget *parent): QDialog(parent)
{
    setWindowTitle("Select widget");

    QGridLayout *layout = new QGridLayout(this);
    setLayout(layout);

    m_list = new QListWidget(this);
    m_list->setObjectName("list");
    layout->addWidget(m_list, 1, 1, 3, 4);
    QListWidgetItem *preselected = nullptr;
    for (const QString &tag : groupOrder(widgets))
    {
        // The group's rows (an empty tag is the "other" fallback); the
        // group is skipped without a row, so the headings never show
        // an empty group.
        QList<WidgetRegistry::WidgetOption> group;
        for (const WidgetRegistry::WidgetOption &w : widgets)
            if ((w.tag.isEmpty() ? widgetTags::other : w.tag) == tag)
                group.append(w);
        if (group.isEmpty())
            continue;

        // The heading row: a group marker, not a widget — never
        // selectable (the current item is always a widget row, so
        // value() never reads a heading).
        QListWidgetItem *header = new QListWidgetItem(heading(tag), m_list);
        QFont font = header->font();
        font.setBold(true);
        font.setUnderline(true);
        header->setFont(font);
        header->setFlags(Qt::NoItemFlags);

        for (const WidgetRegistry::WidgetOption &w : group)
        {
            QListWidgetItem *item = new QListWidgetItem( w.title );
            item->setData(Qt::UserRole, w.id);
            m_list->addItem(item);
            if (w.id == selected)
            {
                preselected = item;
            }
        }
    }
    // The current item: the pre-selected widget, else the first widget
    // row (not a heading — the first row may be one). value() reads the
    // current item, so "Select" without touching the list confirms the
    // pre-selection (or the first widget) instead of a heading.
    if (preselected != nullptr) {
        m_list->setCurrentItem(preselected);
    } else {
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *item = m_list->item(i);
            if (item->flags().testFlag(Qt::ItemIsSelectable)) {
                m_list->setCurrentItem(item);
                break;
            }
        }
    }
    connect(m_list, &QListWidget::doubleClicked, this, &SelectWidgetDialog::onListDblClick);

    QPushButton *cancel = new QPushButton("Cancel", this);
    cancel->setObjectName("cancelButton");
    layout->addWidget(cancel, 4, 3, 1, 2);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    QPushButton *ok = new QPushButton("Select", this);
    ok->setObjectName("okButton");
    ok->setDefault(true);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(ok, 4, 1, 1, 2);

}

void SelectWidgetDialog::onListDblClick(QModelIndex index)
{
    // A double-click on a group heading is a no-op: only a widget row
    // (a selectable item) accepts.
    if (!index.isValid())
        return;
    const QListWidgetItem *item = m_list->itemFromIndex(index);
    if (item != nullptr && item->flags().testFlag(Qt::ItemIsSelectable))
        accept();
}

QString SelectWidgetDialog::value() const
{
    return m_list->currentItem()->data(Qt::UserRole).toString();
}
