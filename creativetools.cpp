#include "mainwindow.h"
#include "stereoviewwidget.h"
#include "stereowindowview.h"
#include "decorations.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QComboBox>
#include <QFontComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QTextEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QColorDialog>
#include <QShortcut>
#include <QSignalBlocker>
#include <QPainter>
#include <QMouseEvent>
#include <QUuid>

namespace {
class ShapeCanvas : public StereoWindowView {
    int m_drag=0;
    QPointF m_start;
    StereoText m_original;
public:
    QSize eyeSize;
    std::function<StereoText()> current;
    std::function<void(StereoText)> changed;
    explicit ShapeCanvas(QWidget *parent):StereoWindowView(parent){}
protected:
    void paintEvent(QPaintEvent *event) override {
        StereoWindowView::paintEvent(event);if(!current)return;
        const auto shape=current();QPainter painter(viewport());painter.setPen(QPen(Qt::cyan,2,Qt::DashLine));
        QRectF rect(QPointF(mapFromScene(shape.rect.topLeft())),QPointF(mapFromScene(shape.rect.bottomRight())));
        painter.setBrush(Qt::NoBrush);painter.drawRect(rect);painter.setBrush(Qt::cyan);
        painter.drawRect(QRectF(rect.bottomRight()-QPointF(5,5),QSizeF(10,10)));
    }
    void mousePressEvent(QMouseEvent *event) override {
        if(current && changed && event->button()==Qt::LeftButton && !(event->modifiers()&Qt::AltModifier)){
            m_original=current();m_start=mapToScene(event->pos());
            const QPointF corner=mapFromScene(m_original.rect.bottomRight());
            if(QLineF(event->position(),corner).length()<12)m_drag=2;
            else if(m_original.rect.contains(m_start))m_drag=1;
            if(m_drag){event->accept();return;}
        }
        StereoWindowView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(m_drag && (event->buttons()&Qt::LeftButton)){
            const QPointF delta=mapToScene(event->pos())-m_start;auto shape=m_original;
            if(m_drag==1)shape.rect.translate(delta);
            if(m_drag==2)shape.rect.setSize(QSizeF(qBound(32.0,m_original.rect.width()+delta.x(),eyeSize.width()*4.0),qBound(32.0,m_original.rect.height()+delta.y(),eyeSize.height()*4.0)));
            changed(shape);event->accept();return;
        }
        StereoWindowView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {if(m_drag){m_drag=0;event->accept();return;}StereoWindowView::mouseReleaseEvent(event);}
};

class TextCanvas : public StereoWindowView {
    int m_drag=0;bool m_right=false;QPointF m_start;StereoText m_original;
public:
    QSize eyeSize;bool editing=true;
    std::function<StereoText()> current;
    std::function<void(StereoText)> changed;
    explicit TextCanvas(QWidget *parent):StereoWindowView(parent){}
    QPointF shifted(QPointF point,bool right)const {if(right)point.rx()+=eyeSize.width()+20-current().depth;return point;}
protected:
    void paintEvent(QPaintEvent *event)override {
        StereoWindowView::paintEvent(event);if(!editing || !current)return;auto text=current();QPainter painter(viewport());painter.setPen(QPen(Qt::cyan,2,Qt::DashLine));
        for(bool right:{false,true}){QRectF rect(QPointF(mapFromScene(shifted(text.rect.topLeft(),right))),QPointF(mapFromScene(shifted(text.rect.bottomRight(),right))));painter.setBrush(Qt::NoBrush);painter.drawRect(rect);painter.setBrush(Qt::cyan);painter.drawRect(QRectF(rect.bottomRight()-QPointF(5,5),QSizeF(10,10)));if(text.style){const QPointF tail=mapFromScene(shifted(text.tail,right));painter.drawEllipse(tail,6,6);}}
    }
    void mousePressEvent(QMouseEvent *event)override {
        if(editing && current && changed && event->button()==Qt::LeftButton && !(event->modifiers()&Qt::AltModifier)){
            m_original=current();m_start=mapToScene(event->pos());
            for(bool right:{false,true}){const QPointF corner=mapFromScene(shifted(m_original.rect.bottomRight(),right));const QPointF tail=mapFromScene(shifted(m_original.tail,right));QRectF rect(shifted(m_original.rect.topLeft(),right),shifted(m_original.rect.bottomRight(),right));if(m_original.style && QLineF(event->position(),tail).length()<12)m_drag=3;else if(QLineF(event->position(),corner).length()<12)m_drag=2;else if(rect.contains(m_start))m_drag=1;if(m_drag){m_right=right;Q_UNUSED(m_right);event->accept();return;}}
        }
        StereoWindowView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event)override {
        if(m_drag && (event->buttons()&Qt::LeftButton)){const QPointF delta=mapToScene(event->pos())-m_start;auto text=m_original;if(m_drag==1){text.rect.translate(delta);text.tail+=delta;}if(m_drag==2)text.rect.setSize(QSizeF(qBound(32.0,m_original.rect.width()+delta.x(),eyeSize.width()*4.0),qBound(32.0,m_original.rect.height()+delta.y(),eyeSize.height()*4.0)));if(m_drag==3)text.tail+=delta;changed(text);event->accept();return;}StereoWindowView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event)override {if(m_drag){m_drag=0;event->accept();return;}StereoWindowView::mouseReleaseEvent(event);}
};

void canvasControls(QVBoxLayout *layout,QDialog *dialog,StereoWindowView *canvas) {
    auto row=new QHBoxLayout;
    for(auto entry:QList<QPair<QString,double>>{{QObject::tr("Zoom +"),1.25},{QObject::tr("Zoom −"),0.8},{QObject::tr("Fit"),0}}){
        auto button=new QPushButton(entry.first);row->addWidget(button);
        QObject::connect(button,&QPushButton::clicked,dialog,[canvas,factor=entry.second]{if(factor)canvas->zoomImage(factor);else canvas->fitImage();});
    }
    row->addWidget(new QLabel(QObject::tr("Scroll to zoom. Drag the shape to move it; drag its corner to resize. Alt-drag outside it to pan.")));row->addStretch();layout->addLayout(row);
    for(auto entry:QList<QPair<QString,double>>{{"Ctrl++",1.25},{"Ctrl+=",1.25},{"Ctrl+-",0.8},{"Ctrl+0",0}}){
        auto key=new QShortcut(QKeySequence(entry.first),dialog);QObject::connect(key,&QShortcut::activated,dialog,[canvas,factor=entry.second]{if(factor)canvas->zoomImage(factor);else canvas->fitImage();});
    }
}

QImage pairImage(const QImage &left,const QImage &right) {
    QImage pair(left.width()*2+20,left.height(),QImage::Format_ARGB32_Premultiplied);pair.fill(QColor("#333333"));
    QPainter painter(&pair);painter.drawImage(0,0,left);painter.drawImage(left.width()+20,0,right);return pair;
}
}

void MainWindow::createFrameShape()
{
    if(!m_viewWidget->isImageLoaded())return;
    if(m_viewWidget->hasLasso() || m_viewWidget->polygonMoveEnabled()){
        QMessageBox::information(this,tr("Shapes"),tr("Apply or cancel the unfinished lasso and turn Move polygon off first."));return;
    }
    QDialog dialog(this);dialog.setObjectName("frameShapeDialog");dialog.setWindowTitle(tr("Create Frame — Shapes"));
    auto layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel(tr("Choose a shape, drag it into position, and resize it with its bottom-right handle. Apply creates ordinary editable frame points.")));
    auto canvas=new ShapeCanvas(&dialog);canvas->setObjectName("shapeCanvas");canvas->eyeSize=m_viewWidget->image().size();layout->addWidget(canvas,1);
    canvasControls(layout,&dialog,canvas);
    StereoText draft;draft.rect=QRectF(canvas->eyeSize.width()*0.15,canvas->eyeSize.height()*0.15,canvas->eyeSize.width()*0.7,canvas->eyeSize.height()*0.7);
    auto row=new QHBoxLayout;auto shape=new QComboBox;shape->setObjectName("frameShapeType");shape->addItems({tr("Rectangle"),tr("Square"),tr("Oval"),tr("Circle")});row->addWidget(shape);
    auto field=[&](const QString &name,int min,int max,int value){row->addWidget(new QLabel(name));auto spin=new QSpinBox;spin->setRange(min,max);spin->setValue(value);row->addWidget(spin);return spin;};
    auto width=field(tr("Width"),32,canvas->eyeSize.width()*4,int(draft.rect.width()));auto height=field(tr("Height"),32,canvas->eyeSize.height()*4,int(draft.rect.height()));
    auto depth=field(tr("Depth"),-10000,10000,0);depth->setObjectName("shapeDepth");layout->addLayout(row);
    auto redraw=[&]{
        const bool equal=shape->currentIndex()==1 || shape->currentIndex()==3;if(equal)draft.rect.setHeight(draft.rect.width());draft.depth=depth->value();
        {QSignalBlocker a(width),b(height);width->setValue(qRound(draft.rect.width()));height->setValue(qRound(draft.rect.height()));}height->setEnabled(!equal);
        auto left=m_viewWidget->leftImage(),right=m_viewWidget->rightImage();const auto points=shapePoints(shape->currentIndex(),draft.rect,draft.depth);
        PictureFrameSettings previewSettings;previewSettings.enabled=true;previewSettings.width=3;previewSettings.style=0;previewSettings.color=Qt::cyan;
        QImage empty(left.size(),QImage::Format_ARGB32_Premultiplied);empty.fill(Qt::transparent);
        {QPainter p(&left);p.drawImage(0,0,StereoViewWidget::pictureFrameOverlay(left.size(),points,false,empty,previewSettings));}
        {QPainter p(&right);p.drawImage(0,0,StereoViewWidget::pictureFrameOverlay(right.size(),points,true,empty,previewSettings));}
        canvas->setImage(pairImage(left,right));canvas->viewport()->update();
    };
    canvas->current=[&]{return draft;};canvas->changed=[&](StereoText value){draft=value;redraw();};
    connect(shape,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&]{if(shape->currentIndex()==1 || shape->currentIndex()==3){const double side=qMin(draft.rect.width(),draft.rect.height());draft.rect.setSize(QSizeF(side,side));}redraw();});
    connect(width,QOverload<int>::of(&QSpinBox::valueChanged),&dialog,[&](int value){draft.rect.setWidth(value);redraw();});
    connect(height,QOverload<int>::of(&QSpinBox::valueChanged),&dialog,[&](int value){draft.rect.setHeight(value);redraw();});
    connect(depth,QOverload<int>::of(&QSpinBox::valueChanged),&dialog,[&]{redraw();});
    auto note=new QLabel(tr("Positive depth puts the frame forward. After Apply, use Edit frame to move dots or curve handles, insert points, scale or move. This replaces the basic frame; foreground selections remain. Undo restores the old frame."));note->setWordWrap(true);layout->addWidget(note);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply Shape"));layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    redraw();dialog.showMaximized();
    if(dialog.exec()==QDialog::Accepted){
        if(!m_viewWidget->points().isEmpty() && QMessageBox::question(this,tr("Replace Frame"),tr("Replace the current basic frame with this shape? Foreground selections remain; Undo restores the old frame."))!=QMessageBox::Yes)return;
        m_viewWidget->replaceFramePoints(shapePoints(shape->currentIndex(),draft.rect,draft.depth));
    }
}

