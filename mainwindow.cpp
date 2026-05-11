#include "mainwindow.h"
#include "stereoviewwidget.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QStyle>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QtMath>
#include <cmath>

static QString settingsFilePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        dir = QCoreApplication::applicationDirPath();
    }
    QDir().mkpath(dir);
    return dir + "/settings.json";
}

static QString legacySettingsFilePath()
{
    return QCoreApplication::applicationDirPath() + "/settings.json";
}

void AppSettings::load() {
    QString path = settingsFilePath();
    if (!QFile::exists(path) && QFile::exists(legacySettingsFilePath())) {
        path = legacySettingsFilePath();
    }
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isNull()) fromJson(doc.object());
    }
}

void AppSettings::save() const {
    QString path = settingsFilePath();
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(toJson()).toJson());
        file.commit();
    }
}

QJsonObject AppSettings::toJson() const {
    QJsonObject obj;
    obj["maskColor"] = maskColor.name();
    obj["opacity"] = (double)opacity;
    obj["padx"] = padx;
    obj["pady"] = pady;
    obj["bgColor"] = bgColor.name();
    obj["interleavingSpace"] = interleavingSpace;
    obj["autoSave"] = autoSave;
    obj["recentFiles"] = QJsonArray::fromStringList(recentFiles);
    obj["snapEnabled"] = snapEnabled;
    obj["featherAmount"] = featherAmount;
    obj["lastFolder"] = lastFolder;
    return obj;
}

void AppSettings::fromJson(const QJsonObject &obj) {
    if (obj.contains("maskColor")) maskColor = QColor(obj["maskColor"].toString());
    if (obj.contains("opacity")) opacity = (float)obj["opacity"].toDouble();
    if (obj.contains("padx")) padx = obj["padx"].toInt();
    if (obj.contains("pady")) pady = obj["pady"].toInt();
    if (obj.contains("bgColor")) bgColor = QColor(obj["bgColor"].toString());
    if (obj.contains("interleavingSpace")) interleavingSpace = obj["interleavingSpace"].toInt();
    if (obj.contains("autoSave")) autoSave = obj["autoSave"].toBool();
    if (obj.contains("snapEnabled")) snapEnabled = obj["snapEnabled"].toBool();
    if (obj.contains("featherAmount")) featherAmount = obj["featherAmount"].toInt();
    if (obj.contains("lastFolder")) lastFolder = obj["lastFolder"].toString();
    if (obj.contains("recentFiles")) {
        recentFiles.clear();
        QJsonArray arr = obj["recentFiles"].toArray();
        for (const auto &v : arr) recentFiles << v.toString();
    }
}

void AppSettings::addRecentFile(const QString &path)
{
    recentFiles.removeAll(path);
    recentFiles.prepend(path);
    while (recentFiles.size() > 5) recentFiles.removeLast();
    save();
}

#include <QStatusBar>
#include <QJsonArray>
#include <QScreen>
#include <QGuiApplication>

class StartupSplash : public QWidget {
public:
    StartupSplash(QWidget *parent) : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        QVBoxLayout *layout = new QVBoxLayout(this);
        
        QFrame *frame = new QFrame(this);
        frame->setStyleSheet("QFrame { background-color: #2c3e50; border: 2px solid #34495e; border-radius: 15px; } QLabel { color: white; border: none; }");
        QVBoxLayout *fLayout = new QVBoxLayout(frame);
        
        QPushButton *closeBtn = new QPushButton("×", frame);
        closeBtn->setFixedSize(30, 30);
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setStyleSheet("QPushButton { background: none; border: none; font-size: 24px; color: #95a5a6; } QPushButton:hover { color: #e74c3c; }");
        connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
        
        QHBoxLayout *topRow = new QHBoxLayout;
        topRow->addStretch();
        topRow->addWidget(closeBtn);
        fLayout->addLayout(topRow);

        QLabel *icon = new QLabel(frame);
        icon->setPixmap(QIcon(":/app_icon.svg").pixmap(80, 80));
        icon->setAlignment(Qt::AlignCenter);
        fLayout->addWidget(icon);

        QLabel *text = new QLabel(tr("<h1>StereoMask v1.3.0</h1>"
                                     "<p align='center'><b>Author:</b> S. Rathinagiri</p>"
                                     "<p align='center'>Developed using <b>GEMINI CLI</b> and Qt6.</p>"), frame);
        text->setAlignment(Qt::AlignCenter);
        fLayout->addWidget(text);
        
