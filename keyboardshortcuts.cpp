#include "mainwindow.h"
#include <QAction>
#include <QShortcut>
#include <QSettings>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QSet>

namespace {
struct Binding { QObject *owner; QString label; QList<QKeySequence> keys; };
QString readable(const QKeySequence &sequence) {
    if(sequence.isEmpty())return QString();
    const auto combination=sequence[0];QStringList words;
    if(combination.keyboardModifiers() & Qt::ControlModifier)words<<"Ctrl";
    if(combination.keyboardModifiers() & Qt::MetaModifier)words<<"Windows";
    if(combination.keyboardModifiers() & Qt::AltModifier)words<<"Alt";
    if(combination.keyboardModifiers() & Qt::ShiftModifier)words<<"Shift";
    QString key=QKeySequence(combination.key()).toString(QKeySequence::PortableText);
    if(key=="+")key="Plus";if(key=="-")key="Minus";if(key=="[")key="Left Bracket [";if(key=="]")key="Right Bracket ]";
    words<<key;return words.join(" + ");
}
QString readableKeys(const QList<QKeySequence> &keys) {QStringList result;for(const auto &key:keys)result<<readable(key);return result.join(" or ");}
QList<Binding> bindings(MainWindow *window) {
    QList<Binding> result;
    for(auto action:window->findChildren<QAction *>()) if(action->property("customShortcutId").isValid())result.append({action,action->property("customShortcutLabel").toString(),action->shortcuts()});
    for(auto shortcut:window->findChildren<QShortcut *>()) if(shortcut->property("customShortcutId").isValid())result.append({shortcut,shortcut->property("customShortcutLabel").toString(),{shortcut->key()}});
    return result;
}
void assign(QObject *owner,const QList<QKeySequence> &keys) {
    if(auto action=qobject_cast<QAction *>(owner))action->setShortcuts(keys);
    else if(auto shortcut=qobject_cast<QShortcut *>(owner))shortcut->setKey(keys.isEmpty() ? QKeySequence() : keys.first());
}
QString fixedKeys() {return QStringLiteral(
"<h3>Editing gestures (fixed)</h3><table cellpadding='6'>"
"<tr><td>Space, or Alt + drag</td><td>Temporarily pan the image</td></tr>"
"<tr><td>Left / Right / Up / Down</td><td>Pan; Shift makes larger steps</td></tr>"
"<tr><td>Left / Right in Stereo Window</td><td>Push back / Pull forward one pixel</td></tr>"
"<tr><td>Delete or Backspace while drawing a lasso</td><td>Remove the latest segment</td></tr>"
"<tr><td>Delete with frame dots selected</td><td>Delete selected dots</td></tr>"
"<tr><td>Enter, Return, or C while drawing a lasso</td><td>Close the outline</td></tr>"
"<tr><td>Ctrl + Enter or Ctrl + Return</td><td>Apply the lasso selection</td></tr>"
"<tr><td>Escape</td><td>Cancel unfinished outline, color picking or polygon drag</td></tr>"
"<tr><td>Ctrl + click a dot</td><td>Add/remove it from the selected points</td></tr>"
"<tr><td>Ctrl + click a lasso anchor</td><td>Remove that anchor</td></tr>"
"<tr><td>Shift + click a lasso edge</td><td>Insert an anchor</td></tr>"
"<tr><td>Shift + drag a polygon</td><td>Constrain its movement horizontally</td></tr>"
"<tr><td>Wheel / Shift + wheel / Ctrl + wheel</td><td>In frame-point and lasso editing: scroll vertically / horizontally / zoom toward the pointer. Other tools retain wheel zoom.</td></tr>"
"<tr><td>Right-click a frame edge</td><td>Add a frame point</td></tr></table>"
"<p>Ctrl, Alt, Windows and Shift are spelled out here. Context-sensitive editing gestures remain fixed to protect drawing and text entry.</p>");}
}
void MainWindow::setupKeyboardShortcuts() {
    QSettings prefs;
    QList<QObject *> owners;
    const QStringList extra={"guidedProcessAction","stereoWindowAction","addFramePointAction","editFramePointsAction","keyboardShortcutsAction","pictureFrameAction","enlargeFrameAction","shrinkFrameAction","frameShapesAction","stereoTextAction"};
    for(auto action:findChildren<QAction *>())if(extra.contains(action->objectName()) || (!action->shortcuts().isEmpty() && action->objectName()!="applyLassoAction" && action->shortcuts().first()!=QKeySequence::Delete))owners<<action;
    for(auto shortcut:findChildren<QShortcut *>())owners<<shortcut;
    for(auto owner:owners) {
        QList<QKeySequence> keys;QString label;
        if(auto action=qobject_cast<QAction *>(owner)){keys=action->shortcuts();label=action->text().remove('&');if(label.startsWith("Undo"))label="Undo";if(label.startsWith("Redo"))label="Redo";}
        else {auto shortcut=qobject_cast<QShortcut *>(owner);keys={shortcut->key()};label=shortcut->key()==QKeySequence(Qt::Key_BracketLeft) ? "Smaller Brush" : "Larger Brush";}
        QString id=owner->objectName().isEmpty() ? label : owner->objectName();
        owner->setProperty("customShortcutId",id);owner->setProperty("customShortcutLabel",label);
        QStringList defaults;for(const auto &key:keys)defaults<<key.toString(QKeySequence::PortableText);owner->setProperty("shortcutDefaults",defaults);
        const QString path="keyboardShortcuts/"+id;
        if(prefs.contains(path)){QList<QKeySequence> saved;for(const auto &text:prefs.value(path).toStringList())if(!text.isEmpty())saved<<QKeySequence::fromString(text,QKeySequence::PortableText);assign(owner,saved);}
    }
}
void MainWindow::showKeyboardShortcuts() {
    QDialog dialog(this);dialog.setObjectName("keyboardShortcutsDialog");dialog.setWindowTitle(tr("Keyboard Shortcuts"));dialog.resize(700,650);
    auto layout=new QVBoxLayout(&dialog);auto text=new QTextBrowser(&dialog);QString html="<h2>Keyboard Shortcuts</h2><p>Windows key names are written as words. Shortcuts below reflect your saved preferences.</p><table cellpadding='6'>";
    for(const auto &binding:bindings(this))html+="<tr><td>"+binding.label.toHtmlEscaped()+"</td><td>"+(binding.keys.isEmpty() ? tr("Disabled") : readableKeys(binding.keys)).toHtmlEscaped()+"</td></tr>";
    text->setHtml(html+"</table>"+fixedKeys());layout->addWidget(text);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);auto edit=buttons->addButton(tr("Edit Shortcuts…"),QDialogButtonBox::ActionRole);edit->setObjectName("editShortcutsButton");
    connect(edit,&QPushButton::clicked,this,[this,text]{editKeyboardShortcuts();QString html="<h2>Keyboard Shortcuts</h2><table cellpadding='6'>";for(const auto &binding:bindings(this))html+="<tr><td>"+binding.label.toHtmlEscaped()+"</td><td>"+(binding.keys.isEmpty()?tr("Disabled"):readableKeys(binding.keys)).toHtmlEscaped()+"</td></tr>";text->setHtml(html+"</table>"+fixedKeys());});connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);dialog.exec();
}
void MainWindow::editKeyboardShortcuts() {
    const auto available=bindings(this);QDialog dialog(this);dialog.setObjectName("editShortcutsDialog");dialog.setWindowTitle(tr("Edit Keyboard Shortcuts"));dialog.resize(700,600);auto layout=new QVBoxLayout(&dialog);
    auto note=new QLabel(tr("Type one shortcut using words, for example Ctrl+Shift+R or Alt+G. Leave blank to disable. Fixed drawing keys cannot be reassigned. Changes apply after Save."));note->setWordWrap(true);layout->addWidget(note);
    auto table=new QTableWidget(available.size(),2,&dialog);table->setHorizontalHeaderLabels({tr("Command"),tr("Shortcut")});table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);layout->addWidget(table);QList<QLineEdit *> fields;
    for(int i=0;i<available.size();++i){auto item=new QTableWidgetItem(available[i].label);item->setFlags(item->flags() & ~Qt::ItemIsEditable);table->setItem(i,0,item);auto field=new QLineEdit(available[i].keys.isEmpty()?QString():readable(available[i].keys.first()));field->setObjectName("shortcutField"+QString::number(i));table->setCellWidget(i,1,field);fields<<field;}
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);auto reset=buttons->addButton(tr("Restore Defaults"),QDialogButtonBox::ResetRole);layout->addWidget(buttons);
    connect(reset,&QPushButton::clicked,&dialog,[available,fields]{for(int i=0;i<available.size();++i){const auto defaults=available[i].owner->property("shortcutDefaults").toStringList();fields[i]->setText(defaults.isEmpty()?QString():readable(QKeySequence(defaults.first())));}});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
        QSet<QString> used;QList<QList<QKeySequence>> edited;
        const QStringList fixed={"Left","Right","Up","Down","Shift+Left","Shift+Right","Shift+Up","Shift+Down","Space","Return","Enter","Delete","Backspace","Esc","C","Ctrl+Return","Ctrl+Enter"};
        for(int i=0;i<fields.size();++i){QString input=fields[i]->text().trimmed();input.replace("Command","Ctrl",Qt::CaseInsensitive).replace("Control","Ctrl",Qt::CaseInsensitive).replace("Option","Alt",Qt::CaseInsensitive).replace("Windows","Meta",Qt::CaseInsensitive).replace("Left Bracket [","[",Qt::CaseInsensitive).replace("Right Bracket ]","]",Qt::CaseInsensitive).replace("Plus","+",Qt::CaseInsensitive).replace("Minus","-",Qt::CaseInsensitive);input.remove(' ');
            QList<QKeySequence> keys;if(!input.isEmpty()){auto key=QKeySequence::fromString(input,QKeySequence::PortableText);if(key.count()!=1 || key[0].key()==Qt::Key_unknown || key[0].key()==0){QMessageBox::warning(&dialog,tr("Invalid shortcut"),tr("Enter one valid shortcut for %1.").arg(available[i].label));return;}const auto canonical=key.toString(QKeySequence::PortableText);
                if(used.contains(canonical) || fixed.contains(canonical)){QMessageBox::warning(&dialog,tr("Shortcut conflict"),tr("%1 is already used or reserved for editing.").arg(readable(key)));return;}used.insert(canonical);keys<<key;
                const auto defaults=available[i].owner->property("shortcutDefaults").toStringList();
                if(!defaults.isEmpty() && key==QKeySequence(defaults.first())) {keys.clear();for(const auto &text:defaults)keys<<QKeySequence(text);}
                else if(!available[i].keys.isEmpty() && key==available[i].keys.first()) keys=available[i].keys;
                for(int alias=1;alias<keys.size();++alias){auto value=keys[alias].toString(QKeySequence::PortableText);if(used.contains(value)){QMessageBox::warning(&dialog,tr("Shortcut conflict"),tr("A shortcut alias conflicts with another command."));return;}used.insert(value);}
            }
            edited<<keys;
        }
        QSettings prefs;for(int i=0;i<available.size();++i){assign(available[i].owner,edited[i]);QStringList stored;for(const auto &key:edited[i])stored<<key.toString(QKeySequence::PortableText);prefs.setValue("keyboardShortcuts/"+available[i].owner->property("customShortcutId").toString(),stored);}dialog.accept();
    });dialog.exec();
}
