#include "smartselection.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <QtMath>
#include <exception>
#include <vector>
#include <algorithm>

StereoPolygonMatch alignStereoPolygon(const QImage &source, const QImage &opposite,
                                     const QVector<QPointF> &anchors, double fallbackShift) {
    StereoPolygonMatch result;
    if (source.isNull() || opposite.size()!=source.size()) return result;
    QVector<double> shifts(anchors.size(),fallbackShift), reliable;
    QVector<bool> matched(anchors.size(),false);
    try {
        const QImage a=source.convertToFormat(QImage::Format_Grayscale8);
        const QImage b=opposite.convertToFormat(QImage::Format_Grayscale8);
        cv::Mat from(a.height(),a.width(),CV_8UC1,const_cast<uchar *>(a.constBits()),a.bytesPerLine());
        cv::Mat to(b.height(),b.width(),CV_8UC1,const_cast<uchar *>(b.constBits()),b.bytesPerLine());
        const int radius=12, range=qMin(1024,qMax(48,a.width()/4));
        // Reciprocal, unambiguous matches along epipolar rows; preserve vertical alignment.
        auto match=[&](const cv::Mat &src,const cv::Mat &dst,QPoint point,double expected,int &x) {
            const cv::Rect patch(point.x()-radius,point.y()-radius,2*radius+1,2*radius+1);
            if (patch.x<0 || patch.y<0 || patch.x+patch.width>src.cols || patch.y+patch.height>src.rows) return false;
            cv::Scalar mean,deviation; cv::meanStdDev(src(patch),mean,deviation);
            if (deviation[0]<5) return false;
            const int low=qMax(0,qRound(point.x()+expected)-range-radius);
            const int high=qMin(dst.cols,qRound(point.x()+expected)+range+radius+1);
            if (high-low<patch.width) return false;
            cv::Mat scores;
            cv::matchTemplate(dst(cv::Rect(low,patch.y,high-low,patch.height)),src(patch),scores,cv::TM_CCOEFF_NORMED);
            double best; cv::Point location; cv::minMaxLoc(scores,nullptr,&best,nullptr,&location);
            if (best<0.65) return false;
            x=low+location.x+radius;
            const int left=qMax(0,location.x-3),right=qMin(scores.cols,location.x+4);
            scores.colRange(left,right).setTo(-1);
            double competitor; cv::minMaxLoc(scores,nullptr,&competitor);
            return best-competitor>=0.04;
        };
        for (int i=0; i<anchors.size(); ++i) {
            const QPoint point=anchors[i].toPoint(); int targetX,backX;
            if (!match(from,to,point,fallbackShift,targetX) ||
                !match(to,from,QPoint(targetX,point.y()),-fallbackShift,backX) || std::abs(backX-point.x())>2) continue;
            shifts[i]=targetX-point.x(); matched[i]=true; reliable.append(shifts[i]);
        }
    } catch (const cv::Exception &) {
        reliable.clear(); matched.fill(false);
    }
    if (!reliable.isEmpty()) {
        std::sort(reliable.begin(),reliable.end()); fallbackShift=reliable[reliable.size()/2];
    }
    for (int i=0; i<anchors.size(); ++i) {
        if (matched[i]) ++result.matchedAnchors;
        result.anchors.append(QPointF(qBound(0.0,anchors[i].x()+(matched[i] ? shifts[i] : fallbackShift),double(opposite.width()-1)),
                                     qBound(0.0,anchors[i].y(),double(opposite.height()-1))));
    }
    return result;
}

