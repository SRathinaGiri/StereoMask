#ifndef UNDOCOMMAND_H
#define UNDOCOMMAND_H

#include <QUndoCommand>
#include <QVector>
#include "maskpoint.h"

class StereoViewWidget;

class MovePointCommand : public QUndoCommand
{
public:
    MovePointCommand(StereoViewWidget *widget, int index, const MaskPoint &oldP, const MaskPoint &newP);
    void undo() override;
    void redo() override;

private:
    StereoViewWidget *m_widget;
    int m_index;
    MaskPoint m_oldPoint;
    MaskPoint m_newPoint;
};

class BatchMoveCommand : public QUndoCommand
{
public:
    BatchMoveCommand(StereoViewWidget *widget, const QList<int> &indices, const QVector<MaskPoint> &oldPoints, const QVector<MaskPoint> &newPoints);
    void undo() override;
    void redo() override;

private:
    StereoViewWidget *m_widget;
    QList<int> m_indices;
    QVector<MaskPoint> m_oldPoints;
    QVector<MaskPoint> m_newPoints;
};

class AddPointCommand : public QUndoCommand
{
public:
    AddPointCommand(StereoViewWidget *widget, const MaskPoint &point, int index = -1);
    void undo() override;
    void redo() override;

private:
    StereoViewWidget *m_widget;
    MaskPoint m_point;
    int m_index;
};

class DeletePointsCommand : public QUndoCommand
{
public:
    DeletePointsCommand(StereoViewWidget *widget, const QList<int> &indices, const QVector<MaskPoint> &points);
    void undo() override;
    void redo() override;

private:
    StereoViewWidget *m_widget;
    QList<int> m_indices;
    QVector<MaskPoint> m_points;
};

#endif // UNDOCOMMAND_H
