#include "mainwindow.h"
#include "stereoviewwidget.h"
#include <QDockWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QAction>
#include <QCheckBox>
#include <QTimer>
#include <QToolBar>

void MainWindow::startGuidedProcess() {
    if(auto existing=findChild<QDockWidget *>("guidedProcessDock")){existing->show();existing->raise();return;}
    auto dock=new QDockWidget(tr("Guided Process"),this);dock->setObjectName("guidedProcessDock");dock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);addDockWidget(Qt::RightDockWidgetArea,dock);
    auto panel=new QWidget(dock);auto layout=new QVBoxLayout(panel);dock->setWidget(panel);panel->setMinimumWidth(285);panel->setMaximumWidth(390);
    auto title=new QLabel;title->setObjectName("guidedStepTitle");title->setWordWrap(true);layout->addWidget(title);
    auto instruction=new QLabel;instruction->setObjectName("guidedInstructions");instruction->setWordWrap(true);instruction->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(instruction);
    auto goal=new QComboBox;goal->setObjectName("guidedGoal");goal->addItems({tr("Create a basic frame"),tr("Create an out-of-frame effect"),tr("Adjust stereo window only")});layout->addWidget(goal);
    auto protrude=new QComboBox;protrude->setObjectName("guidedProtrude");protrude->addItems({tr("Yes, preserve protruding objects"),tr("No, use the basic frame")});layout->addWidget(protrude);
    auto tool=new QComboBox;tool->setObjectName("guidedTool");tool->addItems({tr("Select Brush"),tr("Polygonal lasso"),tr("Fuzzy Select region"),tr("Select by color threshold")});layout->addWidget(tool);
    auto bothEyes=new QCheckBox(tr("I have checked both eye images"));bothEyes->setObjectName("guidedBothEyes");layout->addWidget(bothEyes);
    auto action=new QPushButton;action->setObjectName("guidedAction");layout->addWidget(action);
    auto extra=new QPushButton;extra->setObjectName("guidedExtra");layout->addWidget(extra);
    auto message=new QLabel;message->setObjectName("guidedMessage");message->setWordWrap(true);layout->addWidget(message);layout->addSpacing(12);
    auto row=new QHBoxLayout;auto back=new QPushButton(tr("Back"));auto skip=new QPushButton(tr("Skip"));auto next=new QPushButton(tr("Next"));back->setObjectName("guidedBack");skip->setObjectName("guidedSkip");next->setObjectName("guidedNext");row->addWidget(back);row->addWidget(skip);row->addWidget(next);layout->addLayout(row);layout->addStretch();
    dock->setProperty("step",0);dock->setProperty("goal",0);dock->setProperty("trail",QVariantList{});
    auto reveal=[this](int section){if(auto toggle=findChild<QAction *>(QString("sectionToggle%1").arg(section)))toggle->setChecked(true);};
    auto refresh=[=] {
        const int step=dock->property("step").toInt();message->clear();goal->setVisible(step==0);protrude->setVisible(step==5);tool->setVisible(step==6);bothEyes->setVisible(step==7);back->setEnabled(step>0);skip->setVisible(step==2 || step==4 || step==8);extra->setVisible(step==3 || step==4 || step==7 || step==9 || step==10);next->setText(step==10 ? tr("Finish") : tr("Next"));
        const QStringList titles={tr("Choose your goal"),tr("Open your stereo image"),tr("Set the starting stereo window"),tr("Create your basic frame"),tr("Refine your frame"),tr("Should anything protrude?"),tr("Choose a foreground tool"),tr("Preserve objects in each eye"),tr("Choose the background and border"),tr("Review your result"),tr("Save and export")};
        title->setText(QString("<b>%1 — %2</b>").arg(step+1).arg(titles[step]));
        action->setVisible(step!=0 && step!=5);
        switch(step) {
        case 0:instruction->setText(tr("Choose what you want to do. The guide uses the image already open, or helps you open one. It keeps your current edits."));break;
        case 1:instruction->setText(m_viewWidget->isImageLoaded()?tr("An image is already open. Choose Next to use it, or Open Image to choose another."):tr("Open a side-by-side stereo image using Open Image. Next becomes useful after an image is loaded."));action->setText(tr("Open Image"));break;
        case 2:instruction->setText(tr("Optional: use the anaglyph slider until the red/cyan edges overlap at the depth you want for the stereo window. Apply or Cancel the dialog, then return here and choose Next. Do this before tracing objects."));action->setText(tr("Adjust Stereo Window"));break;
        case 3:instruction->setText(m_viewWidget->points().size()>=3 ? tr("Your existing frame is ready. You can keep it and choose Next, or edit its dots.") : tr("Choose Create Frame Shape for an editable rectangle, square, circle or oval. Or choose Create with points, then right-click a series of corners around the area to keep in either eye. Three or more points form a closed opening. Four points usually make a rectangle. Left-drag dots to adjust them."));action->setText(tr("Create with points"));extra->setText(tr("Create Frame Shape"));break;
        case 4:instruction->setText(tr("Right-click an edge away from a dot to add a point. Left-drag dots to move them; select dots and use Delete Point to remove them. Add two points along one edge for a rectangular extension. Select the dots you want to adjust before using frame depth, scale, alignment or movement controls."));action->setText(tr("Open Edit Frame controls"));extra->setText(tr("Picture Frame appearance"));break;
        case 5:instruction->setText(tr("Do you want legs, wheels or other objects to remain visible outside the frame? Yes guides you through preserving them. No takes you to background and review."));break;
        case 6:instruction->setText(tr("Select Brush paints areas to preserve. Polygonal lasso places precise points around an edge. Fuzzy Select chooses one connected color region; Color Threshold chooses matching pixels throughout one eye. Set Color Tolerance for either color tool. Choose a tool, click Activate Tool, then Next."));action->setText(tr("Activate Tool"));break;
        case 7:instruction->setText(tr("Work on each eye separately. Brush: paint the parts to preserve. Lasso: outline the object and Apply Selection. Fuzzy/Color: click a representative pixel; each click creates a History object immediately. Use Add or Subtract and repeat as needed. Check the other eye as well."));action->setText(tr("Open Out-of-Frame controls"));extra->setText(tr("Apply Selection"));break;
        case 8:instruction->setText(tr("Choose Out-of-Frame Background: transparent PNG, black, white, a solid color, or Match preview. Match preview uses Background Color and opacity. A Picture Frame border is optional and appears behind preserved objects."));action->setText(tr("Choose background controls"));extra->setVisible(true);extra->setText(tr("Picture Frame appearance"));break;
        case 9:instruction->setText(tr("Use Clean Preview to inspect the result without dots and outlines. Check both eyes, edges and frame depth; Anaglyph can help. Turn Clean Preview off before drawing corrections. Back returns to earlier steps without undoing your work."));action->setText(tr("Toggle Clean Preview"));extra->setText(tr("Toggle Anaglyph"));break;
        case 10:instruction->setText(tr("Save Project retains editable frame and foreground selections. Export creates the finished image; choose PNG for transparency. Saving a project does not export an image. You can leave or restart this guide without losing your current edits."));action->setText(tr("Save Project"));extra->setText(tr("Export Image"));break;
        }
    };
    auto move=[=](bool skipping) {
        int step=dock->property("step").toInt();
        if(m_viewWidget->hasLasso() && step!=0 && step!=1){message->setText(tr("Apply or cancel your unfinished outline before changing steps."));return;}
        if(step==0){dock->setProperty("goal",goal->currentIndex());step=1;}
        else if(step==1){if(!m_viewWidget->isImageLoaded()){message->setText(tr("Please open a stereo image first."));return;}step=2;}
        else if(!m_viewWidget->isImageLoaded()){dock->setProperty("step",1);refresh();return;}
        else if(step==2)step=dock->property("goal").toInt()==2 ? 9 : 3;
        else if(step==3){if(m_viewWidget->points().size()<3){message->setText(tr("Add at least three frame points before continuing."));return;}step=4;}
        else if(step==5)step=protrude->currentIndex()==0 ? 6 : 8;
        else if(step==6){auto combo=findChild<QComboBox *>("outOfFrameToolCombo");const int mode=QList<int>{1,7,9,10}.value(tool->currentIndex(),1);const int index=combo->findData(mode);combo->setCurrentIndex(index);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,index));if(auto clean=findChild<QAction *>("cleanPreviewAction"))clean->setChecked(false);for(auto preview:findChildren<QAction *>())if(preview->text()=="Preview Result")preview->setChecked(false);reveal(5);step=7;}
        else if(step==7 && !bothEyes->isChecked()){message->setText(tr("Check both eye images, then tick the confirmation to continue."));return;}
        else if(step==10){dock->hide();return;}
        else ++step;
        Q_UNUSED(skipping);auto trail=dock->property("trail").toList();trail.append(dock->property("step"));dock->setProperty("trail",trail);dock->setProperty("step",step);refresh();
    };
    connect(next,&QPushButton::clicked,dock,[move]{move(false);});connect(skip,&QPushButton::clicked,dock,[move]{move(true);});
    connect(back,&QPushButton::clicked,dock,[=]{if(m_viewWidget->hasLasso()){message->setText(tr("Apply or cancel the unfinished outline first."));return;}auto trail=dock->property("trail").toList();if(!trail.isEmpty()){dock->setProperty("step",trail.takeLast());dock->setProperty("trail",trail);refresh();}});
    connect(action,&QPushButton::clicked,dock,[=]{const int step=dock->property("step").toInt();
        if(step==1){newProject();refresh();}
        else if(step==2)adjustStereoWindow();
        else if(step==3){reveal(3);findChild<QAction *>("createFramePointsAction")->trigger();}
        else if(step==4){reveal(4);findChild<QAction *>("editFramePointsAction")->trigger();}
        else if(step==6){auto combo=findChild<QComboBox *>("outOfFrameToolCombo");const int mode=QList<int>{1,7,9,10}.value(tool->currentIndex(),1);int index=combo->findData(mode);combo->setCurrentIndex(index);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,index));if(auto clean=findChild<QAction *>("cleanPreviewAction"))clean->setChecked(false);for(auto preview:findChildren<QAction *>())if(preview->text()=="Preview Result")preview->setChecked(false);reveal(5);}
        else if(step==7 || step==8)reveal(5);
        else if(step==9)findChild<QAction *>("cleanPreviewAction")->trigger();
        else if(step==10){if(m_viewWidget->saveProject()){saveProject();message->setText(tr("Project saved. Export separately to create the finished image."));}else message->setText(tr("Project could not be saved. Check that an image is open and its folder is writable."));}
    });
    connect(extra,&QPushButton::clicked,dock,[=]{int step=dock->property("step").toInt();if(step==3)createFrameShape();else if(step==4 || step==8)adjustPictureFrame();else if(step==7)findChild<QAction *>("applyLassoAction")->trigger();else if(step==9){for(auto action:findChild<QToolBar *>("controlSection1")->actions())if(action->text()=="Anaglyph")action->trigger();}else if(step==10)exportImage();});
    refresh();dock->show();
}
