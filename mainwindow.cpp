#include "mainwindow.h"
#include "stereoviewwidget.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QDockWidget>
#include <QFormLayout>
#include <QSpinBox>
#include <QSlider>
#include <QActionGroup>
#include <QToolBar>

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_viewWidget = new StereoViewWidget(this);
    setCentralWidget(m_viewWidget);

    createMenus();
    createDockWidgets();

    setWindowTitle(tr("StereoMask - Qt6"));
    resize(1024, 768);

    connect(m_viewWidget, &StereoViewWidget::selectionChanged, this, [this](int disp){
        m_disparitySlider->blockSignals(true);
        m_disparitySpinBox->blockSignals(true);
        m_disparitySlider->setValue(disp);
        m_disparitySpinBox->setValue(disp);
        m_disparitySlider->blockSignals(false);
        m_disparitySpinBox->blockSignals(false);
    });
}

MainWindow::~MainWindow()
{
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::openImage);
    fileMenu->addAction(tr("&Save As..."), QKeySequence::Save, this, &MainWindow::saveImage);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = m_viewWidget->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);

    QAction *redoAction = m_viewWidget->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *sbsAction = viewMenu->addAction(tr("&Side-by-Side"));
    sbsAction->setCheckable(true);
    sbsAction->setChecked(true);

    QAction *anaglyphAction = viewMenu->addAction(tr("&Anaglyph"));
    anaglyphAction->setCheckable(true);

    QAction *swapAction = viewMenu->addAction(tr("S&wap Sides"));
    swapAction->setCheckable(true);

    QActionGroup *group = new QActionGroup(this);
    group->addAction(sbsAction);
    group->addAction(anaglyphAction);

    connect(anaglyphAction, &QAction::toggled, this, &MainWindow::setAnaglyphMode);
    connect(swapAction, &QAction::toggled, this, &MainWindow::toggleSwapSides);
}