        layout->addWidget(frame);
        setFixedSize(400, 300);
        
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            int x = screenGeom.x() + (screenGeom.width() - 400) / 2;
            int y = screenGeom.y() + (screenGeom.height() - 300) / 2;
            move(x, y);
        }
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_settings.load();
    m_exportWatcher = new QFutureWatcher<bool>(this);
    m_viewWidget = new StereoViewWidget(this);
    m_viewWidget->setMaskSettings(m_settings.maskColor, m_settings.opacity, m_settings.featherAmount);
    m_viewWidget->setPaddingSettings(m_settings.padx, m_settings.pady, m_settings.bgColor, m_settings.interleavingSpace);
    m_viewWidget->setSnapEnabled(m_settings.snapEnabled);
    m_viewWidget->setAutoSave(m_settings.autoSave);
    setCentralWidget(m_viewWidget);

    setWindowIcon(QIcon(":/app_icon.svg"));

    createMenus();
    createToolbar();
    statusBar()->showMessage(tr("Ready"));

    setWindowTitle(tr("StereoMask"));
    resize(1200, 800);

    setupConnections();

    QTimer::singleShot(200, this, [this](){
        StartupSplash *splash = new StartupSplash(this);
        splash->show();
        QTimer::singleShot(3000, splash, &QWidget::close);
    });
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupConnections()
{
    connect(m_viewWidget, &StereoViewWidget::selectionChanged, this, [this](int disp){
        Q_UNUSED(disp);
    });

    connect(m_exportWatcher, &QFutureWatcher<bool>::finished, this, [this](){
        QString fileName = m_exportWatcher->property("fileName").toString();
        bool exported = m_exportWatcher->result();
        if (exported) {
            statusBar()->showMessage(tr("Image exported to %1").arg(QFileInfo(fileName).fileName()), 5000);
        } else {
            QMessageBox::critical(this, tr("Export Failed"), tr("Could not export image to %1.").arg(fileName));
            statusBar()->showMessage(tr("Export failed"), 5000);
        }
    });
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Project (Open Image)..."), QKeySequence::New, this, &MainWindow::newProject);
    fileMenu->addAction(tr("&Open Mask Project..."), QKeySequence::Open, this, &MainWindow::openProject);
    
    m_recentFilesMenu = fileMenu->addMenu(tr("Recent Projects"));
    for (int i = 0; i < 5; ++i) {
        m_recentFileActions[i] = m_recentFilesMenu->addAction(QString(), this, &MainWindow::openRecentFile);
        m_recentFileActions[i]->setVisible(false);
    }
    updateRecentFileActions();

    fileMenu->addAction(tr("&Save Mask Project"), QKeySequence::Save, this, &MainWindow::saveProject);
    fileMenu->addAction(tr("&Export Image..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this, &MainWindow::exportImage);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = m_viewWidget->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);

    QAction *redoAction = m_viewWidget->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);
}

void MainWindow::updateRecentFileActions()
{
    for (int i = 0; i < 5; ++i) {
        if (i < m_settings.recentFiles.size()) {
            QString text = QString("&%1 %2").arg(i + 1).arg(QFileInfo(m_settings.recentFiles[i]).fileName());
            m_recentFileActions[i]->setText(text);
            m_recentFileActions[i]->setData(m_settings.recentFiles[i]);
            m_recentFileActions[i]->setVisible(true);
        } else {
            m_recentFileActions[i]->setVisible(false);
        }
    }
}

void MainWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action) {
        QString path = action->data().toString();
        if (m_viewWidget->loadProject(path)) {
            m_settings.addRecentFile(path);
            m_settings.lastFolder = QFileInfo(path).absolutePath();
            m_settings.save();
            updateRecentFileActions();
            statusBar()->showMessage(tr("Project loaded: %1").arg(QFileInfo(path).fileName()), 5000);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Could not load project: %1").arg(m_viewWidget->lastError()));
            m_settings.recentFiles.removeAll(path);
            m_settings.save();
            updateRecentFileActions();
        }
    }
}

static QAction *addToolAction(QToolBar *toolbar, const QIcon &icon, const QString &text, const QString &tip)
{
    QAction *action = toolbar->addAction(icon, text);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    return action;
}

static void addToolGroupLabel(QToolBar *toolbar, const QString &text)
{
    QLabel *label = new QLabel(text, toolbar);
    label->setStyleSheet("QLabel { color: #475569; font-weight: 600; padding-left: 8px; padding-right: 2px; }");
    toolbar->addWidget(label);
}

