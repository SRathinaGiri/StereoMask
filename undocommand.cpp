#include "undocommand.h"
#include "stereoviewwidget.h"
#include <algorithm>
#include <utility>

MovePointCommand::MovePointCommand(StereoViewWidget *widget, int index, const MaskPoint &oldP, const MaskPoint &newP)
    : m_widget(widget), m_index(index), m_oldPoint(oldP), m_newPoint(newP)
{
    setText("Move Point");
}

void MovePointCommand::undo() { m_widget->updatePointInternal(m_index, m_oldPoint); }
void MovePointCommand::redo() { m_widget->updatePointInternal(m_index, m_newPoint); }

AddPointCommand::AddPointCommand(StereoViewWidget *widget, const MaskPoint &point, int index)
    : m_widget(widget), m_point(point), m_index(index)
{
    setText("Add Point");
}

void AddPointCommand::undo() { m_widget->removePointInternal(m_index); }
void AddPointCommand::redo() { m_widget->addPointInternal(m_point, m_index); }

DeletePointsCommand::DeletePointsCommand(StereoViewWidget *widget, const QList<int> &indices, const QVector<MaskPoint> &points)
    : m_widget(widget), m_indices(indices), m_points(points)
{
    setText("Delete Points");
}

void DeletePointsCommand::undo()
{
    QVector<std::pair<int, MaskPoint>> pairs;
    for (int i = 0; i < m_indices.count() && i < m_points.count(); ++i) {
        pairs.append({m_indices[i], m_points[i]});
    }
    std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    QList<int> indices;
    QVector<MaskPoint> points;
    for (const auto &pair : pairs) {
        indices.append(pair.first);
        points.append(pair.second);
    }
    m_widget->insertPointsInternal(indices, points);
}

void DeletePointsCommand::redo()
{
    QList<int> indices = m_indices;
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    m_widget->removePointsInternal(indices);
}

BatchMoveCommand::BatchMoveCommand(StereoViewWidget *widget, const QList<int> &indices, const QVector<MaskPoint> &oldPoints, const QVector<MaskPoint> &newPoints)
    : m_widget(widget), m_indices(indices), m_oldPoints(oldPoints), m_newPoints(newPoints)
{
    setText("Align Points");
}

void BatchMoveCommand::undo() { m_widget->updatePointsInternal(m_indices, m_oldPoints); }
void BatchMoveCommand::redo() { m_widget->updatePointsInternal(m_indices, m_newPoints); }