SmartSelectionResult findSmartEdges(const QImage &image, const QRect &region,
                                  const QImage &hints, const QImage &roughReveal)
{
    SmartSelectionResult result;
    if (image.isNull() || region.width() < 8 || region.height() < 8 ||
        !image.rect().contains(region)) {
        result.error = QStringLiteral("Draw a loose box around the leg first (at least 8 pixels wide and high).");
        return result;
    }
    try {
        // Small local regions use original pixels; cap large regions to bound
        // the graph's memory and processing time. Source photos stay unchanged.
        const double scale = qMin(1.0, 1600.0 / qMax(region.width(), region.height()));
        const QSize size(qMax(8, qRound(region.width()*scale)), qMax(8, qRound(region.height()*scale)));
        QImage rgb = image.copy(region).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB888);
        const QImage seed = hints.isNull() ? QImage() : hints.copy(region).scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const QImage rough = roughReveal.isNull() ? QImage() : roughReveal.copy(region).scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        cv::Mat input(rgb.height(), rgb.width(), CV_8UC3, rgb.bits(), rgb.bytesPerLine());
        cv::Mat bgr; cv::cvtColor(input, bgr, cv::COLOR_RGB2BGR);
        cv::Mat labels(size.height(), size.width(), CV_8UC1, cv::Scalar(cv::GC_PR_BGD));
        int keepCount = 0, removeCount = 0;
        for (int y=0; y<size.height(); ++y) {
            auto *row = labels.ptr<uchar>(y);
            for (int x=0; x<size.width(); ++x) {
                // Prior rough paint is uncertain foreground, never a hard seed.
                if (!rough.isNull() && qAlpha(rough.pixel(x,y)) > 0) row[x] = cv::GC_PR_FGD;
                // Boundary supplies definite background unless explicitly marked.
                if (x==0 || y==0 || x==size.width()-1 || y==size.height()-1) row[x] = cv::GC_BGD;
                if (!seed.isNull() && qAlpha(seed.pixel(x,y)) > 0) {
                    const QRgb p = seed.pixel(x,y);
                    if (qGreen(p) > qRed(p)) { row[x] = cv::GC_FGD; ++keepCount; }
                    else { row[x] = cv::GC_BGD; }
                }
                if (row[x] == cv::GC_BGD) ++removeCount;
            }
        }
        if (keepCount < 5 || removeCount < 5) {
            result.error = QStringLiteral("Paint a small Keep stroke inside the leg, leaving background space around the box. Add a Remove stroke on the background if needed.");
            return result;
        }
        cv::Mat bg, fg;
        cv::grabCut(bgr, labels, cv::Rect(), bg, fg, 5, cv::GC_INIT_WITH_MASK);
        // Retain only foreground components connected to an explicit Keep
        // seed. Disconnected scenery of similar color cannot survive by itself.
        cv::Mat foreground = (labels == cv::GC_FGD) | (labels == cv::GC_PR_FGD);
        cv::Mat components;
        const int count = cv::connectedComponents(foreground, components, 8, CV_32S);
        std::vector<bool> retain(count, false);
        for (int y=0; y<size.height(); ++y)
            for (int x=0; x<size.width(); ++x)
                if (labels.at<uchar>(y,x) == cv::GC_FGD)
                    retain[components.at<int>(y,x)] = true;
        retain[0] = false;
        QImage mask(size, QImage::Format_ARGB32_Premultiplied);
        for (int y=0; y<size.height(); ++y) {
            auto *out = reinterpret_cast<QRgb *>(mask.scanLine(y));
            for (int x=0; x<size.width(); ++x)
                out[x] = retain[components.at<int>(y,x)] ? qRgba(255,255,255,255) : qRgba(0,0,0,0);
        }
        result.mask = mask.scaled(region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Honor original-resolution seed strokes, including narrow strokes
        // that the bounded processing grid might otherwise soften.
        if (!hints.isNull()) {
            for (int y=0; y<region.height(); ++y) {
                auto *out = reinterpret_cast<QRgb *>(result.mask.scanLine(y));
                for (int x=0; x<region.width(); ++x) {
                    const QRgb p = hints.pixel(region.x()+x, region.y()+y);
                    if (qAlpha(p)>0) out[x] = qGreen(p)>qRed(p) ? qRgba(255,255,255,255) : qRgba(0,0,0,0);
                }
            }
        }
    } catch (const cv::Exception &e) {
        result.error = QStringLiteral("Edge selection could not separate these colors. Add Keep and Remove strokes and try again. Details: ") + QString::fromUtf8(e.what());
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
    }
    return result;
}