static QIcon customIcon(const QString &name)
{
    constexpr int size = 64;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor ink("#1f2937");
    const QColor blue("#2563eb");
    const QColor green("#16a34a");
    const QColor red("#dc2626");
    const QColor cyan("#0891b2");
    const QColor fill("#f8fafc");

    p.setPen(QPen(QColor("#cbd5e1"), 3));
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(5, 5, 54, 54), 8, 8);

    auto line = [&](QPointF a, QPointF b, QColor c = QColor(), int w = 5) {
        if (!c.isValid()) c = ink;
        p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(a, b);
    };
    auto arrow = [&](QPointF a, QPointF b, QColor c = QColor()) {
        if (!c.isValid()) c = blue;
        line(a, b, c, 5);
        QPointF d = b - a;
        double len = std::hypot(d.x(), d.y());
        if (len == 0) return;
        d /= len;
        QPointF n(-d.y(), d.x());
        line(b, b - d * 10 + n * 6, c, 5);
        line(b, b - d * 10 - n * 6, c, 5);
    };
    auto point = [&](QPointF c, QColor color = QColor()) {
        if (!color.isValid()) color = green;
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(color);
        p.drawEllipse(c, 6, 6);
    };
    auto label = [&](const QString &text, QColor c = QColor(), int pt = 16) {
        if (!c.isValid()) c = ink;
        QFont font = p.font();
        font.setBold(true);
        font.setPointSize(pt);
        p.setFont(font);
        p.setPen(c);
        p.drawText(pixmap.rect(), Qt::AlignCenter, text);
    };

    if (name == "new") {
        p.setPen(QPen(blue, 4)); p.setBrush(Qt::white); p.drawRect(18, 13, 28, 38);
        line({25, 26}, {39, 26}, blue, 4); line({25, 36}, {39, 36}, blue, 4);
    } else if (name == "open") {
        p.setPen(QPen(ink, 4)); p.setBrush(QColor("#fde68a")); p.drawRoundedRect(QRectF(12, 24, 42, 25), 4, 4);
        p.drawRect(15, 18, 18, 10);
    } else if (name == "save") {
        p.setPen(QPen(ink, 4)); p.setBrush(QColor("#bfdbfe")); p.drawRoundedRect(QRectF(14, 13, 36, 38), 4, 4);
        p.setBrush(Qt::white); p.drawRect(21, 36, 22, 12); p.setBrush(blue); p.drawRect(22, 16, 17, 12);
    } else if (name == "export") {
        p.setPen(QPen(ink, 4)); p.setBrush(QColor("#dcfce7")); p.drawRect(13, 18, 38, 28);
        arrow({25, 32}, {45, 32}, green);
    } else if (name == "anaglyph") {
        line({22, 18}, {30, 46}, red, 5); line({42, 18}, {34, 46}, cyan, 5);
        p.setPen(QPen(ink, 4)); p.setBrush(Qt::NoBrush); p.drawArc(14, 18, 17, 18, 0, 360 * 16); p.drawArc(33, 18, 17, 18, 0, 360 * 16);
    } else if (name == "swap") {
        arrow({16, 24}, {48, 24}, blue); arrow({48, 40}, {16, 40}, green);
    } else if (name == "pan") {
        arrow({32, 12}, {32, 24}); arrow({32, 52}, {32, 40}); arrow({12, 32}, {24, 32}); arrow({52, 32}, {40, 32});
    } else if (name == "lockH") {
        arrow({17, 32}, {47, 32}); line({32, 18}, {32, 46}, red, 4);
    } else if (name == "lockV") {
        arrow({32, 17}, {32, 47}); line({18, 32}, {46, 32}, red, 4);
    } else if (name == "snap") {
        point({22, 32}); point({42, 32}); line({22, 32}, {42, 32}, green, 4);
    } else if (name == "settings") {
        p.setPen(QPen(ink, 4)); p.setBrush(QColor("#e0f2fe")); p.drawEllipse(18, 18, 28, 28); p.setBrush(fill); p.drawEllipse(27, 27, 10, 10);
        for (int a = 0; a < 360; a += 60) {
            double r = qDegreesToRadians((double)a);
            line({32 + std::cos(r) * 18, 32 + std::sin(r) * 18}, {32 + std::cos(r) * 25, 32 + std::sin(r) * 25}, ink, 4);
        }
    } else if (name == "undo") {
        arrow({45, 21}, {20, 21}); p.drawArc(18, 18, 30, 28, 200 * 16, 230 * 16);
    } else if (name == "redo") {
        arrow({19, 21}, {44, 21}); p.drawArc(16, 18, 30, 28, -50 * 16, 230 * 16);
    } else if (name == "delete") {
        p.setPen(QPen(red, 5)); line({20, 20}, {44, 44}, red, 6); line({44, 20}, {20, 44}, red, 6);
    } else if (name == "clear") {
        p.setPen(QPen(red, 4)); p.setBrush(Qt::NoBrush); p.drawRect(18, 18, 28, 28); line({16, 16}, {48, 48}, red, 5);
    } else if (name.startsWith("align")) {
        QString dir = name.mid(5);
        line({16, 16}, {16, 48}, ink, 4); line({48, 16}, {48, 48}, ink, 4);
        if (dir == "Left") { point({28, 24}); point({28, 40}); arrow({42, 32}, {22, 32}); }
        else if (dir == "Right") { point({36, 24}); point({36, 40}); arrow({22, 32}, {42, 32}); }
        else if (dir == "Top") { point({24, 34}); point({40, 34}); arrow({32, 48}, {32, 20}); }
        else if (dir == "Bottom") { point({24, 30}); point({40, 30}); arrow({32, 16}, {32, 44}); }
        else { point({20, 42}); point({32, 30}); point({44, 18}); line({20, 42}, {44, 18}, cyan, 4); }
    } else if (name == "scaleUp") {
        arrow({25, 39}, {18, 46}, green); arrow({39, 25}, {46, 18}, green); label("+", green, 18);
    } else if (name == "scaleDown") {
        arrow({16, 48}, {26, 38}, green); arrow({48, 16}, {38, 26}, green); label("-", green, 20);
    } else if (name == "shiftLeft") arrow({46, 32}, {18, 32});
    else if (name == "shiftRight") arrow({18, 32}, {46, 32});
    else if (name == "shiftUp") arrow({32, 46}, {32, 18});
    else if (name == "shiftDown") arrow({32, 18}, {32, 46});
    else if (name.startsWith("rot")) {
        p.setPen(QPen(blue, 5, Qt::SolidLine, Qt::RoundCap)); p.setBrush(Qt::NoBrush); p.drawArc(17, 17, 30, 30, 35 * 16, 290 * 16);
        arrow({42, 18}, {49, 27}, blue); label(name.right(1).toUpper(), ink, 14);
    } else if (name == "color") {
        p.setPen(Qt::NoPen); p.setBrush(red); p.drawEllipse(18, 18, 14, 14); p.setBrush(green); p.drawEllipse(32, 18, 14, 14); p.setBrush(blue); p.drawEllipse(25, 32, 14, 14);
    } else if (name == "curve") {
        p.setPen(QPen(blue, 5, Qt::SolidLine, Qt::RoundCap)); QPainterPath path; path.moveTo(16, 43); path.cubicTo(20, 12, 45, 52, 49, 20); p.drawPath(path);
        point({16, 43}, blue); point({49, 20}, blue);
    } else {
        label("?", ink, 18);
    }

    return QIcon(pixmap);
}

