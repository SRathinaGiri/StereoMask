#include "quickaccess.h"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QIcon arrowIcon(QWidget *widget,QStyle::StandardPixmap symbol)
{
    auto pixels=widget->style()->standardIcon(symbol).pixmap(18,18);
    QPainter painter(&pixels);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixels.rect(),widget->palette().color(QPalette::ButtonText));
    return QIcon(pixels);
}
}

QuickAccess::QuickAccess(QSettings *settings, QWidget *parent)
    : QWidget(parent), m_settings(settings)
{
    setObjectName("quickAccessPanel");
    auto layout=new QVBoxLayout(this);
    layout->setContentsMargins(2,4,2,4);
    auto tools=new QToolBar(this);
    tools->setIconSize(QSize(18,18));
    auto edit=tools->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),tr("Manage Quick Access"));
    edit->setObjectName("manageQuickAccessAction");
    edit->setToolTip(tr("Add, remove and reorder Quick Access commands"));
    connect(edit,&QAction::triggered,this,&QuickAccess::manage);
    layout->addWidget(tools);
    auto scroll=new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto page=new QWidget(scroll);
    m_buttons=new QVBoxLayout(page);
    m_buttons->setContentsMargins(0,0,0,0);
    m_buttons->setSpacing(2);
    m_buttons->addStretch();
    scroll->setWidget(page);
    layout->addWidget(scroll);
}

const QuickAccess::Command *QuickAccess::command(const QString &id) const
{
    for(const auto &entry:m_commands)if(entry.id==id && entry.action)return &entry;
    return nullptr;
}

void QuickAccess::registerCommand(const QString &id, const QString &category, QAction *action)
{
    if(id.isEmpty() || !action || action->isSeparator() || command(id))return;
    m_commands.append({id,category,action});
}

void QuickAccess::attachButton(QToolButton *button, const QString &id)
{
    if(!button || !command(id))return;
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(button,&QWidget::customContextMenuRequested,this,[this,button,id](const QPoint &position) {
        QMenu menu(button);
        const bool added=m_ids.contains(id);
        auto toggle=menu.addAction(added ? tr("Remove from Quick Access") : tr("Add to Quick Access"));
        toggle->setObjectName("toggleQuickAccessCommand");
        connect(toggle,&QAction::triggered,this,[this,id,added]{if(added)removeCommand(id);else addCommand(id);});
        if(added) {
            auto up=menu.addAction(tr("Move up"));up->setEnabled(m_ids.indexOf(id)>0);
            auto down=menu.addAction(tr("Move down"));down->setEnabled(m_ids.indexOf(id)<m_ids.size()-1);
            connect(up,&QAction::triggered,this,[this,id]{moveCommand(id,-1);});
            connect(down,&QAction::triggered,this,[this,id]{moveCommand(id,1);});
        }
        menu.exec(button->mapToGlobal(position));
    });
}

void QuickAccess::restore()
{
    m_ids.clear();
    for(const auto &id:m_settings->value("quickAccess/commands").toStringList())
        if(command(id) && !m_ids.contains(id))m_ids.append(id);
    rebuild();
}

void QuickAccess::save()
{
    m_settings->setValue("quickAccess/commands",m_ids);
    m_settings->sync();
    rebuild();
}

void QuickAccess::addCommand(const QString &id)
{
    if(!command(id) || m_ids.contains(id))return;
    m_ids.append(id);save();
}

void QuickAccess::removeCommand(const QString &id)
{
    if(m_ids.removeAll(id))save();
}

void QuickAccess::moveCommand(const QString &id, int offset)
{
    const int from=m_ids.indexOf(id),to=from+offset;
    if(from<0 || to<0 || to>=m_ids.size())return;
    m_ids.move(from,to);save();
}

void QuickAccess::rebuild()
{
    while(auto item=m_buttons->takeAt(0)) {
        if(auto widget=item->widget()){widget->hide();widget->setObjectName(QString());widget->deleteLater();}
        delete item;
    }
    for(const auto &id:m_ids)if(const auto entry=command(id)) {
        auto button=new QToolButton(this);
        button->setObjectName("quickAccessCommand_"+id);
        button->setProperty("quickAccessCommandId",id);
        button->setDefaultAction(entry->action);
        button->setIconSize(QSize(20,20));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setMaximumWidth(126);
        const auto update=[button] {
            const auto action=button->defaultAction();
            button->setText(button->fontMetrics().elidedText(action->text().remove('&'),Qt::ElideRight,
                112-(action->icon().isNull() ? 0 : 24)));
            button->setToolTip(action->toolTip().isEmpty() ? action->text() : action->toolTip());
        };
        connect(entry->action,&QAction::changed,button,update);
        update();
        if(entry->action->menu())button->setPopupMode(QToolButton::InstantPopup);
        attachButton(button,id);
        m_buttons->addWidget(button,0,Qt::AlignLeft);
    }
    m_buttons->addStretch();
}

