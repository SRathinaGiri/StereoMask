#include "smartselection.h"
#include "stereoviewwidget.h"
#include "decorations.h"
#include <QApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static QImage fixture() {
    QImage image(240,180,QImage::Format_RGB32); image.fill(QColor(60,130,65));
    QPainter p(&image); p.setPen(Qt::NoPen);p.setBrush(QColor(220,170,130));p.drawEllipse(QRect(60,28,96,130));p.end();
    // Texture avoids degenerate single-color GMMs and represents a photograph.
    for(int y=0;y<180;++y)for(int x=0;x<240;++x) {
        QColor c=image.pixelColor(x,y); const int n=((x*17+y*11)%15)-7;
        image.setPixelColor(x,y,QColor(c.red()+n,c.green()+n,c.blue()+n));
    }
    return image;
}
static QImage hints(QSize size) {
    QImage h(size,QImage::Format_ARGB32_Premultiplied);h.fill(Qt::transparent);
    QPainter p(&h);p.fillRect(QRect(104,70,8,45),Qt::green);p.fillRect(QRect(177,60,8,50),Qt::red);return h;
}
static void waitFor(bool &finished) {
    QElapsedTimer timer;timer.start();
    while(!finished && timer.elapsed()<20000) { QApplication::processEvents();QThread::msleep(10); }
    require(finished,"async edge selection timed out");
}
static void click(QWidget &view, QPoint pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(pos),QPointF(pos),Qt::LeftButton,Qt::LeftButton,modifiers);
    QApplication::sendEvent(&view,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(pos),QPointF(pos),Qt::LeftButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(&view,&release);
}
static void drag(QWidget &view,QPoint from,QPoint to,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(from),QPointF(from),Qt::LeftButton,Qt::LeftButton,modifiers);
    QMouseEvent move(QEvent::MouseMove,QPointF(to),QPointF(to),Qt::NoButton,Qt::LeftButton,modifiers);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(to),QPointF(to),Qt::LeftButton,Qt::NoButton,modifiers);
    QApplication::sendEvent(&view,&press);QApplication::sendEvent(&view,&move);QApplication::sendEvent(&view,&release);
}
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 try {
    QTemporaryDir borderTemp;
    QImage borderPair(480,180,QImage::Format_RGB32);borderPair.fill(QColor(60,130,65));
    const QString borderFile=borderTemp.path()+"/border.png";require(borderPair.save(borderFile),"border fixture");
    StereoViewWidget border;border.setAutoSave(false);require(border.loadImage(borderFile),"border load");
    for (const QPointF &point:QVector<QPointF>{{80,50},{160,50},{160,130},{80,130}}) border.addPoint(point,0);
    border.setCutoutBackground(0);
    PictureFrameSettings decoration;decoration.enabled=true;decoration.width=12;decoration.style=0;decoration.color=Qt::red;
    border.setPictureFrameSettings(decoration);
    require(border.exportEye(false).pixelColor(75,90)==QColor(Qt::red),"border color/width");
    require(border.exportEye(false).pixelColor(60,90).alpha()==0,"border outside alpha");
    require(border.exportEye(false).pixelColor(100,90)==QColor(60,130,65),"border covers opening");
    SelectionState foreground;SelectionObject leg;leg.id="leg";leg.path.addRect(QRectF(70,80,25,20));foreground.objects={leg};border.restoreSelectionState(foreground);
    require(border.exportEye(false).pixelColor(75,90)==QColor(60,130,65),"foreground must cover border");
    require(border.exportEye(true).pixelColor(75,90)==QColor(Qt::red),"foreground eye isolation");
    require(border.saveProject(),"border save");StereoViewWidget borderReload;borderReload.setAutoSave(false);
    require(borderReload.loadProject(borderTemp.path()+"/border.msk") && borderReload.pictureFrameSettings()==decoration && borderReload.exportEye(false)==border.exportEye(false),"border project roundtrip");
    decoration.style=2;border.setPictureFrameSettings(decoration);require(border.exportEye(true).pixelColor(75,90)!=QColor(Qt::red),"decorative cache refresh");
    border.undoStack()->undo();require(border.pictureFrameSettings().style==0,"border undo");border.undoStack()->redo();require(border.pictureFrameSettings().style==2,"border redo");
    const QString borderExport=borderTemp.path()+"/export.png";
    require(StereoViewWidget::saveComposedPair(borderExport,border.exportEye(false),border.exportEye(true),3,4,5,Qt::transparent),"border composed export");
    QImage exportedBorder(borderExport);require(exportedBorder.pixelColor(78,94)==border.exportEye(false).pixelColor(75,90) && exportedBorder.pixelColor(0,0).alpha()==0,"border export pixels/padding");
    std::cout<<"Picture-frame styles, foreground overlap, eye isolation, persistence, Undo/Redo and PNG export: PASS\n";

    const QRectF shapeBounds(60,20,140,140);
    border.replaceFramePoints(shapePoints(0,shapeBounds,10));require(border.points().size()==4,"rectangle shape vertices");
    border.undoStack()->undo();require(border.points().size()==4,"shape replacement undo");border.undoStack()->redo();
    const auto ellipse=shapePoints(2,shapeBounds,10);border.replaceFramePoints(ellipse);
    require(border.points().size()==4 && border.points()[0].isCurve && border.points()[2].cp1Disparity==10,"ellipse shape geometry");
    const QImage ellipseOpening=border.openingMask(false);const auto first=border.points()[0],second=border.points()[1];
    const QPointF curveMiddle=(first.pos+first.cp1*3+first.cp2*3+second.pos)/8;
    border.addPoint(curveMiddle,10,1);require(border.points().size()==5 && border.points()[0].isCurve && border.points()[1].isCurve,"curve insertion split");
    const QImage splitOpening=border.openingMask(false);int curveDifference=0;
    for(int y=0;y<splitOpening.height();++y)for(int x=0;x<splitOpening.width();++x)curveDifference+=qAbs(qAlpha(ellipseOpening.pixel(x,y))-qAlpha(splitOpening.pixel(x,y)));
    require(curveDifference<600,"curve split preserves shape");
    std::cout<<"Rectangle/Oval frame shapes and curve-preserving point insertion: PASS\n";

    QTemporaryDir windowTemp;
    const QString windowFile=windowTemp.path()+"/window.png";
    QImage windowSource(40,20,QImage::Format_RGB32);windowSource.fill(Qt::black);
    windowSource.setPixelColor(12,10,Qt::white);windowSource.setPixelColor(28,10,Qt::white);
    require(windowSource.save(windowFile),"window fixture save");
    StereoViewWidget windowView;windowView.setAutoSave(false);require(windowView.loadImage(windowFile),"window load");
    const auto originalWindowLeft=windowView.leftImage();
    require(windowView.stereoWindowPreview(-4).pixelColor(10,10)==QColor(Qt::white),"window anaglyph alignment");
    const auto sideBySideWindow=windowView.stereoWindowPreview(-4,false);
    require(sideBySideWindow.size()==QSize(40,20) && sideBySideWindow.pixelColor(10,10)==QColor(Qt::white) &&
            sideBySideWindow.pixelColor(30,10)==QColor(Qt::white),"window side-by-side alignment");
    require(windowView.stereoWindowOffset()==0 && windowView.leftImage()==originalWindowLeft,"window preview mutates source");
    windowView.setStereoWindowOffset(-4);
    require(windowView.leftImage().pixelColor(10,10)==QColor(Qt::white) && windowView.rightImage().pixelColor(10,10)==QColor(Qt::white),"window relative shift");
    require(windowView.leftImage().size()==originalWindowLeft.size() && windowView.leftImage().pixelColor(19,0).alpha()==0,"window edge/canvas");
    require(windowView.exportEye(false).pixelColor(10,10)==QColor(Qt::white),"window export/cache");
    windowView.undoStack()->undo();require(windowView.stereoWindowOffset()==0 && windowView.leftImage()==originalWindowLeft,"window undo");
    windowView.undoStack()->redo();require(windowView.stereoWindowOffset()==-4,"window redo");
    require(windowView.saveProject(),"window save");
    StereoViewWidget windowReload;windowReload.setAutoSave(false);
    require(windowReload.loadProject(windowTemp.path()+"/window.msk") && windowReload.stereoWindowOffset()==-4 && windowReload.leftImage()==windowView.leftImage(),"window project roundtrip");
    StereoViewWidget windowSidecar;windowSidecar.setAutoSave(false);
    require(windowSidecar.loadImage(windowFile) && windowSidecar.stereoWindowOffset()==-4,"window sidecar roundtrip");
    windowView.addPoint(QPointF(3,3),0);windowView.setStereoWindowOffset(3);
    require(windowView.points()[0].pos==QPointF(3,3) && windowView.leftImage().pixelColor(14,10)==QColor(Qt::white) && windowView.rightImage().pixelColor(7,10)==QColor(Qt::white),"window frame separation/odd shift");
    windowView.setStereoWindowOffset(0);
    require(windowView.leftImage()==originalWindowLeft && QImage(windowFile)==windowSource,"window reset/original preserved");
    std::cout<<"Stereo-window pixels, SBS/anaglyph preview, export/cache, alpha, Undo/Redo, project/sidecar, frame separation and original preservation: PASS\n";

    QImage insertionPair(720,240,QImage::Format_RGB32);insertionPair.fill(Qt::gray);
    const QString insertionFile=windowTemp.path()+"/insert.png";require(insertionPair.save(insertionFile),"frame insertion fixture save");
    StereoViewWidget insertionView;insertionView.setAutoSave(false);insertionView.resize(800,400);
    require(insertionView.loadImage(insertionFile),"frame insertion fixture load");insertionView.fitView();
    const QVector<QPointF> corners={{80,50},{160,50},{160,130},{80,130}};
    for(const auto &point:corners) insertionView.addPoint(point,0);
    QMouseEvent addFramePoint(QEvent::MouseButtonPress,QPointF(159,129),QPointF(159,129),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QApplication::sendEvent(&insertionView,&addFramePoint);
    require(insertionView.points().size()==5 && insertionView.points()[0].pos==corners[0] &&
            insertionView.points()[2].pos==corners[1] && insertionView.points()[3].pos==corners[2] &&
            insertionView.points()[4].pos==corners[3],"nearest-edge insert changes frame order");
    insertionView.undoStack()->undo();require(insertionView.points().size()==4,"frame insertion undo");
    insertionView.undoStack()->redo();require(insertionView.points().size()==5,"frame insertion redo");
    std::cout<<"Nearest-edge frame insertion preserves vertices/order and Undo/Redo: PASS\n";

    StereoViewWidget drawnFrame;drawnFrame.setAutoSave(false);drawnFrame.resize(800,400);
    require(drawnFrame.loadImage(insertionFile),"right-click frame fixture load");drawnFrame.fitView();
    require(drawnFrame.points().isEmpty(),"fresh right-click frame must start empty");
    auto rightClickFrame=[&](QPointF point) {
        // At fit scale 1 the left image's pixel origin is (19,79).
        const QPointF widgetPoint=point+QPointF(19,79);
        QMouseEvent press(QEvent::MouseButtonPress,widgetPoint,widgetPoint,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,widgetPoint,widgetPoint,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&drawnFrame,&press);QApplication::sendEvent(&drawnFrame,&release);
    };
    for(const auto &point:corners) rightClickFrame(point);
    require(drawnFrame.points().size()==4,"right-click frame point count");
    for(int i=0;i<corners.size();++i) {
        require(QLineF(drawnFrame.points()[i].pos,corners[i]).length()<0.01,"new frame must retain click order and close to first point");
    }
    rightClickFrame(QPointF(80,90));
    require(drawnFrame.points().size()==5 && drawnFrame.points().first().pos==corners.first() &&
            drawnFrame.points().last().pos==QPointF(80,90),"closing-edge insertion must preserve first vertex");
    drawnFrame.undoStack()->undo();require(drawnFrame.points().size()==4 && drawnFrame.points().first().pos==corners.first(),"closing-edge insertion undo");
    drawnFrame.undoStack()->redo();require(drawnFrame.points().size()==5 && drawnFrame.points().last().pos==QPointF(80,90),"closing-edge insertion redo");
    std::cout<<"Right-click frame creation retains click order and closing-edge insertion retains first vertex: PASS\n";

    border.replaceFramePoints(ellipse);
    const auto closing=ellipse.last();
    const QPointF closingMiddle=(closing.pos+closing.cp1*3+closing.cp2*3+ellipse.first().pos)/8;
    border.addPoint(closingMiddle,10,ellipse.size());
    require(border.points().size()==5 && border.points().first().pos==ellipse.first().pos && border.points().last().isCurve,"closing curve split keeps first vertex");
    const QImage closingOpening=border.openingMask(false);int closingDifference=0;
    for(int y=0;y<closingOpening.height();++y)for(int x=0;x<closingOpening.width();++x)
        closingDifference+=qAbs(qAlpha(ellipseOpening.pixel(x,y))-qAlpha(closingOpening.pixel(x,y)));
    require(closingDifference<600,"closing curve split preserves shape");
    border.undoStack()->undo();require(border.points().size()==4,"closing curve split undo");
    border.undoStack()->redo();require(border.points().size()==5,"closing curve split redo");

    const QImage image=fixture();const QImage seed=hints(image.size());const QRect region(30,12,170,156);
    QImage rough(image.size(),QImage::Format_ARGB32_Premultiplied);rough.fill(Qt::transparent);
    {QPainter p(&rough);p.fillRect(QRect(43,20,126,145),Qt::white);p.fillRect(QRect(229,160,4,4),Qt::white);}
    const auto result=findSmartEdges(image,region,seed,rough);
    require(result.error.isEmpty()&&!result.mask.isNull(),"native GrabCut failed");
    require(qAlpha(result.mask.pixel(108-region.x(),90-region.y()))==255,"Keep seed lost");
    require(qAlpha(result.mask.pixel(180-region.x(),90-region.y()))==0,"Remove seed survived");
    require(qAlpha(result.mask.pixel(45-region.x(),90-region.y()))==0,"sloppy background survived");
    require(qAlpha(result.mask.pixel(65-region.x(),90-region.y()))==255,"object edge failed to expand beyond Keep stroke");
    const auto fresh=findSmartEdges(image,region,seed,QImage());
    require(fresh.error.isEmpty()&&qAlpha(fresh.mask.pixel(65-region.x(),90-region.y()))==255,"fresh selection needs pre-existing reveal");
    QImage distractor=image;{QPainter p(&distractor);p.setPen(Qt::NoPen);p.setBrush(QColor(220,170,130));p.drawEllipse(QRect(165,125,24,24));}
    const auto isolated=findSmartEdges(distractor,region,seed,rough);
    require(isolated.error.isEmpty()&&qAlpha(isolated.mask.pixel(175-region.x(),140-region.y()))==0,"unseeded disconnected object survived");
    require(!findSmartEdges(image,region,QImage(),rough).error.isEmpty(),"missing seeds should be rejected");
    require(!findSmartEdges(image,QRect(-1,0,20,20),seed,rough).error.isEmpty(),"invalid ROI should be rejected");
    QTemporaryDir temp;require(temp.isValid(),"temporary directory failed");
    QImage sbs(480,180,QImage::Format_RGB32);{QPainter p(&sbs);p.drawImage(0,0,image);p.drawImage(240,0,image);}
    const QString file=temp.path()+"/stereo.png";require(sbs.save(file),"fixture save failed");
    StereoViewWidget view;view.setAutoSave(false);view.resize(800,400);require(view.loadImage(file),"fixture load failed");
    view.addPoint(QPointF(20,20),13);view.addPoint(QPointF(210,20),13);view.addPoint(QPointF(210,65),13);view.addPoint(QPointF(20,65),13);
    const auto frame=view.points();
    view.applyReveal(false,rough);view.applySmartHints(false,seed,region);
    bool finished=false;QString status;
    QObject::connect(&view,&StereoViewWidget::smartBusy,[&](bool busy){if(!busy)finished=true;});
    QObject::connect(&view,&StereoViewWidget::smartStatus,[&](const QString &s){status=s;});
    const int undoIndex=view.undoStack()->index();view.findEdges();waitFor(finished);
    require(status.startsWith("Edges refined"),"async job did not apply");
    const QImage refined=view.revealMask(false);
    require(qAlpha(refined.pixel(45,90))==0,"refinement failed to replace sloppy paint");
    require(qAlpha(refined.pixel(230,161))==255,"paint outside ROI was modified");
    require(view.revealMask(true).isNull(),"opposite eye was modified");
    require(view.points().size()==frame.size(),"frame point count changed");
    for(int i=0;i<frame.size();++i) require(view.points()[i].pos==frame[i].pos&&view.points()[i].disparity==13,"frame geometry or depth changed");
    require(view.undoStack()->index()==undoIndex+1,"refinement not one undo operation");
    view.undoStack()->undo();require(view.revealMask(false)==rough,"Undo did not restore rough paint");
    view.undoStack()->redo();require(view.revealMask(false)==refined,"Redo did not restore refinement");
    require(view.saveProject(),"project save failed");
    StereoViewWidget restored;restored.setAutoSave(false);require(restored.loadProject(temp.path()+"/stereo.msk"),"project reload failed");
    require(restored.revealMask(false).convertToFormat(QImage::Format_ARGB32_Premultiplied)==refined.convertToFormat(QImage::Format_ARGB32_Premultiplied),"refined mask lost after reload");
    QFile project(temp.path()+"/stereo.msk");require(project.open(QIODevice::ReadOnly),"read project failed");
    const auto json=QJsonDocument::fromJson(project.readAll()).object();
    project.close(); // Windows requires the read handle closed before QSaveFile replaces it.
    require(!json["smartHintsLeftPNG"].toString().isEmpty(),"hints not persisted");
    require(json["smartRegionLeft"].toArray().size()==4,"box not persisted");
    // Swap mapping: painting the right DISPLAY viewport edits the physical left eye.
    view.setSwapSides(true);view.setBrushMode(1);view.setBrushSize(8);
    const QImage rightBefore=view.revealMask(true);click(view,QPoint(726,244));
    require(view.revealMask(true)==rightBefore,"Swap brush edited wrong physical eye");
    require(view.revealMask(false)!=refined,"Swap brush did not paint physical left eye");
    // A stale async result must not overwrite a newer mask.
    view.applySmartHints(false,seed,region);finished=false;view.findEdges();view.applyReveal(false,rough);waitFor(finished);
    require(status.startsWith("Selection changed"),"stale result was not discarded");
    require(view.revealMask(false)==rough,"stale result overwrote newer paint");
    const QString exportFile=temp.path()+"/cutout.png";
    require(StereoViewWidget::saveCutout(exportFile,image,image,frame,refined,QImage(),0,0,0,0),"PNG export failed");
    const QImage exported(exportFile);
    require(qAlpha(exported.pixel(45,90))==0,"export background is opaque");
    require(qAlpha(exported.pixel(108,90))==255,"export lost selected leg");
    require(qAlpha(exported.pixel(108,40))==255,"export lost frame interior");
    // Native event-driven lasso tests: Backspace and toolbar Delete retain previous segments.
    view.setSwapSides(false); view.setBrushMode(7); view.applyReveal(false,QImage());
    click(view,QPoint(120,145)); click(view,QPoint(250,145)); click(view,QPoint(250,260));
    QKeyEvent back(QEvent::KeyPress,Qt::Key_Backspace,Qt::NoModifier);
    QApplication::sendEvent(&view,&back);require(view.hasLasso(),"Backspace erased entire outline");
    click(view,QPoint(240,255)); view.deleteSelectedPoints();require(view.hasLasso(),"Delete action erased entire outline");
    click(view,QPoint(240,255)); click(view,QPoint(120,255));
    const auto beforeLasso=view.revealMask(false);const auto otherEye=view.revealMask(true);const int li=view.undoStack()->index();
    view.closeLasso();view.applyLasso();
    require(!view.hasLasso(),"applied outline was not cleared");
    const QImage lasso=view.revealMask(false);require(!lasso.isNull(),"polygon lasso did not create mask");
    require(qAlpha(lasso.pixel(95,90))>0,"polygon lasso failed to keep interior");
    require(qAlpha(lasso.pixel(220,160))==0,"polygon lasso kept distant exterior");
    require(view.revealMask(true)==otherEye,"lasso modified other eye");
    require(view.undoStack()->index()==li+1,"lasso not a single undo");
    view.undoStack()->undo();require(view.revealMask(false)==beforeLasso,"lasso undo failed");view.undoStack()->redo();
    // Adjust a point by dragging, then replace a sloppy ROI while preserving distant paint.
    view.applyReveal(false,rough); view.applySmartHints(false,seed,region);
    click(view,QPoint(120,145));click(view,QPoint(250,145));click(view,QPoint(250,260));click(view,QPoint(120,260));
    QMouseEvent dp(QEvent::MouseButtonPress,QPointF(250,260),QPointF(250,260),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&dp);
    QMouseEvent dm(QEvent::MouseMove,QPointF(280,280),QPointF(280,280),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&dm);
    QMouseEvent dr(QEvent::MouseButtonRelease,QPointF(280,280),QPointF(280,280),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&dr);
    view.applyLasso(true);const auto replaced=view.revealMask(false);
    require(qAlpha(replaced.pixel(145,133))==0,"dragged point did not enlarge erased polygon");
    require(qAlpha(replaced.pixel(95,90))==0,"Erase lasso retained enclosed reveal");
    require(qAlpha(replaced.pixel(5,5))==qAlpha(rough.pixel(5,5)),"Replace box changed distant paint");
    view.undoStack()->undo();require(view.revealMask(false)==rough,"Replace box undo failed");
    view.applyReveal(false,lasso);
    // Escape discards pending work without changing the painted reveal.
    click(view,QPoint(140,150));QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&view,&escape);
    require(!view.hasLasso() && view.revealMask(false)==lasso,"Escape changed committed reveal");
    // Clicking the first anchor closes instead of beginning a drag; inspect status before applying.
    view.applyReveal(false,QImage()); view.fitView(); view.setBrushMode(7);
    click(view,QPoint(120,145));click(view,QPoint(250,145));click(view,QPoint(240,255));click(view,QPoint(120,255));
    click(view,QPoint(120,145));require(status.startsWith("Outline closed"),"first-anchor click did not close outline");
    view.applyLasso();require(view.revealMask(false)==lasso,"first-anchor closure changed polygon mask");
    // A click on the first point of an already closed outline still supports dragging it.
    view.applyReveal(false,QImage());click(view,QPoint(120,145));click(view,QPoint(250,145));click(view,QPoint(240,255));click(view,QPoint(120,255));click(view,QPoint(120,145));
    QMouseEvent fp(QEvent::MouseButtonPress,QPointF(120,145),QPointF(120,145),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&fp);
    QMouseEvent fm(QEvent::MouseMove,QPointF(90,125),QPointF(90,125),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&fm);
    QMouseEvent fr(QEvent::MouseButtonRelease,QPointF(90,125),QPointF(90,125),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&fr);
    view.applyLasso();require(qAlpha(view.revealMask(false).pixel(50,50))>0,"closed first anchor could not be adjusted");
    // Navigate while a lasso is unfinished: source-coordinate geometry must survive.
    view.applyReveal(false,QImage());view.fitView();view.setBrushMode(7);
    click(view,QPoint(120,145));click(view,QPoint(250,145));click(view,QPoint(240,255));click(view,QPoint(120,255));
    QKeyEvent spaceDown(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier);QApplication::sendEvent(&view,&spaceDown);
    QMouseEvent panPress(QEvent::MouseButtonPress,QPointF(170,180),QPointF(170,180),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&panPress);
    QMouseEvent panMove(QEvent::MouseMove,QPointF(230,220),QPointF(230,220),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&panMove);
    QMouseEvent panRelease(QEvent::MouseButtonRelease,QPointF(230,220),QPointF(230,220),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&panRelease);
    QKeyEvent spaceUp(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);QApplication::sendEvent(&view,&spaceUp);
    const auto beforeWheelPan=view.panOffset();const auto beforeWheelZoom=view.zoom();
    QWheelEvent scroll(QPointF(210,210),QPointF(210,210),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&view,&scroll);
    require(view.zoom()==beforeWheelZoom && view.panOffset().x()==beforeWheelPan.x() && view.panOffset().y()>beforeWheelPan.y(),"ordinary lasso wheel must pan vertically without zoom");
    QWheelEvent wheel(QPointF(210,210),QPointF(210,210),QPoint(),QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&view,&wheel);
    require(view.zoom()>1,"Ctrl+wheel did not zoom");require(view.hasLasso(),"pan/zoom discarded unfinished lasso");
    view.fitView();require(view.zoom()==1 && view.hasLasso(),"Fit view lost outline");view.applyLasso();
    require(view.revealMask(false)==lasso,"pan/zoom changed lasso source geometry");
    for(int mode:{0,7,8}) {
        view.setBrushMode(mode);view.fitView();view.setZoom(2);
        const auto frameBefore=view.points();const int undoBefore=view.undoStack()->index();
        QWheelEvent down(QPointF(210,210),QPointF(210,210),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&view,&down);
        require(view.zoom()==2 && view.panOffset().x()==0 && view.panOffset().y()<0,"point-edit wheel down must scroll both eyes without zoom");
        const auto afterDown=view.panOffset();
        QWheelEvent smooth(QPointF(210,210),QPointF(210,210),QPoint(0,24),QPoint(),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
        QApplication::sendEvent(&view,&smooth);
        require(view.zoom()==2 && view.panOffset().y()>afterDown.y(),"trackpad pixel scrolling must pan vertically");
        const auto beforeHorizontal=view.panOffset();
        QWheelEvent horizontal(QPointF(210,210),QPointF(210,210),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::ShiftModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&view,&horizontal);
        require(view.zoom()==2 && view.panOffset().x()<beforeHorizontal.x() && view.panOffset().y()==beforeHorizontal.y(),"Shift+wheel must scroll horizontally only");
        QWheelEvent horizontalReverse(QPointF(210,210),QPointF(210,210),QPoint(),QPoint(120,0),Qt::NoButton,Qt::ShiftModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&view,&horizontalReverse);
        require(QLineF(view.panOffset(),beforeHorizontal).length()<0.01,"native horizontal Shift+wheel must reverse horizontal scroll");
        QWheelEvent zoomOut(QPointF(210,210),QPointF(210,210),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(&view,&zoomOut);
        require(view.zoom()<2 && view.undoStack()->index()==undoBefore && view.points().size()==frameBefore.size(),"Ctrl+wheel zoom out must not edit frame/history");
        for(int i=0;i<frameBefore.size();++i)require(view.points()[i].pos==frameBefore[i].pos,"wheel changes frame geometry");
    }
    view.fitView();view.setBrushMode(7);
    view.clearPaintedReveal();require(view.revealMask(false).isNull() && view.revealMask(true).isNull(),"clear reveal failed");
    view.undoStack()->undo();require(view.revealMask(false)==lasso && view.revealMask(true)==otherEye,"clear reveal undo failed");
    // Multi-selection regression: closed first contour automatically commits on a new click.
    StereoViewWidget multi;multi.setAutoSave(false);multi.resize(800,400);require(multi.loadImage(file),"multi fixture load failed");multi.fitView();multi.setBrushMode(7);
    click(multi,QPoint(120,145));click(multi,QPoint(250,145));click(multi,QPoint(250,230));click(multi,QPoint(120,230));click(multi,QPoint(120,145));
    require(multi.lassoClosed(),"first multi outline not closed");
    click(multi,QPoint(260,245));
    require(multi.hasLasso() && !multi.lassoClosed(),"new contour could not start after closing first");
    require(qAlpha(multi.revealMask(false).pixel(90,80))==255,"starting new contour lost first mask");
    require(multi.completedLasso(false).contains(QPointF(90,80)),"completed first contour disappeared");
    click(multi,QPoint(320,245));click(multi,QPoint(320,300));click(multi,QPoint(260,300));click(multi,QPoint(260,245));
    QApplication::sendEvent(&multi,&escape);
    require(!multi.hasLasso(),"Escape did not finalize closed contour");
    require(multi.completedLasso(false).contains(QPointF(175,140)) && multi.completedLasso(false).contains(QPointF(90,80)),"two completed outlines not retained");
    const QImage both=multi.revealMask(false);const QPainterPath bothOutline=multi.completedLasso(false);
    multi.setLassoSubtract(true);
    click(multi,QPoint(150,160));click(multi,QPoint(200,160));click(multi,QPoint(200,210));click(multi,QPoint(150,210));multi.closeLasso();multi.finishLasso();
    require(qAlpha(multi.revealMask(false).pixel(100,80))==0,"Subtract did not remove enclosed selection");
    require(qAlpha(multi.revealMask(false).pixel(90,105))==255,"Subtract removed entire previous contour");
    require(qAlpha(multi.revealMask(false).pixel(175,140))==255,"Subtract removed another leg");
    require(!multi.completedLasso(false).contains(QPointF(100,80)),"Subtract boundary still includes removed area");
    multi.undoStack()->undo();require(multi.revealMask(false)==both && multi.completedLasso(false)==bothOutline,"selection undo did not restore mask and outlines together");
    multi.undoStack()->redo();
    require(multi.saveProject(),"multi selection project save failed");
    StereoViewWidget reopen;reopen.setAutoSave(false);require(reopen.loadProject(temp.path()+"/stereo.msk"),"multi project reopen failed");
    require(reopen.completedLasso(false).contains(QPointF(175,140)) && !reopen.completedLasso(false).contains(QPointF(100,80)),"multiple contour persistence lost selection or hole");
    require(reopen.revealMask(false).convertToFormat(QImage::Format_ARGB32_Premultiplied)==multi.revealMask(false).convertToFormat(QImage::Format_ARGB32_Premultiplied),"multiple selection alpha changed after reload");
    const auto retained=multi.completedLasso(false);const auto retainedMask=multi.revealMask(false);
    click(multi,QPoint(330,145));QApplication::sendEvent(&multi,&escape);
    require(multi.completedLasso(false)==retained && multi.revealMask(false)==retainedMask,"Escape unfinished erased completed work");
    multi.setLassoSubtract(false);
    click(multi,QPoint(230,150));click(multi,QPoint(290,150));click(multi,QPoint(290,210));click(multi,QPoint(230,210));multi.closeLasso();multi.finishLasso();
    require(multi.completedLasso(false).contains(QPointF(165,75)),"overlapping Add did not enlarge selection");
    require(multi.revealMask(true).isNull(),"multi-selection changed other eye");
    std::cout<<"Multiple polygon Add/Subtract, closed-Escape retention, contour+alpha undo, holes and project persistence: PASS\n";
    // Magnetic path must bend toward a real edge between its exact clicked endpoints.
    QImage edge(120,100,QImage::Format_RGB32);edge.fill(Qt::black);
    QPainter ep(&edge);ep.fillRect(QRect(0,40,120,60),Qt::white);ep.end();
    const auto trace=magneticLassoPath(edge,QPointF(10,40),QPointF(110,40));
    require(trace.size()>50 && trace.first()==QPointF(10,40) && trace.last()==QPointF(110,40),"magnetic path endpoints failed");
    for(const auto &point:trace) require(std::abs(point.y()-40)<3,"magnetic path left strong edge");
    // Real-photo diagnostic: both physical eyes, several short anchored segments along upper leg/shoe.
    QImage real("/workspace/scratch/edb9f7a24ab9/upload/BelushiAndCheerleaders_InjectZ_SHARP_Parallel_Depth0.060.png");
    if(!real.isNull()) {
        QImage overlay=real.copy(); QPainter rp(&overlay);rp.setPen(QPen(Qt::magenta,1));
        const QVector<QPointF> anchors={QPointF(438,201),QPointF(470,205),QPointF(501,213),QPointF(529,220),QPointF(559,226),QPointF(574,218),QPointF(593,209),QPointF(616,203),QPointF(608,225),QPointF(587,249),QPointF(575,261),QPointF(562,253),QPointF(555,244),QPointF(528,239),QPointF(503,240),QPointF(475,232),QPointF(449,221),QPointF(438,201)};
        for(int eye=0;eye<2;++eye) {
            QImage eyeImage=real.copy(eye*624,0,624,420);
            for(int i=1;i<anchors.size();++i) {
                const auto segment=magneticLassoPath(eyeImage,anchors[i-1],anchors[i]);
                for(int k=1;k<segment.size();++k) rp.drawLine(segment[k-1]+QPointF(eye*624,0),segment[k]+QPointF(eye*624,0));
            }
        }
        rp.end();overlay.save("/tmp/stereomask-r4-real-trace.png");
    }
    // Source-resolution stereo matching, including reverse direction and fallback.
    QImage stereoLeft(240,180,QImage::Format_RGB32);
    quint32 random=12345;
    for (int y=0; y<180; ++y) for (int x=0; x<240; ++x) {
        random=random*1664525u+1013904223u;
        const int value=int(random>>24); stereoLeft.setPixel(x,y,qRgb(value,value,value));
    }
    QImage stereoRight(stereoLeft.size(),stereoLeft.format()); stereoRight.fill(Qt::black);
    { QPainter p(&stereoRight); p.drawImage(-12,0,stereoLeft); }
    const QVector<QPointF> matchAnchors={{72,60},{150,60},{150,110},{72,110}};
    const auto stereoMatch=alignStereoPolygon(stereoLeft,stereoRight,matchAnchors,0);
    require(stereoMatch.matchedAnchors==4,"stereo anchor matching failed");
    for (int i=0;i<matchAnchors.size();++i)
        require(stereoMatch.anchors[i]==matchAnchors[i]-QPointF(12,0),"stereo matching has incorrect disparity");
    const auto reverseMatch=alignStereoPolygon(stereoRight,stereoLeft,stereoMatch.anchors,0);
    require(reverseMatch.anchors==matchAnchors && reverseMatch.matchedAnchors==4,"reverse stereo matching failed");
    QImage blank(240,180,QImage::Format_RGB32); blank.fill(Qt::white);
    const auto fallback=alignStereoPolygon(blank,blank,matchAnchors,-9);
    require(fallback.matchedAnchors==0 && fallback.anchors[0]==matchAnchors[0]-QPointF(9,0),"low contrast fallback failed");
    QImage stereoPair(480,180,QImage::Format_RGB32);
    {QPainter p(&stereoPair);p.drawImage(0,0,stereoLeft);p.drawImage(240,0,stereoRight);}
    const QString pairedFile=temp.path()+"/paired.png"; require(stereoPair.save(pairedFile),"paired fixture save failed");
    StereoViewWidget paired; paired.setAutoSave(false); paired.resize(800,400);
    require(paired.loadImage(pairedFile),"paired fixture load failed");
    require(!paired.pairedLassoEnabled(),"paired lasso unexpectedly enabled by default");
    paired.setBrushMode(7); paired.setPairedLassoEnabled(true);
    click(paired,QPoint(120,145));click(paired,QPoint(250,145));click(paired,QPoint(250,230));click(paired,QPoint(120,230));
    paired.closeLasso(); paired.finishLasso();
    const auto pairObjects=paired.selectionObjects();
    require(pairObjects.size()==2 && !pairObjects[0].right && pairObjects[1].right && pairObjects[0].id!=pairObjects[1].id,"paired polygons are not independent per-eye objects");
    for (int i=0; i<4; ++i) require(pairObjects[1].anchors[i]==pairObjects[0].anchors[i]-QPointF(12,0),"paired apply alignment incorrect");
    paired.selectSelection(pairObjects[1].id);paired.setPolygonMoveEnabled(true);
    const int moveIndex=paired.undoStack()->index();
    drag(paired,QPoint(570,200),QPoint(600,215));
    const auto shifted=paired.selectionObjects();
    require(shifted[0].anchors==pairObjects[0].anchors && shifted[1].id==pairObjects[1].id && shifted.size()==2,"whole polygon move modifies its paired partner or identity");
    for (int i=0;i<4;++i) require(QLineF(shifted[1].anchors[i],pairObjects[1].anchors[i]+QPointF(20,10)).length()<0.01,"whole polygon move does not translate all anchors equally");
    require(shifted[1].path==QTransform::fromTranslate(20,10).map(pairObjects[1].path) && shifted[1].magnetic==pairObjects[1].magnetic,"polygon move changes shape or magnetic flags");
    require(paired.undoStack()->index()==moveIndex+1,"polygon drag creates multiple undo commands");
    require(paired.saveProject(),"moved polygon save failed");
    StereoViewWidget movedReload;movedReload.setAutoSave(false);
    require(movedReload.loadProject(temp.path()+"/paired.msk") && movedReload.selectionObjects()[1].anchors==shifted[1].anchors && movedReload.selectionObjects()[1].path==shifted[1].path && movedReload.selectionObjects()[0].anchors==pairObjects[0].anchors,"moved polygon JSON roundtrip failed");
    paired.undoStack()->undo();require(paired.selectionObjects()[1].anchors==pairObjects[1].anchors,"polygon move undo failed");
    paired.undoStack()->redo();require(paired.selectionObjects()[1].anchors==shifted[1].anchors,"polygon move redo failed");paired.undoStack()->undo();
    drag(paired,QPoint(570,200),QPoint(600,215),Qt::ShiftModifier);
    require(QLineF(paired.selectionObjects()[1].anchors[0],pairObjects[1].anchors[0]+QPointF(20,0)).length()<0.01,"Shift-drag does not constrain polygon horizontally");paired.undoStack()->undo();
    drag(paired,QPoint(170,200),QPoint(200,215));require(paired.selectionObjects()[1].anchors==pairObjects[1].anchors && !paired.hasLasso(),"wrong-eye drag changes polygon or draws outline");
    drag(paired,QPoint(570,200),QPoint(570,200));require(paired.undoStack()->index()==moveIndex,"zero-length drag adds undo command");
    drag(paired,QPoint(570,200),QPoint(1000,500));
    const auto bounded=paired.selectionObjects()[1].path.boundingRect();
    require(bounded.right()<=239.01 && bounded.bottom()<=179.01 && bounded.left()>=0 && bounded.top()>=0,"polygon move clips shape outside image");paired.undoStack()->undo();
    QMouseEvent cancelPress(QEvent::MouseButtonPress,QPointF(570,200),QPointF(570,200),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent cancelMove(QEvent::MouseMove,QPointF(600,215),QPointF(600,215),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&paired,&cancelPress);QApplication::sendEvent(&paired,&cancelMove);
    QKeyEvent cancelDrag(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(&paired,&cancelDrag);
    require(paired.selectionObjects()[1].anchors==pairObjects[1].anchors && paired.undoStack()->index()==moveIndex,"Escape does not cancel polygon move");
    paired.setSwapSides(true);drag(paired,QPoint(170,200),QPoint(200,215));
    require(QLineF(paired.selectionObjects()[1].anchors[0],pairObjects[1].anchors[0]+QPointF(20,10)).length()<0.01,"swapped polygon drag uses wrong physical eye");paired.undoStack()->undo();paired.setSwapSides(false);
    paired.setZoom(2);drag(paired,QPoint(550,200),QPoint(580,215));
    require(QLineF(paired.selectionObjects()[1].anchors[0],pairObjects[1].anchors[0]+QPointF(10,5)).length()<0.01,"zoomed polygon movement uses screen instead of image coordinates");paired.undoStack()->undo();paired.fitView();
    paired.setPolygonMoveEnabled(false);
    paired.undoStack()->undo();require(paired.selectionObjects().isEmpty(),"paired undo leaves one eye behind");
    paired.undoStack()->redo();require(paired.selectionObjects().size()==2,"paired redo failed");
    require(paired.editSelection(pairObjects[0].id),"paired object cannot reopen");
    paired.finishLasso();require(paired.selectionObjects().size()==2 && paired.selectionObjects()[1].anchors==pairObjects[1].anchors,"editing paired polygon duplicated or changed its former partner");
    paired.setLassoSubtract(true); paired.setSwapSides(true);
    click(paired,QPoint(120,145));click(paired,QPoint(250,145));click(paired,QPoint(250,230));click(paired,QPoint(120,230));
    paired.finishLasso();
    require(paired.selectionObjects().size()==4 && paired.selectionObjects()[2].right && !paired.selectionObjects()[3].right && paired.selectionObjects()[2].subtract && paired.selectionObjects()[3].subtract,"swapped reverse-eye paired subtraction failed");
    require(paired.saveProject(),"paired project save failed");
    StereoViewWidget pairReload; pairReload.setAutoSave(false);
    require(pairReload.loadProject(temp.path()+"/paired.msk") && pairReload.selectionObjects().size()==4 && pairReload.selectionObjects()[3].anchors==paired.selectionObjects()[3].anchors,"paired object roundtrip failed");
    // Navigation changes the shared pan only, preserving an unfinished polygon.
    paired.setLassoSubtract(false); paired.setSwapSides(false);
    click(paired,QPoint(120,145));click(paired,QPoint(250,145));click(paired,QPoint(250,230));paired.closeLasso();
    paired.setZoom(2);
    const auto beforeNavigation=paired.selectionObjects();
    QKeyEvent down(QEvent::KeyPress,Qt::Key_Down,Qt::NoModifier);
    QApplication::sendEvent(&paired,&down);
    require(paired.panOffset().y()<0 && paired.panOffset().x()==0 && paired.lassoClosed(),"arrow scroll loses outline or moves wrong direction");
    const double ordinaryStep=-paired.panOffset().y();
    QKeyEvent up(QEvent::KeyPress,Qt::Key_Up,Qt::NoModifier); QApplication::sendEvent(&paired,&up);
    require(paired.panOffset().isNull(),"opposite arrow does not reverse scrolling");
    QKeyEvent fastRight(QEvent::KeyPress,Qt::Key_Right,Qt::ShiftModifier); QApplication::sendEvent(&paired,&fastRight);
    require(qAbs(paired.panOffset().x()+ordinaryStep*3)<0.01,"fast horizontal scrolling is not aligned");
    paired.fitView();require(paired.panOffset().isNull() && paired.zoom()==1 && paired.lassoClosed(),"fit view loses polygon");
    paired.finishLasso();require(paired.selectionObjects().size()==beforeNavigation.size()+2,"scrolling changes pending polygon apply");
    paired.setPairedLassoEnabled(false);
    click(paired,QPoint(120,145));click(paired,QPoint(250,145));click(paired,QPoint(250,230)); paired.finishLasso();
    require(paired.selectionObjects().size()==beforeNavigation.size()+3,"disabled pairing still duplicates polygon");
    paired.setPairedLassoEnabled(true); paired.setBrushMode(8);
    click(paired,QPoint(120,145));click(paired,QPoint(250,145));click(paired,QPoint(250,230)); paired.finishLasso();
    require(paired.selectionObjects().size()==beforeNavigation.size()+5 && paired.selectionObjects().last().magnetic.contains(true) && !paired.selectionObjects().last().path.isEmpty(),"smart paired lasso lost magnetic segments");
    const auto smartMoving=paired.selectionObjects().last();
    paired.selectSelection(smartMoving.id);paired.setPolygonMoveEnabled(true);
    const QPoint smartStart(qRound(419.5+smartMoving.anchors[0].x()*1.5),qRound(64.5+smartMoving.anchors[0].y()*1.5));
    drag(paired,smartStart,smartStart+QPoint(30,15));
    const auto movedSmart=paired.selectionObjects().last();
    require(movedSmart.path.elementCount()==smartMoving.path.elementCount(),"moving smart polygon changes its contour point count");
    for (int i=0;i<smartMoving.path.elementCount();++i) {
        const auto before=smartMoving.path.elementAt(i),after=movedSmart.path.elementAt(i);
        require(before.type==after.type && QLineF(QPointF(after.x,after.y),QPointF(before.x+20,before.y+10)).length()<0.001,"moving smart polygon retraces rather than translating its shape");
    }
    paired.undoStack()->undo();paired.setPolygonMoveEnabled(false);
    QImage blankPair(480,180,QImage::Format_RGB32); blankPair.fill(Qt::white);
    const QString fallbackFile=temp.path()+"/fallback.png"; require(blankPair.save(fallbackFile),"fallback fixture save failed");
    StereoViewWidget fallbackView;fallbackView.setAutoSave(false);fallbackView.resize(800,400);
    require(fallbackView.loadImage(fallbackFile),"fallback fixture load failed");
    for (const auto &point:QVector<QPointF>{{30,20},{210,20},{210,160},{30,160}}) fallbackView.addPoint(point,9);
    fallbackView.setBrushMode(7);fallbackView.setPairedLassoEnabled(true);
    click(fallbackView,QPoint(120,145));click(fallbackView,QPoint(250,145));click(fallbackView,QPoint(250,230));fallbackView.finishLasso();
    require(fallbackView.selectionObjects()[1].anchors[0]==fallbackView.selectionObjects()[0].anchors[0]-QPointF(9,0),"frame disparity fallback failed");
    std::cout<<"Paired stereo lasso, reverse/Swap, independent editing, Undo/Redo, JSON and aligned arrow scrolling: PASS\n";
    // Applied polygon objects remain independent and editable after project reload.
    const QString objectsFile=temp.path()+"/objects.png"; require(sbs.save(objectsFile),"objects fixture save failed");
    StereoViewWidget objects; objects.setAutoSave(false); objects.resize(800,400);
    require(objects.loadImage(objectsFile),"objects load failed"); objects.setBrushMode(7);
    click(objects,QPoint(120,145));click(objects,QPoint(250,145));click(objects,QPoint(250,230));click(objects,QPoint(120,230));
    objects.closeLasso(); objects.finishLasso();
    click(objects,QPoint(260,245));click(objects,QPoint(320,245));click(objects,QPoint(320,300));click(objects,QPoint(260,300));
    objects.closeLasso(); objects.finishLasso();
    require(objects.selectionObjects().size()==2,"polygons merged into one object");
    const QString firstId=objects.selectionObjects()[0].id;
    const QString secondId=objects.selectionObjects()[1].id;
    const QImage originalObjects=objects.revealMask(false);
    objects.setSelectionVisible(firstId,false);
    require(qAlpha(objects.revealMask(false).pixel(90,80))==0 && qAlpha(objects.revealMask(false).pixel(175,140))==255,"visibility affected unrelated object");
    objects.undoStack()->undo();require(objects.revealMask(false)==originalObjects,"visibility undo failed");
    objects.renameSelection(firstId,"Left hand"); objects.setSelectionVisible(secondId,false);
    require(objects.saveProject(),"object project save failed");
    QFile objectJson(temp.path()+"/objects.msk"); require(objectJson.open(QIODevice::ReadOnly),"object JSON read failed");
    auto savedObjectJson=QJsonDocument::fromJson(objectJson.readAll()).object(); objectJson.close();
    const auto savedObjects=savedObjectJson["selectionObjects"].toObject()["objects"].toArray();
    require(savedObjects.size()==2 && savedObjects[0].toObject()["anchors"].toArray().size()==4,"polygon coordinates not saved as JSON");
    StereoViewWidget objectReload; objectReload.setAutoSave(false); objectReload.resize(800,400);
    require(objectReload.loadProject(temp.path()+"/objects.msk"),"object project reload failed");
    require(objectReload.selectionObjects()[0].id==firstId && objectReload.selectionObjects()[0].name=="Left hand" && !objectReload.selectionObjects()[1].visible,"object identity/name/visibility lost");
    require(objectReload.editSelection(firstId) && objectReload.lassoClosed(),"saved polygon cannot reopen for editing");
    QMouseEvent objectPress(QEvent::MouseButtonPress,QPointF(120,145),QPointF(120,145),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent objectMove(QEvent::MouseMove,QPointF(90,120),QPointF(90,120),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent objectRelease(QEvent::MouseButtonRelease,QPointF(90,120),QPointF(90,120),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    const auto oldAnchors=objectReload.selectionObjects()[0].anchors;
    QApplication::sendEvent(&objectReload,&objectPress); QApplication::sendEvent(&objectReload,&objectMove); QApplication::sendEvent(&objectReload,&objectRelease);
    objectReload.finishLasso();
    require(objectReload.selectionObjects().size()==2 && objectReload.selectionObjects()[0].id==firstId && objectReload.selectionObjects()[0].anchors!=oldAnchors,"editing duplicated polygon or lost changes");
    objectReload.undoStack()->undo();require(objectReload.selectionObjects()[0].anchors==oldAnchors,"polygon edit undo failed");
    objectReload.deleteSelection(firstId); require(objectReload.selectionObjects().size()==1,"delete object failed");
    objectReload.undoStack()->undo();require(objectReload.selectionObjects().size()==2,"delete object undo failed");
    objectReload.moveSelection(secondId,-1);
    require(objectReload.selectionObjects()[0].id==secondId,"move up failed");
    require(objectReload.saveProject(),"reordered project save failed");
    StereoViewWidget orderedReload; orderedReload.setAutoSave(false);
    require(orderedReload.loadProject(temp.path()+"/objects.msk") && orderedReload.selectionObjects()[0].id==secondId,"order not persisted");
    objectReload.undoStack()->undo();
    require(objectReload.selectionObjects()[0].id==firstId,"reorder undo failed");
    require(objectReload.editSelection(firstId),"cannot reopen polygon for insertion");
    QMouseEvent insertPress(QEvent::MouseButtonPress,QPointF(185,145),QPointF(185,145),Qt::LeftButton,Qt::LeftButton,Qt::ShiftModifier);
    QMouseEvent insertRelease(QEvent::MouseButtonRelease,QPointF(185,145),QPointF(185,145),Qt::LeftButton,Qt::NoButton,Qt::ShiftModifier);
    QApplication::sendEvent(&objectReload,&insertPress); QApplication::sendEvent(&objectReload,&insertRelease);
    objectReload.finishLasso();
    require(objectReload.selectionObjects()[0].anchors.size()==5,"edge insertion failed");
    objectReload.undoStack()->undo();
    require(objectReload.selectionObjects()[0].anchors.size()==4,"point insertion undo failed");
    require(objectReload.editSelection(firstId),"cannot reopen polygon for closing-edge insertion");
    QMouseEvent closingPress(QEvent::MouseButtonPress,QPointF(120,185),QPointF(120,185),Qt::LeftButton,Qt::LeftButton,Qt::ShiftModifier);
    QMouseEvent closingRelease(QEvent::MouseButtonRelease,QPointF(120,185),QPointF(120,185),Qt::LeftButton,Qt::NoButton,Qt::ShiftModifier);
    QApplication::sendEvent(&objectReload,&closingPress); QApplication::sendEvent(&objectReload,&closingRelease);
    objectReload.finishLasso();
    require(objectReload.selectionObjects()[0].anchors.size()==5,"closing-edge insertion failed");
    objectReload.undoStack()->undo();
    const auto beforeToggle=objectReload.selectionObjects()[0];
    const QImage otherEyeBefore=objectReload.openingMask(true);
    objectReload.toggleSelectionOperation(firstId);
    const auto afterToggle=objectReload.selectionObjects()[0];
    require(afterToggle.subtract && afterToggle.id==beforeToggle.id && afterToggle.anchors==beforeToggle.anchors && afterToggle.path==beforeToggle.path && afterToggle.name==beforeToggle.name && afterToggle.visible==beforeToggle.visible,"mode switch loses polygon metadata");
    require(qAlpha(objectReload.openingMask(false).pixel(90,80))==0 && objectReload.openingMask(true)==otherEyeBefore,"mode switch does not change mask or affects other eye");
    require(objectReload.saveProject(),"mode switch save failed");
    StereoViewWidget toggledReload;toggledReload.setAutoSave(false);
    require(toggledReload.loadProject(temp.path()+"/objects.msk") && toggledReload.selectionObjects()[0].subtract,"mode switch not persisted");
    objectReload.undoStack()->undo();require(!objectReload.selectionObjects()[0].subtract,"mode switch undo failed");
    objectReload.undoStack()->redo();require(objectReload.selectionObjects()[0].subtract,"mode switch redo failed");
    objectReload.toggleSelectionOperation(firstId);require(!objectReload.selectionObjects()[0].subtract,"Subtract to Add switch failed");
    require(objectReload.editSelection(firstId),"mode switch guard fixture edit failed");
    objectReload.toggleSelectionOperation(firstId);
    require(objectReload.lassoClosed() && !objectReload.selectionObjects()[0].subtract,"mode switch loses pending edits");objectReload.cancelLasso();
    objectReload.toggleSelectionOperation(secondId);require(objectReload.selectionObjects()[1].subtract && !objectReload.selectionObjects()[1].visible,"hidden object mode switch enables object");
    // Arbitrary point removal keeps the polygon closed and remains an edit of the same object.
    const QString pointsFile=temp.path()+"/point-edit.png";
    require(sbs.save(pointsFile),"point removal fixture save failed");
    for (int mode: {7,8}) for (int removed: {0,1,3}) {
        StereoViewWidget pointEdit; pointEdit.setAutoSave(false); pointEdit.resize(800,400);
        require(pointEdit.loadImage(pointsFile),"point removal fixture load failed"); pointEdit.setBrushMode(mode);
        const QVector<QPoint> clicks={{120,145},{250,145},{250,230},{120,230}};
        for (const auto &point:clicks) click(pointEdit,point);
        QKeyEvent closeKey(QEvent::KeyPress,Qt::Key_C,Qt::NoModifier); QApplication::sendEvent(&pointEdit,&closeKey);
        require(pointEdit.lassoClosed() && pointEdit.selectionObjects().isEmpty(),"C does not close without applying");
        pointEdit.finishLasso(); const auto original=pointEdit.selectionObjects()[0];
        require(pointEdit.editSelection(original.id),"cannot edit polygon for removal");
        click(pointEdit,QPoint(520,145),Qt::ControlModifier);
        require(pointEdit.lassoClosed() && pointEdit.selectionObjects().size()==1,"Ctrl-click in other eye commits polygon");
        click(pointEdit,QPoint(185,180),Qt::ControlModifier);
        require(pointEdit.lassoClosed() && pointEdit.selectionObjects().size()==1,"Ctrl-click away from point commits polygon");
        click(pointEdit,clicks[removed],Qt::ControlModifier);
        require(pointEdit.lassoClosed(),"point removal opens closed polygon"); pointEdit.finishLasso();
        auto expected=original.anchors; expected.removeAt(removed);
        require(pointEdit.selectionObjects().size()==1 && pointEdit.selectionObjects()[0].id==original.id && pointEdit.selectionObjects()[0].anchors==expected,"wrong polygon point removed");
        require(pointEdit.selectionObjects()[0].magnetic.size()==2 && !pointEdit.selectionObjects()[0].path.isEmpty(),"point removal corrupts segments");
        require(pointEdit.editSelection(original.id),"cannot reopen triangle");
        click(pointEdit,clicks[removed==0 ? 1 : 0],Qt::ControlModifier); pointEdit.finishLasso();
        require(pointEdit.selectionObjects()[0].anchors.size()==3,"point removal allows invalid closed polygon");
        pointEdit.undoStack()->undo(); pointEdit.undoStack()->undo();
        require(pointEdit.selectionObjects()[0].anchors==original.anchors && pointEdit.selectionObjects()[0].path==original.path,"point removal undo failed");
    }
    StereoViewWidget openPoints;openPoints.setAutoSave(false);openPoints.resize(800,400);
    require(openPoints.loadImage(pointsFile),"open removal fixture load failed");openPoints.setBrushMode(7);
    click(openPoints,QPoint(120,145));click(openPoints,QPoint(250,145));click(openPoints,QPoint(250,230));click(openPoints,QPoint(120,230));
    click(openPoints,QPoint(250,145),Qt::ControlModifier);
    require(openPoints.hasLasso() && !openPoints.lassoClosed(),"open point removal closes polygon");
    QKeyEvent enterClose(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier); QApplication::sendEvent(&openPoints,&enterClose);
    require(openPoints.lassoClosed(),"Enter closure regressed");openPoints.finishLasso();
    require(openPoints.selectionObjects()[0].anchors.size()==3,"open point removal failed");
    require(openPoints.editSelection(openPoints.selectionObjects()[0].id),"keypad fixture reopen failed");
    openPoints.removeLassoSegment();click(openPoints,QPoint(120,230));
    QKeyEvent keypadClose(QEvent::KeyPress,Qt::Key_Enter,Qt::KeypadModifier);QApplication::sendEvent(&openPoints,&keypadClose);
    require(openPoints.lassoClosed(),"keypad Enter does not close outline");
    QKeyEvent applyKey(QEvent::KeyPress,Qt::Key_Return,Qt::ControlModifier); QApplication::sendEvent(&openPoints,&applyKey);
    require(!openPoints.hasLasso() && openPoints.selectionObjects().size()==1,"Ctrl+Enter does not apply reopened polygon");
    click(openPoints,QPoint(120,145));click(openPoints,QPoint(250,145));click(openPoints,QPoint(250,230));
    QKeyEvent applyKeypad(QEvent::KeyPress,Qt::Key_Enter,Qt::ControlModifier|Qt::KeypadModifier); QApplication::sendEvent(&openPoints,&applyKeypad);
    require(!openPoints.hasLasso() && openPoints.selectionObjects().size()==2,"Ctrl+keypad Enter does not close and apply open polygon");
    openPoints.undoStack()->undo();require(openPoints.selectionObjects().size()==1,"shortcut apply undo failed");
    click(openPoints,QPoint(120,145));click(openPoints,QPoint(250,145));QApplication::sendEvent(&openPoints,&applyKey);
    require(openPoints.hasLasso() && openPoints.selectionObjects().size()==1,"shortcut commits invalid two-point outline");openPoints.cancelLasso();
    std::cout<<"Ctrl-click point removal, first/middle/last, magnetic segments, triangle guard, open outline, C/Enter closure and Undo: PASS\n";
    // Older projects retain flattened masks as imported, hideable groups.
    savedObjectJson.remove("selectionObjects");
    require(objectJson.open(QIODevice::WriteOnly|QIODevice::Truncate),"legacy fixture write failed");
    objectJson.write(QJsonDocument(savedObjectJson).toJson()); objectJson.close();
    StereoViewWidget legacyObjects;legacyObjects.setAutoSave(false);
    require(legacyObjects.loadProject(temp.path()+"/objects.msk"),"legacy selection import failed");
    require(!legacyObjects.selectionObjects().isEmpty() && legacyObjects.selectionObjects()[0].anchors.isEmpty(),"legacy masks claimed reconstructed vertices");
    require(legacyObjects.revealMask(false).convertToFormat(QImage::Format_ARGB32_Premultiplied)==objects.revealMask(false),"legacy migration changed mask appearance");
    std::cout<<"Selection objects, visibility, rename, JSON roundtrip, polygon edit/delete undo and legacy migration: PASS\n";
    // Preview pixels must follow color/opacity and per-eye selection, regardless of editing tool.
    QImage whitePair(480,180,QImage::Format_RGB32); whitePair.fill(Qt::white);
    const QString previewFile=temp.path()+"/preview.png";
    require(whitePair.save(previewFile),"preview fixture save failed");
    StereoViewWidget preview; preview.setAutoSave(false); preview.resize(800,400);
    require(preview.loadImage(previewFile),"preview fixture load failed");
    for (const QPointF &point : QVector<QPointF>{{80,50},{160,50},{160,130},{80,130}}) preview.addPoint(point,0);
    preview.setMaskSettings(Qt::red,0.5f); preview.setBrushMode(7);
    require(preview.maskPreviewEnabled(),"mask preview not enabled by default");
    auto renderPreview=[&] { QImage screen(preview.size(),QImage::Format_RGB32); preview.render(&screen); return screen; };
    auto screen=renderPreview();
    require(!preview.cleanPreviewEnabled(),"clean preview unexpectedly active by default");
    const auto framePointWithMarks=screen.pixelColor(140,140);
    preview.setCleanPreviewEnabled(true); screen=renderPreview();
    require(framePointWithMarks.green()<100 && screen.pixelColor(140,140).green()>120,"clean preview does not hide frame points");
    require(screen.pixelColor(100,199).green()>120 && screen.pixelColor(100,199).green()<135,"clean preview changes mask opacity");
    preview.setCleanPreviewEnabled(false);screen=renderPreview();
    auto exterior=screen.pixelColor(100,199);
    require(exterior.red()>250 && exterior.green()>120 && exterior.green()<135,"preview ignores mask color/opacity in lasso mode");
    require(screen.pixelColor(199,199)==QColor(Qt::white),"frame opening is masked");
    QImage selected(240,180,QImage::Format_ARGB32_Premultiplied); selected.fill(Qt::transparent);
    {QPainter p(&selected);p.fillRect(QRect(20,70,20,40),Qt::white);}
    preview.applyReveal(false,selected); screen=renderPreview();
    require(screen.pixelColor(64,199)==QColor(Qt::white),"selected foreground is masked or tinted in preview");
    require(screen.pixelColor(464,199).green()<135,"left selection wrongly clears right-eye mask");
    preview.setMaskPreviewEnabled(false); screen=renderPreview();
    require(screen.pixelColor(100,199)==QColor(Qt::white),"preview toggle does not restore source photo");
    preview.setCleanPreviewEnabled(true);screen=renderPreview();
    require(screen.pixelColor(100,199).green()<135 && !preview.maskPreviewEnabled(),"clean preview does not show mask independently or changes preview setting");
    const auto cleanMask=preview.openingMask(false);
    click(preview,QPoint(100,199));require(!preview.hasLasso() && preview.openingMask(false)==cleanMask,"clean preview click edits invisible geometry");
    preview.setCleanPreviewEnabled(false);
    preview.setMaskPreviewEnabled(true); preview.setSwapSides(true); screen=renderPreview();
    require(screen.pixelColor(464,199)==QColor(Qt::white) && screen.pixelColor(64,199).green()<135,"swapped preview selection maps to wrong eye");
    preview.setSwapSides(false); preview.setAnaglyphMode(true); screen=renderPreview();
    require(screen.pixelColor(219,199).red()>250 && screen.pixelColor(219,199).green()<135,"anaglyph loses per-eye selection");
    preview.setMaskPreviewEnabled(false); screen=renderPreview();
    require(screen.pixelColor(219,199)==QColor(Qt::white),"anaglyph ignores preview toggle");
    preview.setAnaglyphMode(false); preview.setMaskPreviewEnabled(true);
    SelectionState stack;
    SelectionObject hole; hole.id="hole"; hole.subtract=true; hole.path.addRect(QRectF(100,70,40,40));
    SelectionObject restore; restore.id="restore"; restore.path.addRect(QRectF(110,80,20,20));
    stack.objects={hole,restore}; preview.restoreSelectionState(stack);
    require(qAlpha(preview.openingMask(false).pixel(105,85))==0 && qAlpha(preview.openingMask(false).pixel(120,90))==255,"ordered frame subtraction/addition failed");
    require(qAlpha(preview.openingMask(true).pixel(105,85))==255,"subtraction affects wrong eye");
    preview.moveSelection("restore",-1);
    require(qAlpha(preview.openingMask(false).pixel(120,90))==0,"reorder did not change hole");
    screen=renderPreview(); require(screen.pixelColor(199,199).green()<135,"preview does not show hole in frame");
    preview.setMaskSettings(Qt::blue,0.5f); preview.setAnaglyphMode(true); screen=renderPreview();
    require(screen.pixelColor(399,199).red()<135 && screen.pixelColor(399,199).green()>250,"anaglyph hole or eye isolation failed");
    preview.setAnaglyphMode(false);
    const QString holeExport=temp.path()+"/hole.png";
    require(StereoViewWidget::saveCutout(holeExport,preview.leftImage(),preview.rightImage(),preview.points(),preview.openingMask(false),preview.openingMask(true),0,0,0,0,Qt::black,true),"ordered export failed");
    require(qAlpha(QImage(holeExport).pixel(120,90))==0,"export does not preserve hole");
    preview.undoStack()->undo(); require(qAlpha(preview.openingMask(false).pixel(120,90))==255,"ordered mask undo failed");
    preview.setSelectionVisible("hole",false); require(qAlpha(preview.openingMask(false).pixel(105,85))==255,"hidden subtraction still applied");
    preview.setMaskSettings(Qt::red,0.5f);preview.setCleanPreviewEnabled(true);
    preview.selectSelection("restore");screen=renderPreview();
    preview.selectSelection("hole");require(renderPreview()==screen,"clean preview still shows selected-object outline");
    preview.setCleanPreviewEnabled(false);preview.setBrushMode(7);
    click(preview,QPoint(120,145));click(preview,QPoint(250,145));click(preview,QPoint(250,230));preview.closeLasso();
    const QImage pendingOpening=preview.openingMask(false);const int pendingObjects=preview.selectionObjects().size();
    preview.setCleanPreviewEnabled(true);screen=renderPreview();
    require(preview.lassoClosed() && preview.selectionObjects().size()==pendingObjects && preview.openingMask(false)==pendingOpening,"clean preview applies or discards unfinished outline");
    preview.setCleanPreviewEnabled(false);require(preview.lassoClosed() && renderPreview()!=screen,"clean preview does not restore unfinished outline marks");preview.cancelLasso();
    preview.setAnaglyphMode(true);preview.setMaskPreviewEnabled(false);preview.setCleanPreviewEnabled(true);screen=renderPreview();
    require(screen.pixelColor(219,199).green()<135,"clean anaglyph preview loses mask");
    preview.setCleanPreviewEnabled(false);preview.setAnaglyphMode(false);
    std::cout<<"Default mask preview, color/opacity, reveal, eye isolation, Swap and anaglyph: PASS\n";
    // Match-preview export retains mask color/opacity instead of flattening to a cutout background.
    QImage exportPhoto(240,180,QImage::Format_RGB32);exportPhoto.fill(QColor(20,80,140));
    QImage exportOpening(exportPhoto.size(),QImage::Format_ARGB32_Premultiplied);exportOpening.fill(Qt::transparent);
    {QPainter p(&exportOpening);p.fillRect(QRect(80,50,80,80),Qt::white);}
    const auto blended=StereoViewWidget::maskedPreviewEye(exportPhoto,exportOpening,Qt::white,0.95f);
    const auto exterior95=blended.pixelColor(20,20);
    require(qAbs(exterior95.red()-243)<=1 && qAbs(exterior95.green()-246)<=1 && qAbs(exterior95.blue()-249)<=1 && exterior95.alpha()==255,"95 percent white mask export loses original background or color");
    require(blended.pixelColor(120,90)==exportPhoto.pixelColor(120,90),"match preview changes opening pixels");
    require(StereoViewWidget::maskedPreviewEye(exportPhoto,exportOpening,Qt::white,0).pixelColor(20,20)==exportPhoto.pixelColor(20,20),"zero opacity export is not source image");
    require(StereoViewWidget::maskedPreviewEye(exportPhoto,exportOpening,Qt::white,1).pixelColor(20,20)==QColor(Qt::white),"full opacity export ignores mask color");
    const QString appearanceFile=temp.path()+"/appearance.png";
    require(StereoViewWidget::saveMaskedPreview(appearanceFile,exportPhoto,exportPhoto,exportOpening,exportOpening,Qt::white,0.95f,3,4,7,Qt::black),"match preview PNG export failed");
    QImage appearance(appearanceFile);
    require(appearance.size()==QSize(493,188) && appearance.pixelColor(23,24)==exterior95 && appearance.pixelColor(270,24)==exterior95 && appearance.pixelColor(0,0)==QColor(Qt::black),"match preview export placement/color wrong");
    require(StereoViewWidget::saveMaskedPreview(temp.path()+"/appearance.jpg",exportPhoto,exportPhoto,exportOpening,exportOpening,Qt::white,0.95f,0,0,0,Qt::black),"match preview JPEG export failed");
    require(qAlpha(StereoViewWidget::cutoutEye(exportPhoto,{},false,exportOpening,0,Qt::black,true).pixel(20,20))==0,"transparent cutout export regressed");
    require(StereoViewWidget::cutoutEye(exportPhoto,{},false,exportOpening,1,Qt::black,true).pixelColor(20,20)==QColor(Qt::black),"black cutout export regressed");
    StereoViewWidget exportSettings;exportSettings.setAutoSave(false);
    require(exportSettings.loadImage(pointsFile) && exportSettings.cutoutBackground()==4,"new image does not default to match preview");
    require(exportSettings.saveProject(),"preview export settings save failed");
    StereoViewWidget settingsReload;settingsReload.setAutoSave(false);
    require(settingsReload.loadProject(temp.path()+"/point-edit.msk") && settingsReload.cutoutBackground()==4,"match preview setting not persisted");
    exportSettings.setCutoutBackground(0);require(exportSettings.saveProject(),"transparent settings save failed");
    require(settingsReload.loadProject(temp.path()+"/point-edit.msk") && settingsReload.cutoutBackground()==0,"saved transparent choice was overwritten");
    std::cout<<"Match-preview export opacity/color, opening, PNG/JPEG, padding, defaults and saved cutout choices: PASS\n";
    StereoViewWidget consistency;consistency.setAutoSave(false);consistency.resize(800,400);
    require(consistency.loadImage(previewFile),"mode consistency fixture load failed");
    for (const auto &point:QVector<QPointF>{{80,50},{160,50},{160,130},{80,130}}) consistency.addPoint(point,0);
    consistency.setMaskSettings(QColor(80,100,140),0.95f);consistency.setCleanPreviewEnabled(true);
    SelectionState modeObjects;
    SelectionObject addOutside;addOutside.id="add";addOutside.path.addRect(QRectF(20,70,20,40));modeObjects.objects.append(addOutside);
    SelectionObject cutInside;cutInside.id="cut";cutInside.subtract=true;cutInside.path.addRect(QRectF(110,80,20,20));modeObjects.objects.append(cutInside);
    consistency.restoreSelectionState(modeObjects);
    auto renderMode=[&] {QImage screen(consistency.size(),QImage::Format_RGB32);consistency.render(&screen);return screen;};
    for (int mode: {0,1,2,3,4}) {
        if (mode==3) consistency.setCutoutColor(QColor(170,40,70));
        else consistency.setCutoutBackground(mode);
        const QImage left=consistency.exportEye(false),right=consistency.exportEye(true);
        const QString modeFile=temp.path()+QString("/mode-%1.png").arg(mode);
        const bool saved=mode==4 ? StereoViewWidget::saveMaskedPreview(modeFile,consistency.leftImage(),consistency.rightImage(),consistency.openingMask(false),consistency.openingMask(true),consistency.maskColor(),consistency.maskOpacity(),0,0,0,Qt::black)
                                : StereoViewWidget::saveCutout(modeFile,consistency.leftImage(),consistency.rightImage(),consistency.points(),consistency.openingMask(false),consistency.openingMask(true),mode,0,0,0,consistency.cutoutColor(),true);
        require(saved,"mode export failed");const QImage exported(modeFile);
        require(exported.copy(0,0,240,180).convertToFormat(left.format())==left && exported.copy(240,0,240,180).convertToFormat(right.format())==right,"per-eye preview/export composition differs");
        const auto modeScreen=renderMode();
        if (mode!=0) {
            require(modeScreen.pixelColor(100,199)==left.pixelColor(53,90),"rendered preview differs from exported background");
            require(modeScreen.pixelColor(199,199)==left.pixelColor(120,90),"rendered subtraction hole differs from export");
            consistency.setAnaglyphMode(true);const auto ana=renderMode();
            require(ana.pixelColor(219,199).red()==left.pixelColor(30,90).red() && ana.pixelColor(219,199).green()==right.pixelColor(30,90).green(),"anaglyph ignores export mode");consistency.setAnaglyphMode(false);
        } else {
            require(qAlpha(left.pixel(53,90))==0 && modeScreen.pixelColor(100,199)!=QColor(Qt::black),"transparent preview is not distinguished from black");
        }
    }
    std::cout<<"All export modes use identical preview/export per-eye compositing, including holes and anaglyph: PASS\n";
    consistency.setCutoutBackground(4);
    const auto cachedEye=consistency.exportEye(false);
    require(consistency.exportEye(false).cacheKey()==cachedEye.cacheKey(),"preview compositing is not cached");
    consistency.setMaskSettings(Qt::red,0.5f);
    require(consistency.exportEye(false).pixelColor(53,90)!=cachedEye.pixelColor(53,90),"preview cache ignores appearance change");
    consistency.setSelectionVisible("cut",false);
    require(consistency.exportEye(false).pixelColor(120,90)==QColor(Qt::white),"preview cache ignores object visibility");
    // Switching from lasso to Freehand must release mouse routing to frame drawing.
    const QString freehandFile = temp.path() + "/freehand.png";
    require(sbs.save(freehandFile), "freehand fixture save failed");
    StereoViewWidget freehand;
    freehand.setAutoSave(false); freehand.resize(800,400);
    require(freehand.loadImage(freehandFile), "freehand fixture load failed");
    freehand.setBrushMode(7); freehand.setFreehandMode(true);
    require(freehand.brushMode()==0, "lasso still owns Freehand input");
    const QPoint start(120,145);
    QMouseEvent fhPress(QEvent::MouseButtonPress,QPointF(start),QPointF(start),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&freehand,&fhPress);
    const QVector<QPoint> stroke={QPoint(160,145),QPoint(200,145),QPoint(250,145),
        QPoint(250,180),QPoint(250,220),QPoint(250,255),QPoint(200,255),
        QPoint(160,255),QPoint(120,255),QPoint(120,220),QPoint(120,180),start};
    for (const QPoint &point : stroke) {
        QMouseEvent move(QEvent::MouseMove,QPointF(point),QPointF(point),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&freehand,&move);
    }
    QMouseEvent fhRelease(QEvent::MouseButtonRelease,QPointF(start),QPointF(start),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(&freehand,&fhRelease);
    require(freehand.points().size()>=3 && freehand.points().size()<stroke.size(), "freehand shape lost or simplification failed");
    const auto drawn=freehand.points();
    freehand.undoStack()->undo(); require(freehand.points().isEmpty(), "freehand undo failed");
    freehand.undoStack()->redo(); require(freehand.points().size()==drawn.size(), "freehand redo failed");
    std::cout<<"Freehand tool switching, simplification and Undo/Redo: PASS\n";
    std::cout<<"Polygon/magnetic lasso, segment Delete/Backspace, Escape, eye isolation and undo: PASS\n";
    std::cout<<"Native algorithm, async ROI isolation, frame/depth preservation, eye isolation, Swap mapping, Undo/Redo, persistence, stale-job rejection and PNG alpha: PASS\n";
 } catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
 return 0;
}
