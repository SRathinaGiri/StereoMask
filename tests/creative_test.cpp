#include "stereoviewwidget.h"
#include "decorations.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPainter>
#include <iostream>
#include <cstdlib>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QtMath>

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << "\n"; std::exit(1); }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir folder;
    QImage source(720, 240, QImage::Format_ARGB32);
    source.fill(Qt::white);
    const QString file = folder.path() + "/source.png";
    source.save(file);

    StereoViewWidget view;
    view.setAutoSave(false);
    require(view.loadImage(file), "load fixture");
    QImage colors(8,3,QImage::Format_RGB32);colors.fill(Qt::black);
    colors.setPixelColor(0,0,QColor(200,30,20));colors.setPixelColor(1,0,QColor(200,30,20));
    colors.setPixelColor(6,0,QColor(200,30,20));colors.setPixelColor(2,0,QColor(210,38,27));
    const auto fuzzy=StereoViewWidget::colorSelectionMask(colors,QPoint(0,0),0,true);
    const auto global=StereoViewWidget::colorSelectionMask(colors,QPoint(0,0),0,false);
    const auto tolerant=StereoViewWidget::colorSelectionMask(colors,QPoint(0,0),12,true);
    require(qAlpha(fuzzy.pixel(0,0)) && qAlpha(fuzzy.pixel(1,0)) && !qAlpha(fuzzy.pixel(6,0)),"fuzzy selection stays connected");
    require(qAlpha(global.pixel(0,0)) && qAlpha(global.pixel(6,0)),"color threshold selects disconnected matches");
    QImage diagonal(2,2,QImage::Format_RGB32);diagonal.fill(Qt::black);
    diagonal.setPixelColor(0,0,Qt::red);diagonal.setPixelColor(1,1,Qt::red);
    require(qAlpha(StereoViewWidget::colorSelectionMask(diagonal,QPoint(0,0),0,true).pixel(1,1))>0,
            "Windows fuzzy selection retains diagonal connectivity");
    require(qAlpha(tolerant.pixel(2,0)),"color tolerance includes nearby RGB values");
    view.applyColorSelection(false,QPoint(10,10),true);
    require(view.selectionObjects().size()==1 && view.selectionObjects()[0].name.startsWith("Fuzzy"),"fuzzy selection becomes History object");
    view.undoStack()->undo();require(view.selectionObjects().isEmpty(),"fuzzy selection undo");
    QVector<MaskPoint> alignment(4);
    alignment[0].pos=QPointF(40,80);alignment[0].disparity=-8;
    alignment[1].pos=QPointF(10,40);alignment[1].disparity=2;
    alignment[2].pos=QPointF(70,100);alignment[2].disparity=14;
    alignment[3].pos=QPointF(90,20);alignment[3].disparity=6;
    auto verifyAlignment=[&](StereoViewWidget::AlignSide side,float expected,bool horizontal) {
        view.undoStack()->clear();view.applyFramePoints(alignment);view.alignSelectedPoints(side);
        for(const auto &point:view.points())require(qAbs((horizontal ? point.pos.x() : point.pos.y())-double(expected))<0.001,"directional alignment extreme");
    };
    verifyAlignment(StereoViewWidget::AlignLeft,10,true);
    verifyAlignment(StereoViewWidget::AlignRight,90,true);
    verifyAlignment(StereoViewWidget::AlignTop,20,false);
    verifyAlignment(StereoViewWidget::AlignBottom,100,false);
    view.undoStack()->clear();view.applyFramePoints(alignment);view.alignSelectedPoints(StereoViewWidget::AlignDepth);
    for(const auto &point:view.points())require(qFuzzyCompare(point.disparity+1,5.0f),"depth alignment uses median disparity");
    view.clearPoints();
    const QRectF bounds(60, 40, 140, 140);
    view.replaceFramePoints(shapePoints(0, bounds, 10));
    require(view.points().size() == 4, "rectangle vertices");
    view.undoStack()->undo();
    require(view.points().isEmpty(), "shape undo");
    view.undoStack()->redo();

    const auto ellipse = shapePoints(2, bounds, 10);
    view.replaceFramePoints(ellipse);
    require(ellipse[0].isCurve && ellipse[3].isCurve && ellipse[2].cp1Disparity == 10,
            "ellipse cubic depth");
    const QImage originalOpening = view.openingMask(false);
    const auto a = ellipse[0], b = ellipse[1];
    const QPointF middle = (a.pos + a.cp1 * 3 + a.cp2 * 3 + b.pos) / 8;
    view.addPoint(middle, 10, 1);
    require(view.points().size() == 5 && view.points()[1].isCurve, "curve point insertion");
    const QImage splitOpening = view.openingMask(false);
    int difference = 0;
    for (int y = 0; y < 240; ++y)
        for (int x = 0; x < 360; ++x)
            difference += qAbs(qAlpha(originalOpening.pixel(x, y)) - qAlpha(splitOpening.pixel(x, y)));
    require(difference < 600, "curve split preserves ellipse");
    view.undoStack()->undo();

    view.replaceFramePoints(shapePoints(0, bounds, 10));
    view.setCutoutBackground(0);
    PictureFrameSettings shadow;
    shadow.shadowEnabled = true;
    shadow.shadowOpacity = 100;
    shadow.shadowDistance = 20;
    shadow.shadowAngle = 0;
    shadow.shadowBlur = 0;
    shadow.enabled = false;
    view.setPictureFrameSettings(shadow);
    auto layer = StereoViewWidget::shadowOverlay(QSize(360, 240), view.points(), false,
                                                  view.openingMask(false), shadow);
    require(qAlpha(layer.pixel(210, 100)) == 255 && qAlpha(layer.pixel(100, 100)) == 0,
            "shadow exterior only");
    {
        QImage gridImage(160,160,QImage::Format_RGB32);gridImage.fill(Qt::white);
        GridSettings grid;grid.enabled=true;grid.horizontal=grid.vertical=1;grid.opacity=100;grid.first=Qt::magenta;
        {QPainter painter(&gridImage);StereoViewWidget::paintAlignmentGrid(painter,QRectF(20,20,100,100),grid);}
        require(gridImage.pixelColor(70,40)==QColor(Qt::magenta) && gridImage.pixelColor(40,70)==QColor(Qt::magenta)
                && gridImage.pixelColor(10,70)==QColor(Qt::white),"grid positions and image clipping");
        grid.twoColors=true;grid.second=Qt::cyan;
        {QPainter painter(&gridImage);StereoViewWidget::paintAlignmentGrid(painter,QRectF(20,20,100,100),grid);}
        int primary=0,secondary=0;for(int y=25;y<65;++y){primary+=gridImage.pixelColor(70,y)==QColor(Qt::magenta);secondary+=gridImage.pixelColor(70,y)==QColor(Qt::cyan);}
        require(primary>0 && secondary>0,"two-color grid retains both contrast colors");
        const auto exported=view.exportEye(false);const int undoCount=view.undoStack()->count();
        view.resize(720,300);view.show();app.processEvents();view.setGridSettings(grid);const auto shown=view.grab().toImage();
        grid.enabled=false;view.setGridSettings(grid);const auto hidden=view.grab().toImage();
        require(shown!=hidden && view.exportEye(false)==exported && view.undoStack()->count()==undoCount,"grid is a view-only overlay, not exported or undoable");
        view.setCleanPreviewEnabled(true);const auto clean=view.grab().toImage();grid.enabled=true;view.setGridSettings(grid);
        require(view.grab().toImage()==clean,"Clean Preview hides alignment grids");
        grid.enabled=false;view.setGridSettings(grid);view.setCleanPreviewEnabled(false);view.hide();
    }
    auto outline=shadow;outline.outlineEnabled=true;outline.outlineOffset=6;
    outline.outlineInnerWidth=3;outline.outlineMiddleWidth=4;outline.outlineOuterWidth=2;
    outline.outlineInnerColor=Qt::red;outline.outlineMiddleColor=Qt::green;outline.outlineOuterColor=Qt::blue;
    const auto outlineLeft=StereoViewWidget::outlineFrameOverlay(QSize(360,240),view.points(),false,view.openingMask(false),outline,Qt::black);
    const auto outlineRight=StereoViewWidget::outlineFrameOverlay(QSize(360,240),view.points(),true,view.openingMask(true),outline,Qt::black);
    require(qAlpha(outlineLeft.pixel(203,100))==0 && outlineLeft.pixelColor(207,100)==QColor(Qt::red)
            && outlineLeft.pixelColor(211,100)==QColor(Qt::green) && outlineLeft.pixelColor(214,100)==QColor(Qt::blue)
            && qAlpha(outlineLeft.pixel(217,100))==0,"three outline bands and gap in source pixels");
    require(outlineRight.pixelColor(197,100)==QColor(Qt::red) && qAlpha(outlineLeft.pixel(100,100))==0,"outline follows per-eye disparity outside the frame");
    auto outlineForeground=view.openingMask(false);{QPainter painter(&outlineForeground);painter.fillRect(202,90,16,20,Qt::white);}
    const auto coveredOutline=StereoViewWidget::outlineFrameOverlay(QSize(360,240),view.points(),false,outlineForeground,outline,Qt::black);
    require(qAlpha(coveredOutline.pixel(207,100))==0 && qAlpha(coveredOutline.pixel(214,100))==0,"foreground protrusions cover outline bands");
    outline.outlineDisplay=false;view.setPictureFrameSettings(outline);
    require(view.exportEye(false).pixelColor(207,100)==QColor(Qt::red) && view.exportEye(false,true,true).pixelColor(207,100)!=QColor(Qt::red),"editor-only visibility must not remove exported outline");
    view.undoStack()->undo();require(!view.pictureFrameSettings().outlineEnabled,"outline settings undo");
    view.undoStack()->redo();require(view.pictureFrameSettings()==outline,"outline settings redo");
    auto changed=outline;changed.outlineInnerColor=Qt::yellow;view.setPictureFrameSettings(changed);
    require(view.exportEye(false).pixelColor(207,100)==QColor(Qt::yellow),"outline color changes invalidate composed cache");
    view.undoStack()->undo();

    StereoText text;
    changed=outline;changed.outlineMiddleWidth=0;
    const auto omitted=StereoViewWidget::outlineFrameOverlay(QSize(360,240),view.points(),false,view.openingMask(false),changed,Qt::black);
    require(omitted.pixelColor(210,100)==QColor(Qt::blue) && qAlpha(omitted.pixel(214,100))==0,"zero width omits an outline band");
    changed=outline;changed.outlineInnerBackground=true;view.setPictureFrameSettings(changed);
    view.setMaskSettings(Qt::magenta,1.0f);require(view.exportEye(false).pixelColor(207,100)==QColor(Qt::magenta),"outline band can follow mask color");
    view.setMaskSettings(Qt::cyan,1.0f);require(view.exportEye(false).pixelColor(207,100)==QColor(Qt::cyan),"mask color invalidates cached outline");
    view.undoStack()->undo();
    view.replaceFramePoints(shapePoints(2,bounds,10));
    const auto curvedOutline=StereoViewWidget::outlineFrameOverlay(QSize(360,240),view.points(),false,view.openingMask(false),outline,Qt::black);
    require(qAlpha(curvedOutline.pixel(130,32))>0 && qAlpha(curvedOutline.pixel(54,34))==0,"outline follows curved frame rather than bounding rectangle");
    view.undoStack()->undo();
    text.id = "fixture";
    text.rect = QRectF(20, 20, 100, 80);
    text.tail = QPointF(40, 140);
    text.style = 2;
    text.text.clear();
    text.fill = Qt::red;
    text.outline = 0;
    text.depth = 15;
    const auto left = textOverlay(QSize(360, 240), {text}, false);
    const auto right = textOverlay(QSize(360, 240), {text}, true);
    require(left.pixelColor(70, 60) == QColor(Qt::red) && qAlpha(right.pixel(110, 60)) == 0,
            "bubble color and stereo placement");
    const auto decoded = textFromJson(textToJson(text), QSize(360, 240));
    require(decoded.id == text.id && decoded.depth == 15 && decoded.tail == text.tail,
            "text JSON roundtrip");
    StereoText sample;sample.id="relief";sample.text="MASK";sample.rect=QRectF(40,40,300,130);
    sample.fontSize=72;sample.font.setBold(true);sample.color=QColor(40,145,180);sample.fill=QColor(180,85,125);
    sample.outline=0;sample.reliefSize=5;
    const QSize textSize(400,220);
    const auto flat=textOverlay(textSize,{sample},false);
    auto cutout=sample;cutout.surface=1;
    const auto plate=textOverlay(textSize,{cutout},false);
    int holes=0;
    for(int y=40;y<170;++y)for(int x=40;x<340;++x)
        if(qAlpha(flat.pixel(x,y))>240 && qAlpha(plate.pixel(x,y))<16)++holes;
    require(holes>100,"cutout plate must have real transparent glyph holes");
    require(plate.pixelColor(45,45)==sample.fill,"cutout plate background color");
    sample.relief=1;const auto embossed=textOverlay(textSize,{sample},false);
    require(embossed!=flat && embossed!=textOverlay(textSize,{sample},true),"emboss shading and stereo relief");
    sample.relief=2;const auto engraved=textOverlay(textSize,{sample},false);
    require(engraved!=flat && engraved!=embossed,"engrave reverses relief");
    cutout.relief=1;const auto raisedPlate=textOverlay(textSize,{cutout},false);
    cutout.relief=2;const auto recessedPlate=textOverlay(textSize,{cutout},false);
    require(raisedPlate!=plate && recessedPlate!=raisedPlate && recessedPlate!=textOverlay(textSize,{cutout},true),"cutout rim relief and stereo depth");
    sample.relief=0;sample.depth=14;const auto center=sample.rect.center();
    for(int axis=0;axis<3;++axis) {
        auto tilted=sample;
        if(axis==0)tilted.tiltX=20;if(axis==1)tilted.tiltY=20;if(axis==2)tilted.tiltZ=20;
        const auto a=stereoTextTransform(tilted,false),b=stereoTextTransform(tilted,true);
        require(QLineF(a.map(center),center).length()<0.001 && QLineF(b.map(center),center-QPointF(14,0)).length()<0.001,"tilt pivot preserves center and base depth");
        const QPointF detail=center+QPointF(70,35);
        require(qAbs(a.map(detail).y()-b.map(detail).y())<0.001,"tilted stereo plane must retain vertical alignment");
        if(axis<2)require(qAbs(a.map(detail).x()-b.map(detail).x()-14)>0.1,"pitch/yaw must create a stereo depth gradient");
        require(textOverlay(textSize,{tilted},false)!=flat,"tilt changes rendered text");
    }
    sample.tiltZ=20;const auto leftTilt=stereoTextTransform(sample,false),rightTilt=stereoTextTransform(sample,true);
    require(qAbs(leftTilt.map(center+QPointF(50,0)).y()-center.y()-50*qSin(qDegreesToRadians(20.0)))<0.001 &&
            qAbs(rightTilt.map(center+QPointF(50,0)).y()-center.y()-50*qSin(qDegreesToRadians(20.0)))<0.001,"20 degree Z tilt must be 20 degrees in both eyes");
    for(int angle:{-45,45})for(int axis=0;axis<4;++axis) {
        auto sameTilt=sample;sameTilt.tiltX=sameTilt.tiltY=sameTilt.tiltZ=0;
        if(axis==0 || axis==3)sameTilt.tiltX=angle;
        if(axis==1 || axis==3)sameTilt.tiltY=angle;
        if(axis==2 || axis==3)sameTilt.tiltZ=angle;
        const auto l=stereoTextTransform(sameTilt,false),r=stereoTextTransform(sameTilt,true);
        for(const auto &point:{sample.rect.topLeft(),sample.rect.topRight(),sample.rect.bottomLeft(),sample.rect.bottomRight(),center})
            require(qAbs(l.map(point).y()-r.map(point).y())<0.001,"45 degree tilted plane must preserve vertical alignment in both eyes");
        sameTilt.depth=0;
        if(axis==2)require(textOverlay(textSize,{sameTilt},false)==textOverlay(textSize,{sameTilt},true),"roll alone must not introduce a stereo depth gradient");
        if(axis<2) {
            const QPointF front=axis==0 ? center+QPointF(0,40) : center-QPointF(70,0);
            const double relative=l.map(front).x()-r.map(front).x()-sample.depth;
            require((angle>0 ? relative : -relative)>0.1,"positive pitch brings bottom forward; positive yaw brings left forward");
        }
    }
    for(int angle:{32,33,34,45,60,80,-45,-80})for(bool rightEye:{false,true}) {
        auto steep=sample;steep.depth=0;steep.relief=1;steep.tiltX=angle;steep.tiltY=steep.tiltZ=0;
        steep.rect=QRectF(800,650,300,130);
        const auto image=textOverlay(QSize(2400,2000),{steep},rightEye);
        int visible=0;
        for(int y=600;y<850;++y)for(int x=700;x<1250;++x)visible+=qAlpha(image.pixel(x,y))>0;
        require(visible>50,"embossed text disappears at steep X tilt on a large photo");
    }
    sample.relief=2;sample.surface=1;sample.tiltX=20;sample.tiltY=-30;
    const auto saved=textFromJson(textToJson(sample),textSize);
    require(saved.relief==2 && saved.surface==1 && saved.reliefSize==5 && saved.tiltX==20 && saved.tiltY==-30 && saved.tiltZ==20,"effect/tilt JSON roundtrip");
    const auto legacy=textFromJson(QJsonObject{},textSize);
    require(legacy.relief==0 && legacy.surface==0 && legacy.tiltX==0 && legacy.tiltY==0 && legacy.tiltZ==0,"legacy projects retain flat unrotated text");
    auto invalid=textToJson(sample);invalid["tiltX"]=900;invalid["tiltY"]=-900;invalid["reliefSize"]=1000;
    const auto clamped=textFromJson(invalid,textSize);
    require(clamped.tiltX==80 && clamped.tiltY==-80 && clamped.reliefSize==32,"untrusted effect values are bounded");
    QImage foreground(textSize,QImage::Format_ARGB32_Premultiplied);foreground.fill(Qt::white);
    auto occluded=sample;occluded.foregroundCovers=true;
    QImage empty(textSize,QImage::Format_ARGB32_Premultiplied);empty.fill(Qt::transparent);
    require(textOverlay(textSize,{occluded},false,foreground)==empty,"foreground occludes transformed text effects");
    QImage gallery(800,10*250,QImage::Format_RGB32);gallery.fill(QColor(225,230,234));
    {QPainter painter(&gallery);for(int row=0;row<10;++row) {
        auto item=sample;item.depth=0;item.tiltX=item.tiltY=item.tiltZ=0;item.surface=row>=3 && row<6 ? 1 : 0;item.relief=row%3;
        if(row==6){item.relief=1;item.tiltX=20;item.tiltY=30;item.tiltZ=12;}
        if(row>=7){item.relief=0;if(row==7)item.tiltX=60;if(row==8)item.tiltY=45;if(row==9)item.tiltZ=45;}
        const QString label=row==7 ? "X +60: floor pose / pitch" : row==8 ? "Y +45: left side forward / yaw" : row==9 ? "Z +45: clockwise roll" : QString("%1 | %2").arg(item.surface ? "Cutout plate" : "Colored letters",item.relief==0 ? "Flat" : item.relief==1 ? "Embossed" : "Engraved");
        painter.setPen(Qt::black);painter.drawText(12,row*250+20,label+" | left / right");
        painter.drawImage(0,row*250+30,textOverlay(textSize,{item},false));painter.drawImage(400,row*250+30,textOverlay(textSize,{item},true));
    }}
    require(gallery.save("text-effects-gallery.png"),"effect gallery screenshot");
    text.text="MASK";
    text.relief=sample.relief;text.surface=sample.surface;text.reliefSize=sample.reliefSize;
    text.tiltX=sample.tiltX;text.tiltY=sample.tiltY;text.tiltZ=sample.tiltZ;
    view.setTextObjects({text});
    view.undoStack()->undo();
    require(view.textObjects().isEmpty(), "text undo");
    view.undoStack()->redo();
    require(view.textObjects().first().tiltY==-30 && view.textObjects().first().surface==1,"text effects undo/redo");

    require(view.saveProject(), "save new fields");
    StereoViewWidget loaded;
    loaded.setAutoSave(false);
    require(loaded.loadProject(folder.path() + "/source.msk"), "reload new fields");
    require(loaded.textObjects().size() == 1 && loaded.pictureFrameSettings().shadowEnabled,
            "project keeps decorations");
    require(loaded.textObjects().first().relief==2 && loaded.textObjects().first().tiltX==20,"project keeps text relief and tilt");
    require(loaded.pictureFrameSettings().outlineEnabled && !loaded.pictureFrameSettings().outlineDisplay
            && loaded.pictureFrameSettings().outlineOffset==6 && loaded.pictureFrameSettings().outlineMiddleWidth==4
            && loaded.pictureFrameSettings().outlineInnerColor==QColor(Qt::red),"project preserves independent outline settings");
    {
        QFile project(folder.path()+"/source.msk");require(project.open(QIODevice::ReadOnly),"read project fixture");
        auto object=QJsonDocument::fromJson(project.readAll()).object();
        const auto validText=object["stereoText"].toArray().first();
        object["stereoText"]=QJsonArray{QJsonValue(),"invalid",42,true,QJsonArray{},validText};
        QFile malformed(folder.path()+"/malformed.msk");require(malformed.open(QIODevice::WriteOnly),"write malformed text fixture");
        require(malformed.write(QJsonDocument(object).toJson())>0,"save malformed text fixture");malformed.close();
        require(loaded.loadProject(malformed.fileName()) && loaded.textObjects().size()==1 && loaded.textObjects().first().text==text.text,
                "skip non-object project text entries without synthesizing default text");
    }
    const QString output = folder.path() + "/output.png";
    require(StereoViewWidget::saveComposedPair(output, view.exportEye(false), view.exportEye(true),
                                               0, 0, 0, Qt::transparent), "decorated PNG export");
    require(QImage(output).size() == source.size(), "export size");
    const QImage exportLeft=QImage(output).copy(QRect(0,0,360,240)).convertToFormat(QImage::Format_ARGB32);
    require(exportLeft==view.exportEye(false).convertToFormat(QImage::Format_ARGB32),"exported text effects match eye preview pixels");
    auto coveringText=text;coveringText.foregroundCovers=true;view.setTextObjects({coveringText});
    const auto beforeOcclusion=view.exportEye(false);
    SelectionState coveredState;SelectionObject coverage;coverage.id="occlusion-cache";
    coverage.raster=QImage(360,240,QImage::Format_ARGB32_Premultiplied);coverage.raster.fill(Qt::white);coveredState.objects.append(coverage);
    view.restoreSelectionState(coveredState);
    const auto coveredBase=view.exportEye(false,false);
    const auto coveredText=view.exportEye(false);
    require(coveredText==coveredBase && coveredText!=beforeOcclusion,"foreground changes invalidate applied text overlay cache");
    require(loaded.closeProject() && loaded.textObjects().isEmpty()
            && !loaded.pictureFrameSettings().shadowEnabled, "close clears decoration state");
    const QString replacement=folder.path()+"/replacement.png";require(source.save(replacement),"save replacement image");
    require(loaded.loadImage(replacement) && loaded.textObjects().isEmpty()
            && loaded.exportEye(false)==loaded.exportEye(false,false),"new image must not retain previous text overlays");
    std::cout << "Shapes, curve splitting, shadow, text, undo, project and PNG export: PASS\n";
}
