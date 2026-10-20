#ifndef OPERATIONPROGRESS_H
#define OPERATIONPROGRESS_H

#include <QApplication>
#include <QPointer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QDialog>
#include <QLabel>
#include <QHBoxLayout>
#include <QScreen>
#include <QPainter>
#include <QStatusBar>
#include <QtConcurrent>
#include <type_traits>
#include <utility>

// Keeps existing synchronous command/undo contracts while pure calculations run
// in a worker. The modal wait processes painting/timers, but not editing input.
class OperationMessagePanel : public QDialog {
public:
    explicit OperationMessagePanel(QWidget *parent)
        : QDialog(parent,Qt::Dialog | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground,false);
        setAttribute(Qt::WA_OpaquePaintEvent,true);
        setAutoFillBackground(true);
        QPalette colors=palette();
        colors.setColor(QPalette::Window,QColor("#25282c"));
        setPalette(colors);
        setWindowOpacity(1.0);
    }
    void reject() override {} // There is no cancellation while committing work.
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(),QColor("#25282c"));
        painter.setPen(QColor("#687078"));
        painter.drawRect(rect().adjusted(0,0,-1,-1));
    }
};

class OperationProgress {
public:
    OperationProgress(QWidget *parent, const QString &title, const QString &stage)
        : dialog(new OperationMessagePanel(parent->window())), label(new QLabel(dialog)),
          window(qobject_cast<QMainWindow *>(parent->window())) {
        dialog->setObjectName("operationProgress");
        dialog->setWindowTitle(title);
        dialog->setAccessibleName(title);
        dialog->setWindowModality(Qt::ApplicationModal);
        label->setStyleSheet("color: #f5f6f7; background: transparent; border: none;");
        auto layout=new QHBoxLayout(dialog);
        layout->setContentsMargins(24,18,24,18);
        label->setObjectName("operationMessage");
        label->setAlignment(Qt::AlignCenter);
        label->setTextFormat(Qt::PlainText);
        layout->addWidget(label);
        const QRect available=parent->screen()->availableGeometry();
        const int width=qMin(640,qMax(160,qMin(parent->window()->width()-40,available.width()-40)));
        dialog->setFixedSize(width,64);
        const QPoint center=parent->window()->frameGeometry().center();
        dialog->move(qBound(available.left(),center.x()-width/2,available.right()-width+1),
                    qBound(available.top(),center.y()-32,available.bottom()-63));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        visible = parent->isVisible();
        setStage(stage);
        if (visible) {
            dialog->show();
            layout->activate();
            paintMessage();
        }
    }
    ~OperationProgress() {
        if(dialog)delete dialog.data();
        QApplication::restoreOverrideCursor();
        if (window && window->statusBar()->currentMessage() == lastStage)
            window->statusBar()->showMessage(QObject::tr("Operation complete."), 3000);
    }
    OperationProgress(const OperationProgress &)=delete;
    OperationProgress &operator=(const OperationProgress &)=delete;
    void setStage(const QString &stage) {
        if(!dialog || !label)return;
        lastStage = stage;
        label->setText(label->fontMetrics().elidedText(stage,Qt::ElideRight,dialog->width()-48));
        label->setAccessibleDescription(stage);
        label->setToolTip(stage);
        if (window) {
            window->statusBar()->showMessage(stage);
            window->statusBar()->repaint();
        }
        if (visible && dialog->isVisible()) paintMessage();
    }
    template<class Function>
    auto run(Function function) -> std::invoke_result_t<Function> {
        using Result = std::invoke_result_t<Function>;
        QFutureWatcher<Result> watcher;
        QEventLoop loop;
        QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run(std::move(function)));
        if (!watcher.isFinished()) loop.exec(QEventLoop::ExcludeUserInputEvents);
        return watcher.result();
    }
private:
    void paintMessage() {
        // Deliver only paint/layout work, not timers or queued commands that can
        // re-enter the operation before a synchronous save has finished.
        QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::UpdateRequest);
        if(!dialog || !label)return;
        label->repaint();
        dialog->repaint();
    }
    QPointer<OperationMessagePanel> dialog;
    QPointer<QLabel> label;
    QPointer<QMainWindow> window;
    QString lastStage;
    bool visible = false;
};

#endif
