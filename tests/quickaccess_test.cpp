#include "quickaccess.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <iostream>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QTemporaryDir directory;
    QSettings settings(directory.filePath("layout.ini"),QSettings::IniFormat);
    QAction first("First"),second("Second"),third("Third");
    first.setCheckable(true);
    int triggers=0;QObject::connect(&first,&QAction::triggered,[&]{++triggers;});
    const auto registerCommands=[&](QuickAccess &panel) {
        panel.registerCommand("first","View",&first);
        panel.registerCommand("second","File",&second);
        panel.registerCommand("third","History",&third);
    };
    QuickAccess panel(&settings);registerCommands(panel);panel.restore();panel.resize(150,450);panel.show();
    panel.addCommand("missing");panel.addCommand("first");panel.addCommand("second");panel.addCommand("first");
    if(panel.commandIds()!=QStringList({"first","second"}))return 1;
    app.processEvents();
    auto button=panel.findChild<QToolButton *>("quickAccessCommand_first");
    if(!button || button->defaultAction()!=&first || button->isChecked())return 2;
    first.setChecked(true);if(!button->isChecked())return 3;
    first.setEnabled(false);if(button->isEnabled())return 4;
    first.setEnabled(true);button->click();if(triggers!=1 || first.isChecked())return 5;
    first.setText("Changed command");if(!button->text().contains("Changed"))return 6;
    panel.moveCommand("second",-1);panel.moveCommand("second",-1);panel.moveCommand("first",5);
    if(panel.commandIds()!=QStringList({"second","first"}))return 7;
    panel.removeCommand("second");panel.addCommand("second");app.processEvents();
    bool managed=false;
    QTimer::singleShot(0,&panel,[&] {
        auto dialog=panel.window()->findChild<QDialog *>("quickAccessManager");
        if(!dialog){if(auto active=qobject_cast<QDialog *>(app.activeModalWidget()))active->reject();return;}
        auto available=dialog->findChild<QListWidget *>("quickAccessAvailable");
        auto chosen=dialog->findChild<QListWidget *>("quickAccessChosen");
        auto search=dialog->findChild<QLineEdit *>("quickAccessSearch");
        auto add=dialog->findChild<QToolButton *>("quickAccessAdd");
        search->setText("Third");
        for(int i=0;i<available->count();++i)if(!available->item(i)->isHidden())available->setCurrentRow(i);
        add->click();
        dialog->findChild<QAction *>("quickAccessMoveUp")->trigger();
        managed=chosen->count()==3 && chosen->item(1)->data(Qt::UserRole).toString()=="third";
        dialog->grab().save("quick-access-manager-ui.png");
        dialog->accept();
    });
    panel.manage();
    if(!managed || panel.commandIds()!=QStringList({"first","third","second"}))return 8;
    QTimer::singleShot(0,&panel,[&] {
        auto dialog=panel.window()->findChild<QDialog *>("quickAccessManager");
        auto chosen=dialog->findChild<QListWidget *>("quickAccessChosen");
        chosen->setCurrentRow(0);dialog->findChild<QAction *>("quickAccessRemove")->trigger();dialog->reject();
    });
    panel.manage();if(panel.commandIds().size()!=3)return 9;
    app.processEvents();
    button=panel.findChild<QToolButton *>("quickAccessCommand_first");
    if(!button)return 10;
    bool context=false;
    QTimer::singleShot(0,&panel,[&] {
        auto menu=button->findChild<QMenu *>();
        if(!menu)return;
        if(auto remove=menu->findChild<QAction *>("toggleQuickAccessCommand")){remove->trigger();context=true;}
        menu->close();
    });
    QMetaObject::invokeMethod(button,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,QPoint(2,2)));
    if(!context || panel.commandIds()!=QStringList({"third","second"}))return 11;
    app.processEvents();
    QuickAccess restored(&settings);registerCommands(restored);restored.restore();
    if(restored.commandIds()!=panel.commandIds())return 12;
    settings.setValue("quickAccess/commands",QStringList({"missing","first","first","third"}));
    restored.restore();if(restored.commandIds()!=QStringList({"first","third"}))return 13;
    std::cout<<"Quick Access commands, state, context menu, manager, ordering and persistence passed.\n";
}