void MainWindow::createToolbar()
{
    auto configureToolbar = [](QToolBar *toolbar) {
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(20, 20));
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    };

    QToolBar *fileViewBar = addToolBar(tr("File and View"));
    configureToolbar(fileViewBar);

    addToolGroupLabel(fileViewBar, tr("File"));
    QAction *newAction = addToolAction(fileViewBar, customIcon("new"), tr("New"), tr("New Project (Open Image)"));
    connect(newAction, &QAction::triggered, this, &MainWindow::newProject);

    QAction *openAction = addToolAction(fileViewBar, customIcon("open"), tr("Open"), tr("Open Mask Project (.msk)"));
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);

    QAction *saveAction = addToolAction(fileViewBar, customIcon("save"), tr("Save"), tr("Save Mask Project (Ctrl+S)"));
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);

    QAction *exportAction = addToolAction(fileViewBar, customIcon("export"), tr("Export"), tr("Export Masked Image"));
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportImage);
    fileViewBar->addSeparator();

    addToolGroupLabel(fileViewBar, tr("View"));
    QAction *anaglyph = addToolAction(fileViewBar, customIcon("anaglyph"), tr("Anaglyph"), tr("Toggle Anaglyph"));
    anaglyph->setCheckable(true);
    connect(anaglyph, &QAction::toggled, this, &MainWindow::setAnaglyphMode);
    
    QAction *swap = addToolAction(fileViewBar, customIcon("swap"), tr("Swap"), tr("Swap Left/Right"));
    swap->setCheckable(true);
    connect(swap, &QAction::toggled, this, &MainWindow::toggleSwapSides);
    
    QAction *pan = addToolAction(fileViewBar, customIcon("pan"), tr("Pan"), tr("Pan Mode (P)"));
    pan->setCheckable(true);
    pan->setShortcut(Qt::Key_P);
    connect(pan, &QAction::toggled, m_viewWidget, &StereoViewWidget::setPanMode);
    fileViewBar->addSeparator();

    addToolGroupLabel(fileViewBar, tr("Assist"));
    QAction *lockH = addToolAction(fileViewBar, customIcon("lockH"), tr("H"), tr("Lock Horizontal"));
    lockH->setCheckable(true);
    connect(lockH, &QAction::toggled, m_viewWidget, &StereoViewWidget::setLockHorizontal);

    QAction *lockV = addToolAction(fileViewBar, customIcon("lockV"), tr("V"), tr("Lock Vertical"));
    lockV->setCheckable(true);
    connect(lockV, &QAction::toggled, m_viewWidget, &StereoViewWidget::setLockVertical);
    
    QAction *snap = addToolAction(fileViewBar, customIcon("snap"), tr("Snap"), tr("Snap to Points (S)"));
    snap->setCheckable(true);
    snap->setShortcut(Qt::Key_S);
    snap->setChecked(m_settings.snapEnabled);
    connect(snap, &QAction::toggled, this, [this](bool checked){
        m_settings.snapEnabled = checked;
        m_settings.save();
        m_viewWidget->setSnapEnabled(checked);
    });

    fileViewBar->addSeparator();
    addToolGroupLabel(fileViewBar, tr("App"));
    QAction *settings = addToolAction(fileViewBar, customIcon("settings"), tr("Settings"), tr("Mask Settings"));
    connect(settings, &QAction::triggered, this, &MainWindow::showSettings);
    QAction *help = addToolAction(fileViewBar, style()->standardIcon(QStyle::SP_MessageBoxQuestion), tr("Help"), tr("Help / Shortcuts"));
    connect(help, &QAction::triggered, this, &MainWindow::showHelp);
    QAction *about = addToolAction(fileViewBar, style()->standardIcon(QStyle::SP_MessageBoxInformation), tr("About"), tr("About StereoMask"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    addToolBarBreak(Qt::TopToolBarArea);

    QToolBar *editMaskBar = addToolBar(tr("Edit, Mask and Transform"));
    configureToolbar(editMaskBar);

    addToolGroupLabel(editMaskBar, tr("Edit"));
    QAction *undo = m_viewWidget->undoStack()->createUndoAction(this, tr("Undo"));
    undo->setIcon(customIcon("undo"));
    undo->setToolTip(tr("Undo"));
    editMaskBar->addAction(undo);

    QAction *redo = m_viewWidget->undoStack()->createRedoAction(this, tr("Redo"));
    redo->setIcon(customIcon("redo"));
    redo->setToolTip(tr("Redo"));
    editMaskBar->addAction(redo);

    QAction *deleteSelected = addToolAction(editMaskBar, customIcon("delete"), tr("Delete"), tr("Delete Selected"));
    connect(deleteSelected, &QAction::triggered, m_viewWidget, &StereoViewWidget::deleteSelectedPoints);

    QAction *clear = addToolAction(editMaskBar, customIcon("clear"), tr("Clear"), tr("Clear All Points"));
    connect(clear, &QAction::triggered, this, &MainWindow::clearPoints);
    editMaskBar->addSeparator();

    addToolGroupLabel(editMaskBar, tr("Align"));
    QAction *alignLeft = addToolAction(editMaskBar, customIcon("alignLeft"), tr("Left"), tr("Align Left"));
    connect(alignLeft, &QAction::triggered, this, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignLeft); });
    QAction *alignRight = addToolAction(editMaskBar, customIcon("alignRight"), tr("Right"), tr("Align Right"));
    connect(alignRight, &QAction::triggered, this, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignRight); });
    QAction *alignTop = addToolAction(editMaskBar, customIcon("alignTop"), tr("Top"), tr("Align Top"));
    connect(alignTop, &QAction::triggered, this, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignTop); });
    QAction *alignBottom = addToolAction(editMaskBar, customIcon("alignBottom"), tr("Bottom"), tr("Align Bottom"));
    connect(alignBottom, &QAction::triggered, this, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignBottom); });
    QAction *alignDepth = addToolAction(editMaskBar, customIcon("alignDepth"), tr("Depth"), tr("Align Depth"));
    connect(alignDepth, &QAction::triggered, this, [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignDepth); });
    editMaskBar->addSeparator();

    addToolGroupLabel(editMaskBar, tr("Scale"));
    QAction *scaleUp = addToolAction(editMaskBar, customIcon("scaleUp"), tr("Up"), tr("Scale Up"));
    connect(scaleUp, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.1f, 1.1f, 0, 0); });
    QAction *scaleDown = addToolAction(editMaskBar, customIcon("scaleDown"), tr("Down"), tr("Scale Down"));
    connect(scaleDown, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(0.9f, 0.9f, 0, 0); });
    editMaskBar->addSeparator();

    addToolGroupLabel(editMaskBar, tr("Move"));
    QAction *shiftLeft = addToolAction(editMaskBar, customIcon("shiftLeft"), tr("Left"), tr("Shift Left"));
    connect(shiftLeft, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, -10, 0); });
    QAction *shiftRight = addToolAction(editMaskBar, customIcon("shiftRight"), tr("Right"), tr("Shift Right"));
    connect(shiftRight, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 10, 0); });
    QAction *shiftUp = addToolAction(editMaskBar, customIcon("shiftUp"), tr("Up"), tr("Shift Up"));
    connect(shiftUp, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 0, -10); });
    QAction *shiftDown = addToolAction(editMaskBar, customIcon("shiftDown"), tr("Down"), tr("Shift Down"));
    connect(shiftDown, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 0, 10); });
    editMaskBar->addSeparator();

    addToolGroupLabel(editMaskBar, tr("Curve / Rotate"));
    QAction *curve = addToolAction(editMaskBar, customIcon("curve"), tr("Curve"), tr("Toggle Curve (C)"));
    curve->setCheckable(true);
    connect(curve, &QAction::triggered, this, &MainWindow::toggleCurve);
    connect(m_viewWidget, &StereoViewWidget::curveSelectionChanged, curve, [curve](bool hasCurveSelection){
        QSignalBlocker blocker(curve);
        curve->setChecked(hasCurveSelection);
    });

    m_rotationActions.clear();
    auto addRotBtn = [&](const QString &text, StereoViewWidget::RotationAxis axis, const QString &tip) {
        QAction *a = addToolAction(editMaskBar, customIcon(axis == StereoViewWidget::AxisX ? "rotX" : axis == StereoViewWidget::AxisY ? "rotY" : "rotZ"), text, tip);
        a->setCheckable(true);
        a->setEnabled(false);
        m_rotationActions.append(a);
        connect(a, &QAction::toggled, this, [this, axis, a](bool checked){
            if (checked) {
                for (QAction *other : m_rotationActions) {
                    if (other != a) {
                        QSignalBlocker blocker(other);
                        other->setChecked(false);
                    }
                }
                m_viewWidget->setRotationMode(axis);
            } else if (m_viewWidget->rotationMode() == axis) {
                m_viewWidget->setRotationMode(StereoViewWidget::AxisNone);
            }
        });
        return a;
    };
    addRotBtn("Rx", StereoViewWidget::AxisX, tr("Rotate Mode X (Tilt Up/Down)"));
    addRotBtn("Ry", StereoViewWidget::AxisY, tr("Rotate Mode Y (Tilt Left/Right)"));
    addRotBtn("Rz", StereoViewWidget::AxisZ, tr("Rotate Mode Z (Roll)"));
    editMaskBar->addSeparator();

    connect(m_viewWidget, &StereoViewWidget::curveSelectionChanged, this, [this](bool hasCurveSelection){
        if (!hasCurveSelection) {
            for (QAction *action : m_rotationActions) {
                QSignalBlocker blocker(action);
                action->setChecked(false);
                action->setEnabled(false);
            }
            m_viewWidget->setRotationMode(StereoViewWidget::AxisNone);
            return;
        }
        for (QAction *action : m_rotationActions) {
            action->setEnabled(true);
        }
    });

    addToolGroupLabel(editMaskBar, tr("Mask"));
    QAction *color = addToolAction(editMaskBar, customIcon("color"), tr("Color"), tr("Quick Mask Color"));
    connect(color, &QAction::triggered, this, [this](){
        QColor c = QColorDialog::getColor(m_viewWidget->maskColor(), this, tr("Select Mask Color"));
        if (c.isValid()) m_viewWidget->setMaskColor(c);
    });

    m_opacitySpin = new QSpinBox;
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix("%");
    m_opacitySpin->setToolTip(tr("Quick Mask Opacity"));
    m_opacitySpin->setValue(m_viewWidget->maskOpacity() * 100);
    connect(m_opacitySpin, &QSpinBox::valueChanged, this, [this](int v){
        m_viewWidget->setMaskOpacity(v / 100.0f);
    });
    editMaskBar->addWidget(m_opacitySpin);

}

