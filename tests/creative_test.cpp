#include "stereoviewwidget.h"
#include "decorations.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPainter>
#include <iostream>
#include <cstdlib>

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

    StereoText text;
    text.id = "fixture";
    text.rect = QRectF(20, 20, 100, 80);
    text.tail = QPointF(40, 140);
    text.style = 2;
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
    view.setTextObjects({text});
    view.undoStack()->undo();
    require(view.textObjects().isEmpty(), "text undo");
    view.undoStack()->redo();

    require(view.saveProject(), "save new fields");
    StereoViewWidget loaded;
    loaded.setAutoSave(false);
    require(loaded.loadProject(folder.path() + "/source.msk"), "reload new fields");
    require(loaded.textObjects().size() == 1 && loaded.pictureFrameSettings().shadowEnabled,
            "project keeps decorations");
    const QString output = folder.path() + "/output.png";
    require(StereoViewWidget::saveComposedPair(output, view.exportEye(false), view.exportEye(true),
                                               0, 0, 0, Qt::transparent), "decorated PNG export");
    require(QImage(output).size() == source.size(), "export size");
    require(loaded.closeProject() && loaded.textObjects().isEmpty()
            && !loaded.pictureFrameSettings().shadowEnabled, "close clears decoration state");
    std::cout << "Shapes, curve splitting, shadow, text, undo, project and PNG export: PASS\n";
}