void MainWindow::editStereoText()
{
    if(!m_viewWidget->isImageLoaded())return;
    if(m_viewWidget->hasLasso() || m_viewWidget->polygonMoveEnabled()){QMessageBox::information(this,tr("Text & Speech Bubbles"),tr("Apply or cancel the unfinished lasso and turn Move polygon off first."));return;}
    QDialog dialog(this);dialog.setObjectName("stereoTextDialog");dialog.setWindowTitle(tr("Stereo Text & Speech Bubbles"));
    auto layout=new QVBoxLayout(&dialog);auto splitter=new QSplitter;layout->addWidget(splitter,1);
    auto canvas=new TextCanvas(&dialog);canvas->setObjectName("textCanvas");canvas->eyeSize=m_viewWidget->image().size();splitter->addWidget(canvas);
    auto panel=new QWidget;auto form=new QFormLayout(panel);form->setRowWrapPolicy(QFormLayout::WrapAllRows);panel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(panel);scroll->setMinimumWidth(310);scroll->setMaximumWidth(400);splitter->addWidget(scroll);splitter->setStretchFactor(0,1);
    QVector<StereoText> draft=m_viewWidget->textObjects();int active=0;bool syncing=false,anaglyph=false;
    auto objects=new QComboBox;objects->setMinimumContentsLength(16);objects->setObjectName("textObjects");form->addRow(tr("Text object:"),objects);
    auto tools=new QHBoxLayout;auto add=new QPushButton(tr("Add"));auto duplicate=new QPushButton(tr("Duplicate"));auto remove=new QPushButton(tr("Delete"));tools->addWidget(add);tools->addWidget(duplicate);tools->addWidget(remove);form->addRow(tools);
    auto content=new QTextEdit;content->setObjectName("stereoTextContent");content->setMaximumHeight(100);form->addRow(tr("Text:"),content);
    auto font=new QFontComboBox;font->setMinimumContentsLength(16);form->addRow(tr("Font:"),font);
    auto spin=[&](const QString &label,int min,int max){auto value=new QSpinBox;value->setRange(min,max);form->addRow(label,value);return value;};
    auto size=spin(tr("Font size (pixels):"),1,1000);auto fontStyle=new QHBoxLayout;auto bold=new QCheckBox(tr("Bold"));auto italic=new QCheckBox(tr("Italic"));fontStyle->addWidget(bold);fontStyle->addWidget(italic);form->addRow(fontStyle);
    auto style=new QComboBox;style->setObjectName("speechBubbleStyle");style->addItems({tr("Text only"),tr("Oval speech bubble"),tr("Rounded rectangle speech bubble"),tr("Thought bubble")});form->addRow(tr("Bubble style:"),style);
    auto alignment=new QComboBox;alignment->addItems({tr("Left"),tr("Center"),tr("Right")});form->addRow(tr("Text alignment:"),alignment);
    auto x=spin(tr("Left-eye X:"),-100000,100000),y=spin(tr("Y:"),-100000,100000),width=spin(tr("Width:"),32,100000),height=spin(tr("Height:"),32,100000);
    auto tailX=spin(tr("Tail X:"),-100000,100000),tailY=spin(tr("Tail Y:"),-100000,100000);auto depth=spin(tr("Stereo depth (pixels):"),-10000,10000);depth->setObjectName("textDepth");
    auto depthSlider=new QSlider(Qt::Horizontal);depthSlider->setRange(-10000,10000);form->addRow(tr("Back ← depth → Forward"),depthSlider);
    auto fillOpacity=spin(tr("Bubble fill opacity (%):"),0,100),outline=spin(tr("Outline width:"),0,100);auto visible=new QCheckBox(tr("Visible"));auto covers=new QCheckBox(tr("Foreground covers text"));form->addRow(visible);form->addRow(covers);
    auto textColor=new QPushButton(tr("Text color…")),fillColor=new QPushButton(tr("Bubble fill color…")),strokeColor=new QPushButton(tr("Outline color…"));form->addRow(textColor);form->addRow(fillColor);form->addRow(strokeColor);
    auto note=new QLabel(tr("Edit once for both eyes. Positive depth pulls forward. Drag the box to move, its corner to resize, or its tail dot to point a bubble. Foreground covers text uses your preserved foreground selections for occlusion."));note->setWordWrap(true);form->addRow(note);
    canvasControls(layout,&dialog,canvas);auto preview=new QPushButton(tr("Preview Anaglyph"));layout->addWidget(preview);auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply Text & Bubbles"));layout->addWidget(buttons);
    const QImage baseLeft=m_viewWidget->exportEye(false,false),baseRight=m_viewWidget->exportEye(true,false);
    auto redraw=[&]{QImage left=baseLeft,right=baseRight;{QPainter p(&left);p.drawImage(0,0,textOverlay(left.size(),draft,false,m_viewWidget->revealMask(false)));}{QPainter p(&right);p.drawImage(0,0,textOverlay(right.size(),draft,true,m_viewWidget->revealMask(true)));}canvas->editing=!anaglyph && !draft.isEmpty();if(anaglyph){StereoImageProcessor processor;processor.createAnaglyphFromEyes(left,right);canvas->setImage(processor.anaglyphImage());}else canvas->setImage(pairImage(left,right));canvas->viewport()->update();};
    std::function<void()> sync;sync=[&]{syncing=true;objects->clear();for(int i=0;i<draft.size();++i)objects->addItem(tr("%1: %2").arg(i+1).arg(draft[i].text.left(30)));if(!draft.isEmpty()){active=qBound(0,active,int(draft.size())-1);objects->setCurrentIndex(active);const auto &text=draft[active];content->setPlainText(text.text);font->setCurrentFont(text.font);size->setValue(text.fontSize);bold->setChecked(text.font.bold());italic->setChecked(text.font.italic());style->setCurrentIndex(text.style);alignment->setCurrentIndex(text.alignment);x->setValue(qRound(text.rect.x()));y->setValue(qRound(text.rect.y()));width->setValue(qRound(text.rect.width()));height->setValue(qRound(text.rect.height()));tailX->setValue(qRound(text.tail.x()));tailY->setValue(qRound(text.tail.y()));depth->setValue(text.depth);depthSlider->setValue(text.depth);fillOpacity->setValue(text.opacity);outline->setValue(text.outline);visible->setChecked(text.visible);covers->setChecked(text.foregroundCovers);for(auto entry:QList<QPair<QPushButton *,QColor>>{{textColor,text.color},{fillColor,text.fill},{strokeColor,text.stroke}})entry.first->setStyleSheet(QString("border:3px solid %1").arg(entry.second.name()));}for(auto widget:panel->findChildren<QWidget *>())if(widget!=add)widget->setEnabled(!draft.isEmpty());add->setEnabled(draft.size()<100);syncing=false;redraw();};
    auto update=[&]{if(syncing || draft.isEmpty())return;auto &text=draft[active];text.text=content->toPlainText().left(10000);text.font=font->currentFont();text.font.setBold(bold->isChecked());text.font.setItalic(italic->isChecked());text.fontSize=size->value();text.style=style->currentIndex();text.alignment=alignment->currentIndex();text.rect=QRectF(x->value(),y->value(),width->value(),height->value());text.tail=QPointF(tailX->value(),tailY->value());text.depth=depth->value();text.opacity=fillOpacity->value();text.outline=outline->value();text.visible=visible->isChecked();text.foregroundCovers=covers->isChecked();{QSignalBlocker blocker(depthSlider);depthSlider->setValue(text.depth);}objects->setItemText(active,tr("%1: %2").arg(active+1).arg(text.text.left(30)));redraw();};
    canvas->current=[&]{return draft.isEmpty()?StereoText{}:draft[active];};canvas->changed=[&](StereoText text){if(draft.isEmpty())return;draft[active]=text;sync();};
    connect(objects,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&](int index){if(!syncing && index>=0){active=index;sync();}});
    connect(add,&QPushButton::clicked,&dialog,[&]{StereoText text;text.id=QUuid::createUuid().toString(QUuid::WithoutBraces);text.rect=QRectF(canvas->eyeSize.width()*0.2,canvas->eyeSize.height()*0.2,qMax(64,canvas->eyeSize.width()/2),qMax(48,canvas->eyeSize.height()/4));text.fontSize=qMax(12,canvas->eyeSize.height()/20);text.tail=text.rect.bottomLeft()+QPointF(20,60);draft.append(text);active=draft.size()-1;sync();});
    connect(duplicate,&QPushButton::clicked,&dialog,[&]{if(draft.isEmpty() || draft.size()>=100)return;auto text=draft[active];text.id=QUuid::createUuid().toString(QUuid::WithoutBraces);text.rect.translate(20,20);text.tail+=QPointF(20,20);draft.append(text);active=draft.size()-1;sync();});
    connect(remove,&QPushButton::clicked,&dialog,[&]{if(draft.isEmpty())return;draft.removeAt(active);sync();});connect(content,&QTextEdit::textChanged,&dialog,update);connect(font,&QFontComboBox::currentFontChanged,&dialog,[&]{update();});
    for(auto value:{size,x,y,width,height,tailX,tailY,depth,fillOpacity,outline})connect(value,QOverload<int>::of(&QSpinBox::valueChanged),&dialog,[&]{update();});for(auto check:{bold,italic,visible,covers})connect(check,&QCheckBox::toggled,&dialog,[&]{update();});for(auto choice:{style,alignment})connect(choice,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[&]{update();});connect(depthSlider,&QSlider::valueChanged,depth,&QSpinBox::setValue);
    auto pick=[&](QPushButton *button,int which){connect(button,&QPushButton::clicked,&dialog,[&,which]{if(draft.isEmpty())return;auto &text=draft[active];QColor &color=which==0?text.color:which==1?text.fill:text.stroke;auto next=QColorDialog::getColor(color,&dialog);if(next.isValid()){color=next;sync();}});};pick(textColor,0);pick(fillColor,1);pick(strokeColor,2);
    connect(preview,&QPushButton::clicked,&dialog,[&]{anaglyph=!anaglyph;preview->setText(anaglyph?tr("Return to Side-by-Side Editing"):tr("Preview Anaglyph"));canvas->fitImage();redraw();});connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(draft.isEmpty())add->click();else sync();dialog.showMaximized();if(dialog.exec()==QDialog::Accepted)m_viewWidget->setTextObjects(draft);
}