#include <queue>
#include <limits>
#include <cmath>
// Local eight-neighbour shortest path: prefer strong colour edges and stay
// near the clicked chord so distant bleacher rails cannot attract the path.
QVector<QPointF> magneticLassoPath(const QImage &image, const QPointF &start, const QPointF &end) {
    if (image.isNull()) return {start,end};
    QRect roi=QRect(start.toPoint(),end.toPoint()).normalized().adjusted(-28,-28,28,28).intersected(image.rect());
    const double factor=qMin(1.0,600.0/qMax(roi.width(),roi.height()));
    QImage rgb=image.copy(roi).scaled(qMax(1,qRound(roi.width()*factor)),qMax(1,qRound(roi.height()*factor)),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
    cv::Mat source(rgb.height(),rgb.width(),CV_8UC3,rgb.bits(),rgb.bytesPerLine()), smooth,gx,gy;
    cv::GaussianBlur(source,smooth,cv::Size(3,3),0.6);
    cv::Sobel(smooth,gx,CV_32F,1,0,3); cv::Sobel(smooth,gy,CV_32F,0,1,3);
    const int w=rgb.width(),h=rgb.height(),n=w*h;
    const double sx=double(w)/roi.width(),sy=double(h)/roi.height();
    auto local=[&](QPointF p) { return QPoint(qBound(0,qRound((p.x()-roi.x())*sx),w-1),qBound(0,qRound((p.y()-roi.y())*sy),h-1)); };
    const QPoint a=local(start),b=local(end);
    const int first=a.y()*w+a.x(),last=b.y()*w+b.x();
    QVector<float> costs(n),distance(n,std::numeric_limits<float>::infinity()); QVector<int> parent(n,-1);
    const double dx=b.x()-a.x(),dy=b.y()-a.y(),length2=dx*dx+dy*dy;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        auto vx=gx.at<cv::Vec3f>(y,x),vy=gy.at<cv::Vec3f>(y,x);
        float strength=0; for(int c=0;c<3;++c) strength=qMax(strength,std::sqrt(vx[c]*vx[c]+vy[c]*vy[c]));
        const double t=length2 ? qBound(0.0,((x-a.x())*dx+(y-a.y())*dy)/length2,1.0):0;
        const double chord=std::hypot(x-a.x()-t*dx,y-a.y()-t*dy)/factor;
        costs[y*w+x]=0.18f+4.0f/(1.0f+strength/22.0f)+float(0.018*chord);
    }
    using Node=std::pair<float,int>; std::priority_queue<Node,std::vector<Node>,std::greater<Node>> queue;
    distance[first]=0;queue.push({0,first});
    while(!queue.empty()) {
        auto [d,i]=queue.top();queue.pop(); if(d!=distance[i]) continue; if(i==last) break;
        const int x=i%w,y=i/w;
        for(int yy=-1;yy<=1;++yy) for(int xx=-1;xx<=1;++xx) {
            if((!xx&&!yy)||x+xx<0||x+xx>=w||y+yy<0||y+yy>=h) continue;
            const int j=(y+yy)*w+x+xx;
            const float next=d+0.5f*(costs[i]+costs[j])*((xx&&yy)?1.41421356f:1.f);
            if(next<distance[j]) {distance[j]=next;parent[j]=i;queue.push({next,j});}
        }
    }
    QVector<QPointF> path; int i=last;
    while(i>=0) {path.prepend(QPointF(roi.x()+(i%w)/sx,roi.y()+(i/w)/sy));if(i==first) break;i=parent[i];}
    if(path.isEmpty() || i!=first) return {start,end};
    path.first()=start;path.last()=end;return path;
}