void MainWindow::createDockWidgets()
{
    QDockWidget *dock = new QDockWidget(tr("Point Controls"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget *container = new QWidget;
    QVBoxLayout *mainLayout = new QVBoxLayout(container);

    QLabel *instr = new QLabel(tr("<b>Instructions:</b><br>"
                                  "1. Left-click a point to select.<br>"
                                  "2. Drag with Left-button to move.<br>"
                                  "3. Right-click on image to add a point.<br>"
                                  "4. Adjust 'Depth' for the selected point."));
    instr->setWordWrap(true);
    mainLayout->addWidget(instr);

    QFormLayout *formLayout = new QFormLayout;

    m_disparitySpinBox = new QSpinBox;
    m_disparitySpinBox->setRange(-500, 500);
    formLayout->addRow(tr("Point Depth (Disparity):"), m_disparitySpinBox);

    m_disparitySlider = new QSlider(Qt::Horizontal);
    m_disparitySlider->setRange(-500, 500);
    formLayout->addRow(m_disparitySlider);

    QPushButton *clearBtn = new QPushButton(tr("Clear All Points"));
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearPoints);
    
    QPushButton *deleteBtn = new QPushButton(tr("Delete Selected"));
    deleteBtn->setShortcut(QKeySequence::Delete);
    connect(deleteBtn, &QPushButton::clicked, [this](){ m_viewWidget->deleteSelectedPoints(); });

    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(deleteBtn);
    mainLayout->addWidget(clearBtn);
    mainLayout->addStretch();

    connect(m_disparitySpinBox, QOverload<int>::of(&QSpinBox::valueChanged), m_disparitySlider, &QSlider::setValue);
    connect(m_disparitySlider, &QSlider::valueChanged, m_disparitySpinBox, &QSpinBox::setValue);
    connect(m_disparitySlider, &QSlider::valueChanged, this, &MainWindow::updatePointDisparity);

    dock->setWidget(container);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // Alignment Dock
    QDockWidget *alignDock = new QDockWidget(tr("Alignment"), this);
    QWidget *alignContainer = new QWidget;
    QGridLayout *alignLayout = new QGridLayout(alignContainer);

    QPushButton *alignLeftBtn = new QPushButton(tr("Align Left"));
    QPushButton *alignRightBtn = new QPushButton(tr("Align Right"));
    QPushButton *alignTopBtn = new QPushButton(tr("Align Top"));
    QPushButton *alignBottomBtn = new QPushButton(tr("Align Bottom"));
    QPushButton *alignDepthBtn = new QPushButton(tr("Align Depth"));

    alignLayout->addWidget(alignLeftBtn, 0, 0);
    alignLayout->addWidget(alignRightBtn, 0, 1);
    alignLayout->addWidget(alignTopBtn, 1, 0);
    alignLayout->addWidget(alignBottomBtn, 1, 1);
    alignLayout->addWidget(alignDepthBtn, 2, 0, 1, 2);

    connect(alignLeftBtn, &QPushButton::clicked, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignLeft); });
    connect(alignRightBtn, &QPushButton::clicked, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignRight); });
    connect(alignTopBtn, &QPushButton::clicked, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignTop); });
    connect(alignBottomBtn, &QPushButton::clicked, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignBottom); });
    connect(alignDepthBtn, &QPushButton::clicked, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignDepth); });

    alignDock->setWidget(alignContainer);
    addDockWidget(Qt::RightDockWidgetArea, alignDock);

    // Transform Dock
    QDockWidget *transDock = new QDockWidget(tr("Mask Transform"), this);
    QWidget *transContainer = new QWidget;
    QFormLayout *transLayout = new QFormLayout(transContainer);

    QPushButton *scaleUpBtn = new QPushButton(tr("Scale Up"));
    QPushButton *scaleDownBtn = new QPushButton(tr("Scale Down"));
    QPushButton *moveLeftBtn = new QPushButton(tr("Shift Left"));
    QPushButton *moveRightBtn = new QPushButton(tr("Shift Right"));
    QPushButton *moveUpBtn = new QPushButton(tr("Shift Up"));
    QPushButton *moveDownBtn = new QPushButton(tr("Shift Down"));

    transLayout->addRow(scaleUpBtn, scaleDownBtn);
    transLayout->addRow(moveLeftBtn, moveRightBtn);
    transLayout->addRow(moveUpBtn, moveDownBtn);

    auto transform = [this](float sx, float sy, float dx, float dy) {
        m_viewWidget->transformSelectedPoints(sx, sy, dx, dy);
    };

    connect(scaleUpBtn, &QPushButton::clicked, [transform](){ transform(1.1f, 1.1f, 0, 0); });
    connect(scaleDownBtn, &QPushButton::clicked, [transform](){ transform(0.9f, 0.9f, 0, 0); });
    connect(moveLeftBtn, &QPushButton::clicked, [transform](){ transform(1.0f, 1.0f, -10, 0); });
    connect(moveRightBtn, &QPushButton::clicked, [transform](){ transform(1.0f, 1.0f, 10, 0); });
    connect(moveUpBtn, &QPushButton::clicked, [transform](){ transform(1.0f, 1.0f, 0, -10); });
    connect(moveDownBtn, &QPushButton::clicked, [transform](){ transform(1.0f, 1.0f, 0, 10); });

    transDock->setWidget(transContainer);
    addDockWidget(Qt::RightDockWidgetArea, transDock);

    // ToolBar for Locks
    QToolBar *lockToolBar = addToolBar(tr("Controls"));
    QAction *panAction = lockToolBar->addAction(tr("Pan Mode"));
    panAction->setCheckable(true);
    panAction->setShortcut(Qt::Key_P);
    
    QAction *lockHAction = lockToolBar->addAction(tr("Lock Horizontal"));
    lockHAction->setCheckable(true);
    QAction *lockVAction = lockToolBar->addAction(tr("Lock Vertical"));
    lockVAction->setCheckable(true);

    connect(panAction, &QAction::toggled, this, [this](bool enabled){ m_viewWidget->setPanMode(enabled); });
    connect(lockHAction, &QAction::toggled, this, [this](bool locked){ m_viewWidget->setLockHorizontal(locked); });
    connect(lockVAction, &QAction::toggled, this, [this](bool locked){ m_viewWidget->setLockVertical(locked); });
}

