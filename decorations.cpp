#include "decorations.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QUuid>
#include <QtMath>
#include <cmath>

QJsonObject textToJson(const StereoText &t) {
    return {{"id",t.id},{"text",t.text},{"rect",QJsonArray{t.rect.x(),t.rect.y(),t.rect.width(),t.rect.height()}},
        {"tail",QJsonArray{t.tail.x(),t.tail.y()}},{"depth",t.depth},{"font",t.font.toString()},{"fontSize",t.fontSize},
        {"style",t.style},{"alignment",t.alignment},{"opacity",t.opacity},{"outline",t.outline},
        {"color",t.color.name(QColor::HexArgb)},{"fill",t.fill.name(QColor::HexArgb)},{"stroke",t.stroke.name(QColor::HexArgb)},
        {"visible",t.visible},{"foregroundCovers",t.foregroundCovers}};
}

StereoText textFromJson(const QJsonObject &o,QSize size) {
    StereoText t;t.id=o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));t.text=o["text"].toString().left(10000);
    auto r=o["rect"].toArray();auto p=o["tail"].toArray();
    const double limit=qMax(1,qMax(size.width(),size.height()))*4.0;
    auto finite=[limit](double v){return qIsFinite(v) ? qBound(-limit,v,limit) : 0.0;};
    if(r.size()==4)t.rect=QRectF(finite(r[0].toDouble()),finite(r[1].toDouble()),qBound(16.0,finite(r[2].toDouble()),limit),qBound(16.0,finite(r[3].toDouble()),limit));
    if(p.size()==2)t.tail=QPointF(finite(p[0].toDouble()),finite(p[1].toDouble()));
    t.depth=qBound(-10000,o["depth"].toInt(),10000);t.font.fromString(o["font"].toString());
    t.fontSize=qBound(1,o["fontSize"].toInt(32),1000);t.style=qBound(0,o["style"].toInt(),3);t.alignment=qBound(0,o["alignment"].toInt(),2);
    t.opacity=qBound(0,o["opacity"].toInt(100),100);t.outline=qBound(0,o["outline"].toInt(2),100);
    auto color=[&](const char *key,QColor fallback){QColor c(o[key].toString());return c.isValid()?c:fallback;};
    t.color=color("color",Qt::black);t.fill=color("fill",Qt::white);t.stroke=color("stroke",Qt::black);
    t.visible=o["visible"].toBool(true);t.foregroundCovers=o["foregroundCovers"].toBool(false);return t;
}

QImage textOverlay(QSize size,const QVector<StereoText> &texts,bool right,const QImage &foreground) {
    QImage result(size,QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);
    for(const auto &t:texts) {
        if(!t.visible)continue;
        QImage layer(size,QImage::Format_ARGB32_Premultiplied);layer.fill(Qt::transparent);
        QPainter p(&layer);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::TextAntialiasing);p.translate(right ? -t.depth : 0,0);
        QPainterPath body;if(t.style==1 || t.style==3)body.addEllipse(t.rect);if(t.style==2)body.addRoundedRect(t.rect,16,16);
        if(t.style==1 || t.style==2) {const QPointF center=t.rect.center(),delta=t.tail-center;const double length=qMax(1.0,std::hypot(delta.x(),delta.y()));const QPointF normal(-delta.y()/length,delta.x()/length);QPainterPath triangle;triangle.moveTo(center+normal*qMin(20.0,t.rect.width()/6));triangle.lineTo(t.tail);triangle.lineTo(center-normal*qMin(20.0,t.rect.width()/6));triangle.closeSubpath();body=body.united(triangle);}
        if(t.style==3) {const QPointF center=t.rect.center(),delta=t.tail-center;const double rx=t.rect.width()/2,ry=t.rect.height()/2;const double edge=1.0/qSqrt(qPow(delta.x()/rx,2)+qPow(delta.y()/ry,2)+0.000001);for(int i=0;i<3;++i){double f=qBound(0.0,edge+(1-edge)*(i+1)/3.0,1.0);double radius=qMax(3.0,12.0-i*4.0);body.addEllipse(center+delta*f,radius,radius);}}
        if(t.style) {QColor fill=t.fill;fill.setAlpha(qRound(255*t.opacity/100.0));p.setBrush(fill);p.setPen(t.outline ? QPen(t.stroke,t.outline) : QPen(Qt::NoPen));p.drawPath(body);}
        QFont font=t.font;font.setPixelSize(t.fontSize);p.setFont(font);p.setPen(t.color);const int align=t.alignment==0 ? Qt::AlignLeft : t.alignment==1 ? Qt::AlignHCenter : Qt::AlignRight;
        QRectF textRect=t.rect.adjusted(t.style?12:0,t.style?12:0,t.style?-12:0,t.style?-12:0);if(t.style==1 || t.style==3)textRect=t.rect.adjusted(t.rect.width()*0.16,t.rect.height()*0.16,-t.rect.width()*0.16,-t.rect.height()*0.16);
        p.drawText(textRect,Qt::TextWordWrap|Qt::AlignVCenter|align,t.text);p.resetTransform();if(t.foregroundCovers && foreground.size()==size){p.setCompositionMode(QPainter::CompositionMode_DestinationOut);p.drawImage(0,0,foreground);}p.end();QPainter composite(&result);composite.drawImage(0,0,layer);
    }
    return result;
}

QVector<MaskPoint> shapePoints(int shape,const QRectF &r,int depth) {
    QVector<MaskPoint> points;
    if(shape<2) {for(const auto &p:QVector<QPointF>{r.topLeft(),r.topRight(),r.bottomRight(),r.bottomLeft()})points.append({p,float(depth)});}
    else {const double x=r.center().x(),y=r.center().y(),rx=r.width()/2,ry=r.height()/2,k=0.5522847498307936;const QVector<QPointF> positions={{x,y-ry},{x+rx,y},{x,y+ry},{x-rx,y}};const QVector<QPointF> first={{x+k*rx,y-ry},{x+rx,y+k*ry},{x-k*rx,y+ry},{x-rx,y-k*ry}};const QVector<QPointF> second={{x+rx,y-k*ry},{x+k*rx,y+ry},{x-rx,y+k*ry},{x-k*rx,y-ry}};for(int i=0;i<4;++i){MaskPoint p{positions[i],float(depth)};p.isCurve=true;p.cp1=first[i];p.cp2=second[i];p.cp1Disparity=p.cp2Disparity=depth;points.append(p);}}
    return points;
}