#include <QCheckBox>

class SettingsDialog : public QDialog {
public:
    SettingsDialog(AppSettings &s, QWidget *parent = nullptr) : QDialog(parent), m_settings(s) {
        setWindowTitle(tr("Mask Settings"));
        QFormLayout *layout = new QFormLayout(this);

        m_maskColorBtn = new QPushButton(tr("Select Color..."));
        updateColorBtn(m_maskColorBtn, m_settings.maskColor);
        connect(m_maskColorBtn, &QPushButton::clicked, [this](){
            QColor c = QColorDialog::getColor(m_settings.maskColor, this);
            if (c.isValid()) { m_settings.maskColor = c; updateColorBtn(m_maskColorBtn, c); }
        });
        layout->addRow(tr("Mask Color:"), m_maskColorBtn);

        m_opacitySpin = new QSpinBox;
        m_opacitySpin->setRange(0, 100);
        m_opacitySpin->setValue(m_settings.opacity * 100);
        m_opacitySpin->setSuffix("%");
        layout->addRow(tr("Mask Opacity:"), m_opacitySpin);

        m_featherSpin = new QSpinBox;
        m_featherSpin->setRange(0, 100);
        m_featherSpin->setValue(m_settings.featherAmount);
        m_featherSpin->setSuffix(" px");
        layout->addRow(tr("Mask Feathering:"), m_featherSpin);

        m_padxSpin = new QSpinBox;
        m_padxSpin->setRange(0, 2000);
        m_padxSpin->setValue(m_settings.padx);
        m_padxSpin->setSuffix(" px");
        layout->addRow(tr("Horizontal Padding:"), m_padxSpin);

        m_padySpin = new QSpinBox;
        m_padySpin->setRange(0, 2000);
        m_padySpin->setValue(m_settings.pady);
        m_padySpin->setSuffix(" px");
        layout->addRow(tr("Vertical Padding:"), m_padySpin);

        m_interleavingSpin = new QSpinBox;
        m_interleavingSpin->setRange(0, 2000);
        m_interleavingSpin->setValue(m_settings.interleavingSpace);
        m_interleavingSpin->setSuffix(" px");
        layout->addRow(tr("Interleaving Space:"), m_interleavingSpin);

        m_bgColorBtn = new QPushButton(tr("Select Color..."));
        updateColorBtn(m_bgColorBtn, m_settings.bgColor);
        connect(m_bgColorBtn, &QPushButton::clicked, [this](){
            QColor c = QColorDialog::getColor(m_settings.bgColor, this);
            if (c.isValid()) { m_settings.bgColor = c; updateColorBtn(m_bgColorBtn, c); }
        });
        layout->addRow(tr("Background Color:"), m_bgColorBtn);

        m_autoSaveCheck = new QCheckBox(tr("AutoSave Project Changes"));
        m_autoSaveCheck->setChecked(m_settings.autoSave);
        layout->addRow(m_autoSaveCheck);

        QDialogButtonBox *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(bbox, &QDialogButtonBox::accepted, [this](){
            m_settings.opacity = m_opacitySpin->value() / 100.0f;
            m_settings.featherAmount = m_featherSpin->value();
            m_settings.padx = m_padxSpin->value();
            m_settings.pady = m_padySpin->value();
            m_settings.interleavingSpace = m_interleavingSpin->value();
            m_settings.autoSave = m_autoSaveCheck->isChecked();
            accept();
        });
        connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addRow(bbox);
    }

private:
    void updateColorBtn(QPushButton *btn, const QColor &c) {
        btn->setStyleSheet(QString("background-color: %1;").arg(c.name()));
    }
    AppSettings &m_settings;
    QPushButton *m_maskColorBtn, *m_bgColorBtn;
    QSpinBox *m_opacitySpin, *m_padxSpin, *m_padySpin, *m_interleavingSpin, *m_featherSpin;
    QCheckBox *m_autoSaveCheck;
};