#include <QMessageBox>

void MainWindow::openImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Stereo Image"), "", tr("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (!fileName.isEmpty()) {
        if (!m_viewWidget->loadImage(fileName)) {
            QMessageBox::critical(this, tr("Error"), tr("Could not load image: %1\n\nReason: %2").arg(fileName, m_viewWidget->lastError()));
        }
    }
}

#include <QDialog>
#include <QColorDialog>
#include <QDialogButtonBox>

class ExportDialog : public QDialog {
public:
    ExportDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle(tr("Export Masked Image"));
        QFormLayout *layout = new QFormLayout(this);

        m_maskColorBtn = new QPushButton(tr("Select Color..."));
        m_maskColor = Qt::black;
        connect(m_maskColorBtn, &QPushButton::clicked, [this](){
            QColor c = QColorDialog::getColor(m_maskColor, this);
            if (c.isValid()) m_maskColor = c;
        });
        layout->addRow(tr("Mask Color:"), m_maskColorBtn);

        m_opacitySpin = new QSpinBox;
        m_opacitySpin->setRange(0, 100);
        m_opacitySpin->setValue(60);
        m_opacitySpin->setSuffix("%");
        layout->addRow(tr("Mask Opacity:"), m_opacitySpin);

        m_padxSpin = new QSpinBox;
        m_padxSpin->setRange(0, 2000);
        m_padxSpin->setValue(0);
        m_padxSpin->setSuffix(" px");
        layout->addRow(tr("Horizontal Padding:"), m_padxSpin);

        m_padySpin = new QSpinBox;
        m_padySpin->setRange(0, 2000);
        m_padySpin->setValue(0);
        m_padySpin->setSuffix(" px");
        layout->addRow(tr("Vertical Padding:"), m_padySpin);

        m_bgColorBtn = new QPushButton(tr("Select Color..."));
        m_bgColor = Qt::white;
        connect(m_bgColorBtn, &QPushButton::clicked, [this](){
            QColor c = QColorDialog::getColor(m_bgColor, this);
            if (c.isValid()) m_bgColor = c;
        });
        layout->addRow(tr("Background Color:"), m_bgColorBtn);

        QDialogButtonBox *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(bbox);
    }

    QColor maskColor() const { return m_maskColor; }
    float opacity() const { return m_opacitySpin->value() / 100.0f; }
    int padx() const { return m_padxSpin->value(); }
    int pady() const { return m_padySpin->value(); }
    QColor bgColor() const { return m_bgColor; }

private:
    QPushButton *m_maskColorBtn, *m_bgColorBtn;
    QSpinBox *m_opacitySpin, *m_padxSpin, *m_padySpin;
    QColor m_maskColor, m_bgColor;
};

void MainWindow::saveImage()
{
    if (!m_viewWidget->isImageLoaded()) return;

    ExportDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save Masked Image"), "", tr("Images (*.png *.jpg)"));
        if (!fileName.isEmpty()) {
            m_viewWidget->saveImage(fileName, dlg.maskColor(), dlg.opacity(), dlg.padx(), dlg.pady(), dlg.bgColor());
        }
    }
}

void MainWindow::updatePointDisparity(int value)
{
    m_viewWidget->setSelectedPointDisparity(value); // Need to add this to widget
}

void MainWindow::clearPoints()
{
    m_viewWidget->clearPoints();
}

void MainWindow::setAnaglyphMode(bool enabled)
{
    m_viewWidget->setAnaglyphMode(enabled);
}

void MainWindow::toggleSwapSides(bool enabled)
{
    m_viewWidget->setSwapSides(enabled);
}