void QuickAccess::manage()
{
    QDialog dialog(window());
    dialog.setObjectName("quickAccessManager");
    dialog.setWindowTitle(tr("Quick Access"));
    dialog.resize(700,480);
    auto layout=new QVBoxLayout(&dialog);
    auto search=new QLineEdit(&dialog);search->setObjectName("quickAccessSearch");
    search->setPlaceholderText(tr("Search commands"));layout->addWidget(search);
    auto columns=new QHBoxLayout;
    auto availableColumn=new QVBoxLayout;availableColumn->addWidget(new QLabel(tr("Available commands"),&dialog));
    auto available=new QListWidget(&dialog);available->setObjectName("quickAccessAvailable");
    availableColumn->addWidget(available);columns->addLayout(availableColumn,1);
    auto controls=new QVBoxLayout;
    auto add=new QToolButton(&dialog);add->setObjectName("quickAccessAdd");
    add->setIcon(arrowIcon(&dialog,QStyle::SP_ArrowRight));add->setToolTip(tr("Add selected command"));
    controls->addStretch();controls->addWidget(add);controls->addStretch();columns->addLayout(controls);
    auto chosenColumn=new QVBoxLayout;chosenColumn->addWidget(new QLabel(tr("Quick Access"),&dialog));
    auto chosen=new QListWidget(&dialog);chosen->setObjectName("quickAccessChosen");
    chosen->setDragDropMode(QAbstractItemView::InternalMove);
    chosen->setDefaultDropAction(Qt::MoveAction);chosenColumn->addWidget(chosen);
    auto tools=new QToolBar(&dialog);tools->setIconSize(QSize(18,18));
    auto up=tools->addAction(arrowIcon(&dialog,QStyle::SP_ArrowUp),tr("Move up"));up->setObjectName("quickAccessMoveUp");
    auto down=tools->addAction(arrowIcon(&dialog,QStyle::SP_ArrowDown),tr("Move down"));down->setObjectName("quickAccessMoveDown");
    auto remove=tools->addAction(style()->standardIcon(QStyle::SP_DialogDiscardButton),tr("Remove"));remove->setObjectName("quickAccessRemove");
    chosenColumn->addWidget(tools);columns->addLayout(chosenColumn,1);layout->addLayout(columns,1);
    auto append=[](QListWidget *list,const Command &entry) {
        auto item=new QListWidgetItem(entry.action->icon(),entry.category+" / "+entry.action->text().remove('&'),list);
        item->setData(Qt::UserRole,entry.id);item->setToolTip(entry.action->toolTip());
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    };
    for(const auto &entry:m_commands)if(entry.action)append(available,entry);
    available->sortItems();
    for(const auto &id:m_ids)if(auto entry=command(id))append(chosen,*entry);
    connect(search,&QLineEdit::textChanged,&dialog,[available](const QString &text) {
        for(int i=0;i<available->count();++i)available->item(i)->setHidden(!available->item(i)->text().contains(text,Qt::CaseInsensitive));
    });
    const auto update=[available,chosen,add,up,down,remove] {
        const int row=chosen->currentRow();up->setEnabled(row>0);down->setEnabled(row>=0 && row<chosen->count()-1);remove->setEnabled(row>=0);
        auto selected=available->currentItem();bool duplicate=false;
        for(int i=0;selected && i<chosen->count();++i)duplicate|=chosen->item(i)->data(Qt::UserRole)==selected->data(Qt::UserRole);
        add->setEnabled(selected && !selected->isHidden() && !duplicate);
    };
    const auto addSelected=[this,available,chosen,append,update] {
        if(auto selected=available->currentItem()) {
            const auto id=selected->data(Qt::UserRole).toString();
            for(int i=0;i<chosen->count();++i)if(chosen->item(i)->data(Qt::UserRole).toString()==id)return;
            if(auto entry=command(id)){append(chosen,*entry);chosen->setCurrentRow(chosen->count()-1);}
        }
        update();
    };
    connect(add,&QToolButton::clicked,&dialog,addSelected);
    connect(available,&QListWidget::itemDoubleClicked,&dialog,[addSelected]{addSelected();});
    connect(available,&QListWidget::currentRowChanged,&dialog,[update]{update();});
    connect(search,&QLineEdit::textChanged,&dialog,[update]{update();});
    connect(chosen,&QListWidget::currentRowChanged,&dialog,[update]{update();});
    connect(chosen->model(),&QAbstractItemModel::rowsMoved,&dialog,[update]{update();});
    const auto move=[chosen,update](int offset) {
        const int from=chosen->currentRow(),to=from+offset;
        if(from>=0 && to>=0 && to<chosen->count()){auto item=chosen->takeItem(from);chosen->insertItem(to,item);chosen->setCurrentRow(to);}
        update();
    };
    connect(up,&QAction::triggered,&dialog,[move]{move(-1);});
    connect(down,&QAction::triggered,&dialog,[move]{move(1);});
    connect(remove,&QAction::triggered,&dialog,[chosen,update]{delete chosen->takeItem(chosen->currentRow());update();});
    update();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted) {
        m_ids.clear();
        for(int i=0;i<chosen->count();++i)m_ids.append(chosen->item(i)->data(Qt::UserRole).toString());
        save();
    }
}