void MainWindow::showSettings()
{
    m_settings.maskColor = m_viewWidget->maskColor();
    m_settings.opacity = m_viewWidget->maskOpacity();
    m_settings.featherAmount = m_viewWidget->featherAmount();
    m_settings.padx = m_viewWidget->padx();
    m_settings.pady = m_viewWidget->pady();
    m_settings.bgColor = m_viewWidget->bgColor();
    m_settings.interleavingSpace = m_viewWidget->interleavingSpace();

    SettingsDialog dlg(m_settings, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings.save();
        m_viewWidget->setMaskSettings(m_settings.maskColor, m_settings.opacity, m_settings.featherAmount);
        m_viewWidget->setPaddingSettings(m_settings.padx, m_settings.pady, m_settings.bgColor, m_settings.interleavingSpace);
        m_viewWidget->setAutoSave(m_settings.autoSave);
        statusBar()->showMessage(tr("Settings updated"), 3000);
    }
}

void MainWindow::showHelp()
{
    QMessageBox::information(this, tr("Help & Shortcuts"),
        tr("<h3>Toolbar Icons:</h3>"
           "<b>New/Open/Save/Export:</b> File operations and final render.<br>"
           "<b>Anaglyph/Swap/Pan:</b> Preview and navigation tools.<br>"
           "<b>Lock H/V and Snap:</b> Movement constraints and point snapping.<br>"
           "<b>Undo/Redo/Delete/Clear:</b> Edit mask points.<br>"
           "<b>Align L/R/T/B/Depth:</b> Align selected points by position or depth.<br>"
           "<b>Scale and Shift:</b> Transform selected points or the whole mask.<br>"
           "<b>Rx/Ry/Rz:</b> Rotate selected points around X, Y, or Z depth axes.<br>"
           "<b>Color/Opacity/Curve:</b> Adjust mask appearance and curve segments.<br>"
           "<b>Settings/Help/About:</b> Application settings and reference.<br>"
           "<br><h3>Shortcuts:</h3>"
           "<b>Right-Click:</b> Add new point (near edge to insert).<br>"
           "<b>Left-Drag:</b> Move points or selection box.<br>"
           "<b>Left-Drag (Green Squares):</b> Move Bezier control handles.<br>"
           "<b>Shift + Left-Drag:</b> Adjust point or handle depth (Disparity).<br>"
           "<b>Ctrl + Left-Click:</b> Multi-select points.<br>"
           "<b>Del:</b> Delete selected points."));
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About StereoMask"),
        tr("<h2>StereoMask v1.3.0</h2>"
           "<p>A precision masking tool for side-by-side stereo images.</p>"
           "<p><b>Author:</b> S. Rathinagiri</p>"
           "<p>Developed using <b>GEMINI CLI</b> and Qt6.</p>"
           "<p>Built for the stereo photography community.</p>"));
}

