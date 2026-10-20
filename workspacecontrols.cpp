#include "mainwindow.h"
#include "stereoviewwidget.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
void updateSwatch(QPushButton *button,const QColor &color)
{
    QPixmap swatch(20,20);swatch.fill(color);button->setIcon(QIcon(swatch));
    button->setToolTip(color.name(QColor::HexArgb));
}
QPushButton *colorButton(QWidget *parent,const QString &name,QColor &color)
{
    auto button=new QPushButton(QObject::tr("Color..."),parent);button->setObjectName(name);updateSwatch(button,color);
    QObject::connect(button,&QPushButton::clicked,parent,[parent,button,&color] {
        const auto next=QColorDialog::getColor(color,parent,QObject::tr("Choose color"));
        if(next.isValid()){color=next;updateSwatch(button,color);}
    });
    return button;
}
QSpinBox *number(QFormLayout *form,const QString &label,const QString &name,int value,int maximum)
{
    auto spin=new QSpinBox;spin->setObjectName(name);spin->setRange(0,maximum);spin->setValue(value);form->addRow(label,spin);return spin;
}
}

void MainWindow::adjustGrid()
{
    auto settings=m_viewWidget->gridSettings();
    QDialog dialog(this);dialog.setWindowTitle(tr("Alignment Grid"));dialog.setObjectName("gridSettingsDialog");
    auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;layout->addLayout(form);
    auto enabled=new QCheckBox(tr("Show grid"),&dialog);enabled->setObjectName("gridEnabled");enabled->setChecked(settings.enabled);form->addRow(enabled);
    auto horizontal=number(form,tr("Horizontal lines:"),"gridHorizontal",settings.horizontal,100);
    auto vertical=number(form,tr("Vertical lines:"),"gridVertical",settings.vertical,100);
    auto mode=new QComboBox(&dialog);mode->setObjectName("gridColorMode");mode->addItems({tr("Single color"),tr("Two colors")});mode->setCurrentIndex(settings.twoColors?1:0);form->addRow(tr("Line colors:"),mode);
    auto first=colorButton(&dialog,"gridFirstColor",settings.first);form->addRow(tr("Primary:"),first);
    auto second=colorButton(&dialog,"gridSecondColor",settings.second);form->addRow(tr("Secondary:"),second);second->setEnabled(settings.twoColors);
    connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),second,[second](int index){second->setEnabled(index==1);});
    auto opacity=number(form,tr("Opacity:"),"gridOpacity",settings.opacity,100);opacity->setSuffix("%");
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&] {
        const GridSettings defaults;enabled->setChecked(defaults.enabled);horizontal->setValue(defaults.horizontal);vertical->setValue(defaults.vertical);mode->setCurrentIndex(0);opacity->setValue(defaults.opacity);
        settings.first=defaults.first;settings.second=defaults.second;updateSwatch(first,settings.first);updateSwatch(second,settings.second);
    });
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted) {
        settings.enabled=enabled->isChecked();settings.horizontal=horizontal->value();settings.vertical=vertical->value();
        settings.twoColors=mode->currentIndex()==1;settings.opacity=opacity->value();m_viewWidget->setGridSettings(settings);
    }
}

void MainWindow::adjustOutlineFrame()
{
    if(!m_viewWidget->isImageLoaded() || m_viewWidget->points().size()<3) {
        QMessageBox::information(this,tr("Outline Frame"),tr("Make your basic frame with at least three points first."));return;
    }
    auto settings=m_viewWidget->pictureFrameSettings();
    QDialog dialog(this);dialog.setWindowTitle(tr("Outline Frame"));dialog.setObjectName("outlineFrameDialog");
    auto layout=new QVBoxLayout(&dialog);
    auto enabled=new QCheckBox(tr("Apply outline frame"),&dialog);enabled->setObjectName("outlineEnabled");enabled->setChecked(settings.outlineEnabled);layout->addWidget(enabled);
    auto display=new QCheckBox(tr("Display in editor"),&dialog);display->setObjectName("outlineDisplay");display->setChecked(settings.outlineDisplay);layout->addWidget(display);
    display->setToolTip(tr("Clean Preview and export always include an applied outline."));
    auto form=new QFormLayout;layout->addLayout(form);
    auto offset=number(form,tr("Offset from frame:"),"outlineOffset",settings.outlineOffset,1000);offset->setSuffix(tr(" px"));
    offset->setToolTip(tr("Gap outside the frame contour in source-image pixels. The outline follows each eye's depth and is clipped to the image canvas."));
    struct Band { QString name,id;int *width;QColor *color;bool *background;QSpinBox *spin=nullptr;QPushButton *button=nullptr;QCheckBox *check=nullptr; };
    QList<Band> bands={{tr("Inner"),"Inner",&settings.outlineInnerWidth,&settings.outlineInnerColor,&settings.outlineInnerBackground},
                      {tr("Middle"),"Middle",&settings.outlineMiddleWidth,&settings.outlineMiddleColor,&settings.outlineMiddleBackground},
                      {tr("Outer"),"Outer",&settings.outlineOuterWidth,&settings.outlineOuterColor,&settings.outlineOuterBackground}};
    for(auto &band:bands) {
        auto group=new QGroupBox(band.name,&dialog);auto row=new QFormLayout(group);layout->addWidget(group);
        band.spin=number(row,tr("Width:"),"outline"+band.id+"Width",*band.width,1000);band.spin->setSuffix(tr(" px"));
        band.button=colorButton(&dialog,"outline"+band.id+"Color",*band.color);row->addRow(tr("Color:"),band.button);
        band.check=new QCheckBox(tr("Use mask color"),group);band.check->setChecked(*band.background);band.check->setObjectName("outline"+band.id+"Background");row->addRow(band.check);
        band.button->setEnabled(!*band.background);connect(band.check,&QCheckBox::toggled,band.button,[button=band.button](bool checked){button->setEnabled(!checked);});
    }
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&] {
        enabled->setChecked(false);offset->setValue(20);display->setChecked(true);
        for(int i=0;i<bands.size();++i) {
            auto &band=bands[i];band.spin->setValue(i==1?10:5);*band.color=QColor(i==1?"#808080":"#c0c0c0");band.check->setChecked(false);updateSwatch(band.button,*band.color);
        }
    });
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted) {
        settings.outlineEnabled=enabled->isChecked();settings.outlineDisplay=display->isChecked();settings.outlineOffset=offset->value();
        for(auto &band:bands){*band.width=band.spin->value();*band.background=band.check->isChecked();}
        m_viewWidget->setPictureFrameSettings(settings);
    }
}