void MainWindow::newProject()
{
    QString start = m_settings.lastFolder.isEmpty() ? "" : m_settings.lastFolder;
    QString fileName = QFileDialog::getOpenFileName(this, tr("New Project (Open Image)"), start, tr("Images (*.png *.jpg *.jpeg *.bmp)"));
    if (!fileName.isEmpty()) {
        m_settings.lastFolder = QFileInfo(fileName).absolutePath();
        m_settings.save();
        if (m_viewWidget->loadImage(fileName)) {
            QFileInfo info(fileName);
            QString mskPath = info.absolutePath() + "/" + info.completeBaseName() + ".msk";
            m_settings.addRecentFile(mskPath);
            updateRecentFileActions();
            statusBar()->showMessage(tr("New project started: %1").arg(info.fileName()), 5000);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Could not load image: %1\n\nReason: %2").arg(fileName, m_viewWidget->lastError()));
        }
    }
}

void MainWindow::openProject()
{
    QString start = m_settings.lastFolder.isEmpty() ? "" : m_settings.lastFolder;
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Mask Project"), start, tr("Mask Project (*.msk)"));
    if (!fileName.isEmpty()) {
        m_settings.lastFolder = QFileInfo(fileName).absolutePath();
        m_settings.save();
        if (m_viewWidget->loadProject(fileName)) {
            m_settings.addRecentFile(fileName);
            updateRecentFileActions();
            statusBar()->showMessage(tr("Project loaded: %1").arg(QFileInfo(fileName).fileName()), 5000);
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Could not load project: %1").arg(m_viewWidget->lastError()));
        }
    }
}

void MainWindow::saveProject()
{
    if (!m_viewWidget->isImageLoaded()) return;
    if (m_viewWidget->saveProject()) {
        QFileInfo info(m_viewWidget->imagePath());
        QString mskPath = info.absolutePath() + "/" + info.completeBaseName() + ".msk";
        m_settings.addRecentFile(mskPath);
        updateRecentFileActions();
        statusBar()->showMessage(tr("Project saved successfully"), 3000);
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Could not save project."));
    }
}

void MainWindow::exportImage()
{
    if (!m_viewWidget->isImageLoaded()) return;
    if (m_exportWatcher->isRunning()) {
        statusBar()->showMessage(tr("Export already in progress"), 3000);
        return;
    }

    QFileInfo info(m_viewWidget->imagePath());
    QString defaultName = info.absolutePath() + "/" + info.completeBaseName() + "_masked.png";
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Masked Image"), defaultName, tr("Images (*.png *.jpg)"));
    if (!fileName.isEmpty()) {
        QImage left = m_viewWidget->leftImage();
        QImage right = m_viewWidget->rightImage();
        QVector<MaskPoint> points = m_viewWidget->points();
        QColor maskColor = m_viewWidget->maskColor();
        float opacity = m_viewWidget->maskOpacity();
        int padx = m_viewWidget->padx();
        int pady = m_viewWidget->pady();
        QColor bgColor = m_viewWidget->bgColor();
        int interleavingSpace = m_viewWidget->interleavingSpace();
        int featherAmount = m_viewWidget->featherAmount();
        QPointer<MainWindow> self(this);

        statusBar()->showMessage(tr("Please wait... Exporting (0%)"));
        m_exportWatcher->setProperty("fileName", fileName);
        m_exportWatcher->setFuture(QtConcurrent::run([=]() {
            return StereoViewWidget::saveImageData(fileName, left, right, points, maskColor, opacity, padx, pady,
                                                   bgColor, interleavingSpace, featherAmount,
                                                   [self](int pct) {
                                                       if (!self) return;
                                                       QMetaObject::invokeMethod(self, [self, pct]() {
                                                           if (self) self->statusBar()->showMessage(self->tr("Please wait... Exporting (%1%)").arg(pct));
                                                       }, Qt::QueuedConnection);
                                                   });
        }));
    }
}

void MainWindow::updatePointDisparity(int value)
{
    m_viewWidget->setSelectedPointDisparity(value);
}

void MainWindow::clearPoints()
{
    if (m_viewWidget->isImageLoaded() && !m_viewWidget->undoStack()->isClean()) {
        if (QMessageBox::question(this, tr("Confirm Clear"), 
            tr("Are you sure you want to clear ALL mask points? This cannot be undone."),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }
    m_viewWidget->clearPoints();
}

void MainWindow::toggleCurve()
{
    m_viewWidget->toggleCurve();
}

void MainWindow::setAnaglyphMode(bool enabled)
{
    m_viewWidget->setAnaglyphMode(enabled);
}

void MainWindow::toggleSwapSides(bool enabled)
{
    m_viewWidget->setSwapSides(enabled);
}
