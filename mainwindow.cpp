#include <QComboBox>
#include "stereowindowview.h"
#include "updatechecker.h"
#include "appversion.h"
#include <QShortcut>
#include <QSettings>
#include <QWidgetAction>
#include "mainwindow.h"
#include "stereoviewwidget.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
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
#include <QDesktopServices>
#include <QUrl>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QStyle>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QActionGroup>
#include <QDockWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QSlider>
#include <QKeyEvent>
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

        QLabel *text = new QLabel(tr("<h1>StereoMask v2.1.1</h1>"
                                     "<p align='center'><b>Stereomasken creator:</b> Bernd Paksa</p>"
                                     "<p align='center'><b>StereoMask Windows:</b> Subbiah Rathinagiri</p><p align='center'><b>Mac port &amp; editing improvements:</b> Theodore Whitten</p>"
                                     "<p align='center'>Original app: <b>GEMINI CLI</b> and Qt6.<br>Mac improvements: <b>Codex</b> assistance.</p>"), frame);
        text->setAlignment(Qt::AlignCenter);
        fLayout->addWidget(text);
        
        layout->addWidget(frame);
        setFixedSize(460, 360);
        
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect screenGeom = screen->geometry();
            int x = screenGeom.x() + (screenGeom.width() - 460) / 2;
            int y = screenGeom.y() + (screenGeom.height() - 360) / 2;
            move(x, y);
        }
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_settings.load();
    m_updateChecker = new UpdateChecker(this);
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
    connect(m_updateChecker, &UpdateChecker::finished, this, [this](bool newer, const UpdateInfo &info, const QString &error) {
        m_checkUpdatesAction->setEnabled(true);
        statusBar()->showMessage(error.isEmpty() ? tr("Update check finished") : tr("Update check failed"), 5000);
        QString text;
        if (!error.isEmpty()) text = error;
        else if (newer) text = tr("A newer StereoMask Windows version is available.\n\nInstalled: %1\nAvailable: %2\n\n%3")
            .arg(STEREOMASK_VERSION, info.version, info.notes);
        else text = tr("You’re up to date.\n\nInstalled version: %1\nLatest published version: %2").arg(STEREOMASK_VERSION, info.version);
        if (newer && info.packageType == "source") text += tr("\n\nThis is a source download that must be built on Windows.");
        QMessageBox dialog(error.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
            tr("Check for Updates"), text, QMessageBox::NoButton, this);
        dialog.setTextFormat(Qt::PlainText);
        QPushButton *open = nullptr;
        if (newer || !error.isEmpty()) open = dialog.addButton(tr("Open GitHub"), QMessageBox::AcceptRole);
        dialog.addButton(tr("Close"), QMessageBox::RejectRole);
        dialog.exec();
        if (open && dialog.clickedButton() == open) {
            const QUrl url = newer ? info.page : QUrl("https://github.com/SRathinaGiri/StereoMask");
            if (!QDesktopServices::openUrl(url)) QMessageBox::information(this, tr("Open GitHub"), tr("Open this address in your browser:\n%1").arg(url.toString()));
        }
    });
    statusBar()->showMessage(tr("Ready"));

    setWindowTitle(tr("StereoMask"));
    resize(1200, 800);

    setupConnections();
    setupKeyboardShortcuts();

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
    
    m_recentFilesMenu = fileMenu->addMenu(tr("Open Recent"));
    m_recentFilesMenu->setObjectName("openRecentMenu");
    for (int i = 0; i < 5; ++i) {
        m_recentFileActions[i] = m_recentFilesMenu->addAction(QString(), this, &MainWindow::openRecentFile);
        m_recentFileActions[i]->setVisible(false);
    }
    updateRecentFileActions();

    fileMenu->addAction(tr("&Save Mask Project"), QKeySequence::Save, this, &MainWindow::saveProject);
    fileMenu->addAction(tr("&Export Image..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this, &MainWindow::exportImage);
    fileMenu->addAction(tr("&Close Project"), QKeySequence::Close, this, &MainWindow::closeProject)->setObjectName("closeProjectAction");
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = m_viewWidget->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);

    QAction *redoAction = m_viewWidget->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("User Guide…"), this, &MainWindow::showHelp);
    auto keys = helpMenu->addAction(tr("Keyboard Shortcuts…"), this, &MainWindow::showKeyboardShortcuts);
    keys->setObjectName("keyboardShortcutsAction");
    m_checkUpdatesAction = helpMenu->addAction(tr("Check for Updates…"), this, &MainWindow::checkForUpdates);
    helpMenu->addAction(tr("About StereoMask"), this, &MainWindow::showAbout);
}

void MainWindow::updateRecentFileActions()
{
    m_recentFilesMenu->menuAction()->setEnabled(!m_settings.recentFiles.isEmpty());
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
    } else if (name == "freehand") {
        p.setPen(QPen(cyan, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(15, 40);
        path.cubicTo(20, 18, 29, 50, 36, 28);
        path.cubicTo(42, 10, 48, 30, 50, 18);
        p.drawPath(path);
        p.setPen(QPen(green, 3));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF({{17, 42}, {28, 35}, {39, 30}, {48, 20}}));
    } else if (name == "eyedropper") {
        line({20,44},{41,23},blue,6);
        p.setPen(QPen(ink,3));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(46,18),6,6);
        line({15,49},{22,42},ink,3);
    } else if (name == "pairedLasso") {
        p.setPen(QPen(blue,4)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(20,32),10,10); p.drawEllipse(QPointF(44,32),10,10);
        line({28,32},{36,32},ink,4);
    } else if (name == "eye" || name == "eyeHidden") {
        p.setPen(QPen(ink,4)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(12,22,40,20));
        p.setBrush(ink); p.drawEllipse(QPointF(32,32),5,5);
        if (name == "eyeHidden") line({13,49},{51,15},ink,4);
    } else if (name == "autoMask") {
        p.setPen(QPen(blue, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor("#dbeafe"));
        p.drawPolygon(QPolygonF({{18, 18}, {48, 18}, {43, 46}, {14, 42}}));
        p.setPen(QPen(green, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(22, 28, 40, 28);
        p.drawLine(40, 28, 34, 22);
        p.drawLine(40, 28, 34, 34);
    } else if (name == "featherPreview") {
        p.setPen(QPen(cyan, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(18, 18, 28, 28));
        p.setPen(QPen(cyan.lighter(150), 2, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawEllipse(QRectF(12, 12, 40, 40));
        p.drawEllipse(QRectF(7, 7, 50, 50));
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

    QAction *windowDepth = addToolAction(fileViewBar, customIcon("anaglyph"), tr("Stereo Window"),
                                         tr("First step: adjust the starting image stereo depth"));
    windowDepth->setObjectName("stereoWindowAction");
    windowDepth->setEnabled(m_viewWidget->isImageLoaded());
    connect(m_viewWidget, &StereoViewWidget::maskEmptyChanged, windowDepth,
            [this, windowDepth](bool) { windowDepth->setEnabled(m_viewWidget->isImageLoaded()); });
    connect(windowDepth, &QAction::triggered, this, &MainWindow::adjustStereoWindow);

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
    QAction *settings = addToolAction(fileViewBar, customIcon("settings"), tr("Background & Export Settings"), tr("Outside-area color/opacity, feathering, export padding and other settings"));
    connect(settings, &QAction::triggered, this, &MainWindow::showSettings);
    QAction *help = addToolAction(fileViewBar, style()->standardIcon(QStyle::SP_MessageBoxQuestion), tr("Help"), tr("Open the User Guide in your web browser"));
    connect(help, &QAction::triggered, this, &MainWindow::showHelp);
    QAction *about = addToolAction(fileViewBar, style()->standardIcon(QStyle::SP_MessageBoxInformation), tr("About"), tr("About StereoMask"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    addToolBarBreak(Qt::TopToolBarArea);

    QToolBar *editMaskBar = addToolBar(tr("Edit and Align"));
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

    QAction *deleteSelected = addToolAction(editMaskBar, customIcon("delete"), tr("Delete"), tr("Delete Selected (Del)"));
    deleteSelected->setShortcut(QKeySequence::Delete);
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

    addToolBarBreak(Qt::TopToolBarArea);

    QToolBar *transformBar = addToolBar(tr("Transform"));
    configureToolbar(transformBar);

    addToolGroupLabel(transformBar, tr("Scale"));
    QAction *scaleUp = addToolAction(transformBar, customIcon("scaleUp"), tr("Up"), tr("Scale Up"));
    connect(scaleUp, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.1f, 1.1f, 0, 0); });
    QAction *scaleDown = addToolAction(transformBar, customIcon("scaleDown"), tr("Down"), tr("Scale Down"));
    connect(scaleDown, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(0.9f, 0.9f, 0, 0); });
    transformBar->addSeparator();

    addToolGroupLabel(transformBar, tr("Move"));
    QAction *shiftLeft = addToolAction(transformBar, customIcon("shiftLeft"), tr("Left"), tr("Shift Left"));
    connect(shiftLeft, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, -10, 0); });
    QAction *shiftRight = addToolAction(transformBar, customIcon("shiftRight"), tr("Right"), tr("Shift Right"));
    connect(shiftRight, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 10, 0); });
    QAction *shiftUp = addToolAction(transformBar, customIcon("shiftUp"), tr("Up"), tr("Shift Up"));
    connect(shiftUp, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 0, -10); });
    QAction *shiftDown = addToolAction(transformBar, customIcon("shiftDown"), tr("Down"), tr("Shift Down"));
    connect(shiftDown, &QAction::triggered, this, [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 0, 10); });
    transformBar->addSeparator();

    addToolGroupLabel(transformBar, tr("Curve / Rotate"));
    QAction *curve = addToolAction(transformBar, customIcon("curve"), tr("Curve"), tr("Toggle Curve (C)"));
    curve->setCheckable(true);
    connect(curve, &QAction::triggered, this, &MainWindow::toggleCurve);
    connect(m_viewWidget, &StereoViewWidget::curveSelectionChanged, curve, [curve](bool hasCurveSelection){
        QSignalBlocker blocker(curve);
        curve->setChecked(hasCurveSelection);
    });

    m_rotationActions.clear();
    auto addRotBtn = [&](const QString &text, StereoViewWidget::RotationAxis axis, const QString &tip) {
        QAction *a = addToolAction(transformBar, customIcon(axis == StereoViewWidget::AxisX ? "rotX" : axis == StereoViewWidget::AxisY ? "rotY" : "rotZ"), text, tip);
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

    addToolBarBreak(Qt::TopToolBarArea);

    QToolBar *maskBar = addToolBar(tr("Mask"));
    configureToolbar(maskBar);

    QAction *pictureFrame=addToolAction(maskBar,customIcon("polygon"),tr("Picture Frame"),tr("Add a colored flat, beveled or decorative border to your frame"));
    pictureFrame->setObjectName("pictureFrameAction");
    connect(pictureFrame,&QAction::triggered,this,&MainWindow::adjustPictureFrame);
    QAction *frameShapes=addToolAction(maskBar,customIcon("autoMask"),tr("Frame Shapes"),tr("Create a Rectangle, Square, Oval or Circle frame"));
    frameShapes->setObjectName("frameShapesAction");
    connect(frameShapes,&QAction::triggered,this,&MainWindow::createFrameShape);
    QAction *stereoText=addToolAction(maskBar,customIcon("curve"),tr("3D Text & Bubbles"),tr("Add stereo text, speech bubbles and thought bubbles"));
    stereoText->setObjectName("stereoTextAction");
    connect(stereoText,&QAction::triggered,this,&MainWindow::editStereoText);

    addToolGroupLabel(maskBar, tr("Mask"));
    QToolButton *autoMaskButton = new QToolButton(maskBar);
    autoMaskButton->setIcon(customIcon("autoMask"));
    autoMaskButton->setText(tr("Auto"));
    autoMaskButton->setToolTip(tr("Create editable mask points from stereo overlap"));
    autoMaskButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    autoMaskButton->setPopupMode(QToolButton::MenuButtonPopup);
    autoMaskButton->setEnabled(m_viewWidget->canAutoMask());

    QMenu *autoMaskMenu = new QMenu(autoMaskButton);
    QActionGroup *autoMaskGroup = new QActionGroup(autoMaskMenu);
    autoMaskGroup->setExclusive(true);
    auto addAutoMaskMode = [&](const QString &label, StereoViewWidget::AutoMaskMode mode, bool checked = false) {
        QAction *action = autoMaskMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(checked);
        action->setData(static_cast<int>(mode));
        autoMaskGroup->addAction(action);
        return action;
    };
    addAutoMaskMode(tr("QTAGENT Rect Decompose"), StereoViewWidget::AutoMaskRectDecompose, true);
    addAutoMaskMode(tr("SGBM Contour"), StereoViewWidget::AutoMaskContour);
    addAutoMaskMode(tr("Legacy Smart Mask"), StereoViewWidget::AutoMaskLegacy);

    auto runAutoMask = [this, autoMaskGroup]() {
        QAction *selected = autoMaskGroup->checkedAction();
        StereoViewWidget::AutoMaskMode mode = selected
            ? static_cast<StereoViewWidget::AutoMaskMode>(selected->data().toInt())
            : StereoViewWidget::AutoMaskRectDecompose;
        if (m_viewWidget->createAutoMaskPoints(mode)) {
            QString label = selected ? selected->text() : tr("AutoMask");
            statusBar()->showMessage(tr("%1 points created").arg(label), 3000);
        } else {
            statusBar()->showMessage(tr("Auto mask failed or is available only when no mask points exist"), 3000);
        }
    };
    connect(autoMaskButton, &QToolButton::clicked, this, runAutoMask);
    connect(autoMaskMenu, &QMenu::triggered, this, runAutoMask);
    autoMaskButton->setMenu(autoMaskMenu);
    QAction *autoMaskWidgetAction = maskBar->addWidget(autoMaskButton);

    connect(m_viewWidget, &StereoViewWidget::maskEmptyChanged, autoMaskButton, [this, autoMaskButton](bool isEmpty){
        autoMaskButton->setEnabled(m_viewWidget->isImageLoaded() && isEmpty);
    });

    addToolBarBreak();
    QToolBar *brushBar = addToolBar(tr("Reveal outside frame"));
    brushBar->addWidget(new QLabel(tr("Editing Tool: ")));
    QComboBox *brushMode = new QComboBox;
    brushMode->setObjectName("outOfFrameToolCombo");
    brushMode->setPlaceholderText(tr("Choose a tool"));
    brushMode->addItem(tr("Select Brush"),1);
    brushMode->addItem(tr("Erase Brush"),2);
    brushMode->addItem(tr("Polygonal lasso"),7);
    brushMode->addItem(tr("Smart polygon lasso"),8);
    brushMode->addItem(tr("Fuzzy Select region"),9);
    brushMode->addItem(tr("Select by color threshold"),10);
    brushBar->addWidget(brushMode);
    brushBar->addWidget(new QLabel(tr(" Size: ")));
    QSpinBox *brushSize = new QSpinBox; brushSize->setRange(1, 1000); brushSize->setValue(40);
    brushBar->addWidget(brushSize);
    brushBar->addWidget(new QLabel(tr(" Color tolerance: ")));
    QSpinBox *colorTolerance=new QSpinBox;colorTolerance->setObjectName("colorSelectionTolerance");
    colorTolerance->setRange(0,255);colorTolerance->setValue(m_viewWidget->colorSelectionTolerance());
    colorTolerance->setToolTip(tr("RGB distance from the clicked color: 0 selects exact matches; 255 accepts every color"));
    colorTolerance->setEnabled(false);brushBar->addWidget(colorTolerance);
    connect(colorTolerance,QOverload<int>::of(&QSpinBox::valueChanged),m_viewWidget,&StereoViewWidget::setColorSelectionTolerance);
    brushBar->addWidget(new QLabel(tr(" Export background: ")));
    QComboBox *brushBackground = new QComboBox;
    brushBackground->addItems({tr("Transparent (PNG)"), tr("Black"), tr("White")});
    brushBar->addWidget(brushBackground);
    brushBackground->addItem(tr("Custom cutout color..."));
    brushBackground->addItem(tr("Match preview"));
    brushBackground->setObjectName("exportBackgroundCombo");
    brushBackground->setCurrentIndex(m_viewWidget->cutoutBackground());
    brushBackground->setToolTip(tr("Match preview exports mask color and opacity. Transparent PNG and solid backgrounds export a cutout instead."));
    addToolBarBreak();
    QToolBar *selectionBar=addToolBar(tr("Selection tools"));
    selectionBar->addWidget(new QLabel(tr("Selection: ")));
    QComboBox *selectionOperation=new QComboBox;
    selectionOperation->addItems({tr("Add to selection"),tr("Subtract from selection")});
    selectionBar->addWidget(selectionOperation);
    QAction *pairedLasso=selectionBar->addAction(customIcon("pairedLasso"),tr("Paired lasso"));
    pairedLasso->setObjectName("pairedLassoAction"); pairedLasso->setCheckable(true);
    pairedLasso->setToolTip(tr("Create an aligned, independent polygon in the opposite eye when applying a new lasso. Check the estimated alignment afterward."));
    if (auto button=qobject_cast<QToolButton *>(selectionBar->widgetForAction(pairedLasso)))
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(pairedLasso,&QAction::toggled,m_viewWidget,&StereoViewWidget::setPairedLassoEnabled);
    connect(selectionOperation,QOverload<int>::of(&QComboBox::activated),this,[this](int mode){m_viewWidget->setLassoSubtract(mode==1);});
    connect(m_viewWidget,&StereoViewWidget::lassoOperationChanged,selectionOperation,[selectionOperation](bool subtract){selectionOperation->setCurrentIndex(subtract ? 1 : 0);});
    connect(m_viewWidget,&StereoViewWidget::brushSettingsLoaded,this,[selectionOperation](int,int){selectionOperation->setCurrentIndex(0);});
    QAction *closeOutline=selectionBar->addAction(tr("Close outline"));
    closeOutline->setToolTip(tr("Close the current lasso outline (Enter or C while editing the image)"));
    connect(closeOutline,&QAction::triggered,m_viewWidget,&StereoViewWidget::closeLasso);
    QAction *applyOutline=selectionBar->addAction(tr("Apply selection"));
    applyOutline->setObjectName("applyLassoAction");
    applyOutline->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Return),QKeySequence(Qt::CTRL | Qt::Key_Enter)});
    applyOutline->setToolTip(tr("Apply the current lasso selection (Ctrl+Enter). Closes an open outline with at least three points."));
    connect(applyOutline,&QAction::triggered,m_viewWidget,&StereoViewWidget::finishLasso);
    QAction *cancelOutline=selectionBar->addAction(tr("Cancel unfinished"));
    connect(cancelOutline,&QAction::triggered,m_viewWidget,&StereoViewWidget::cancelUnfinishedLasso);
    addToolBarBreak();
    QToolBar *navigationBar=addToolBar(tr("View navigation"));
    QAction *clearPaint=navigationBar->addAction(tr("Clear painted reveal"));
    connect(clearPaint,&QAction::triggered,m_viewWidget,&StereoViewWidget::clearPaintedReveal);
    QAction *fit=navigationBar->addAction(tr("Fit view"));
    fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    fit->setToolTip(tr("Fit both full images in the window (Ctrl+0). Keeps your unfinished outline."));
    connect(fit,&QAction::triggered,m_viewWidget,&StereoViewWidget::fitView);
    QAction *zoomIn=navigationBar->addAction(tr("Zoom +"));
    zoomIn->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    zoomIn->setToolTip(tr("Zoom in (Ctrl+Plus or Ctrl+Equals)."));
    connect(zoomIn,&QAction::triggered,this,[this] { m_viewWidget->setZoom(m_viewWidget->zoom()*1.2f); m_viewWidget->setFocus(); });
    QAction *zoomOut=navigationBar->addAction(tr("Zoom −"));
    zoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    zoomOut->setToolTip(tr("Zoom out (Ctrl+Minus)."));
    connect(zoomOut,&QAction::triggered,this,[this] { m_viewWidget->setZoom(m_viewWidget->zoom()/1.2f); m_viewWidget->setFocus(); });
    connect(m_viewWidget, &StereoViewWidget::smartStatus, this, [this](const QString &message) { statusBar()->showMessage(message); });
    connect(m_viewWidget, &StereoViewWidget::smartBusy, this, [brushMode](bool busy) {
        brushMode->setEnabled(!busy);
    });
    connect(brushSize, QOverload<int>::of(&QSpinBox::valueChanged), m_viewWidget, &StereoViewWidget::setBrushSize);
    for (const auto key : {Qt::Key_BracketLeft, Qt::Key_BracketRight}) {
        auto shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated, this, [this, brushSize, key] {
            if (!m_viewWidget->brushMode() || m_viewWidget->brushMode()==3 || m_viewWidget->brushMode()>=7) return;
            const int delta = qMax(1, qRound(brushSize->value() * 0.1));
            brushSize->setValue(brushSize->value() + (key == Qt::Key_BracketLeft ? -delta : delta));
        });
    }
    connect(brushBackground, QOverload<int>::of(&QComboBox::activated), this, [this, brushBackground](int mode) {
        if (mode == 3) {
            const QColor color = QColorDialog::getColor(m_viewWidget->cutoutColor(), this, tr("Choose solid cutout background color"));
            if (color.isValid()) m_viewWidget->setCutoutColor(color);
            else brushBackground->setCurrentIndex(m_viewWidget->cutoutBackground());
        } else m_viewWidget->setCutoutBackground(mode);
        m_viewWidget->setFocus();
    });
    connect(m_viewWidget, &StereoViewWidget::brushSettingsLoaded, this, [brushMode, brushBackground, colorTolerance](int mode, int background) {
        brushMode->setCurrentIndex(brushMode->findData(mode)); brushBackground->setCurrentIndex(background);colorTolerance->setEnabled(mode==9||mode==10);
    });
    connect(brushMode, QOverload<int>::of(&QComboBox::activated), this, [this, anaglyph, pan, brushMode](int index) {
        const int mode=brushMode->itemData(index).toInt();
        if (mode) { anaglyph->setChecked(false); pan->setChecked(false);if(auto clean=findChild<QAction *>("cleanPreviewAction"))clean->setChecked(false);m_viewWidget->setCutoutBackground(m_viewWidget->cutoutBackground()); }
        m_viewWidget->setBrushMode(mode);
        const QStringList tips={tr("Edit frame points"), tr("Select Brush: paint areas to preserve outside the frame. [ smaller, ] larger."), tr("Erase Brush: remove preserved areas outside the frame. The frame stays open."),
            tr("Drag a loose box around one leg in one eye; leave background space around it."), tr("Paint a small Keep stroke safely inside the leg, then Find edges."),
            tr("Paint Remove on unwanted background, then Find edges again."), tr("Erase incorrect Keep/Remove hints, then Find edges again."), tr("Polygonal lasso: click around the edge, then click the first point to close. Apply selection keeps it. Delete steps back; drag points to adjust."), tr("Magnetic lasso: click short sections along the edge; drag points to adjust; Delete steps back; Enter closes, Apply selection keeps it."),
            tr("Fuzzy Select: click a connected region in either eye. Color tolerance controls how far neighboring pixels may differ from the clicked color."),
            tr("Select by color threshold: click a color to select every matching pixel in that eye, including disconnected regions. Color tolerance controls the range.")};
        statusBar()->showMessage(tips.value(mode));
    });
    connect(brushMode,QOverload<int>::of(&QComboBox::activated),this,[colorTolerance,brushMode](int index){const int mode=brushMode->itemData(index).toInt();colorTolerance->setEnabled(mode==9||mode==10);});

    QAction *freehand = addToolAction(maskBar, customIcon("freehand"), tr("Freehand"), tr("Draw a freehand mask when no mask points exist"));
    freehand->setCheckable(true);
    freehand->setEnabled(m_viewWidget->isImageLoaded() && m_viewWidget->canFreehandDraw());
    connect(freehand, &QAction::toggled, m_viewWidget, &StereoViewWidget::setFreehandMode);
    connect(m_viewWidget, &StereoViewWidget::freehandModeChanged, freehand, [freehand](bool enabled){
        QSignalBlocker blocker(freehand);
        freehand->setChecked(enabled);
    });
    connect(m_viewWidget, &StereoViewWidget::maskEmptyChanged, freehand, [this, freehand](bool isEmpty){
        bool enabled = m_viewWidget->isImageLoaded() && isEmpty;
        freehand->setEnabled(enabled);
        if (!enabled) {
            QSignalBlocker blocker(freehand);
            freehand->setChecked(false);
        }
    });

    QAction *color = addToolAction(maskBar, customIcon("color"), tr("Mask color"), tr("Change the same mask color used in Settings and Match preview export"));
    color->setObjectName("maskColorAction");
    connect(color, &QAction::triggered, this, [this](){
        QColor c = QColorDialog::getColor(m_viewWidget->maskColor(), this, tr("Select Mask Color"));
        if (c.isValid()) m_viewWidget->setMaskColor(c);
    });
    QAction *pickColor=addToolAction(maskBar,customIcon("eyedropper"),tr("Pick mask color"),tr("Sample the mask color from either source image. Uses the same setting as Mask color and Settings. Escape cancels."));
    pickColor->setObjectName("pickMaskColorAction");
    connect(pickColor,&QAction::triggered,this,[this,anaglyph,pan] {
        anaglyph->setChecked(false);
        pan->setChecked(false);
        if (auto clean=findChild<QAction *>("cleanPreviewAction")) clean->setChecked(false);
        m_viewWidget->beginColorPick();
        statusBar()->showMessage(tr("Click either source image to sample the mask color. Escape cancels."));
    });
    connect(m_viewWidget,&StereoViewWidget::maskColorPicked,this,[this](const QColor &color) {
        statusBar()->showMessage(tr("Mask color: %1").arg(color.name()));
    });

    QAction *maskPreview = addToolAction(maskBar, customIcon("autoMask"), tr("Preview Result"), tr("Preview the selected export appearance, including ordered foreground selections. Match preview uses mask color and opacity; cutout modes use their selected background."));
    maskPreview->setCheckable(true);
    maskPreview->setChecked(m_viewWidget->maskPreviewEnabled());
    connect(maskPreview, &QAction::toggled, m_viewWidget, &StereoViewWidget::setMaskPreviewEnabled);
    QAction *cleanPreview=addToolAction(maskBar,customIcon("eye"),tr("Clean Preview"),tr("Show the selected export appearance without editing outlines, points or hints (F8). Turn off to resume drawing."));
    cleanPreview->setObjectName("cleanPreviewAction"); cleanPreview->setCheckable(true);
    cleanPreview->setChecked(m_viewWidget->cleanPreviewEnabled()); cleanPreview->setShortcut(Qt::Key_F8);
    connect(cleanPreview,&QAction::toggled,m_viewWidget,&StereoViewWidget::setCleanPreviewEnabled);

    QAction *previewFeather = addToolAction(maskBar, customIcon("featherPreview"), tr("Preview Feather"), tr("Enable feathering in preview"));
    previewFeather->setCheckable(true);
    previewFeather->setChecked(m_viewWidget->previewFeatherEnabled());
    connect(previewFeather, &QAction::toggled, m_viewWidget, &StereoViewWidget::setPreviewFeatherEnabled);

    m_opacitySpin = new QSpinBox;
    m_opacitySpin->setObjectName("maskOpacitySpin");
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix("%");
    m_opacitySpin->setToolTip(tr("Match preview background opacity; solid and transparent background modes ignore this setting"));
    m_opacitySpin->setValue(m_viewWidget->maskOpacity() * 100);
    connect(m_opacitySpin, &QSpinBox::valueChanged, this, [this](int v){
        m_viewWidget->setMaskOpacity(v / 100.0f);
    });
    QAction *opacityWidgetAction = maskBar->addWidget(m_opacitySpin);
    auto syncAppearance=[this,color](const QColor &value,float opacity) {
        QPixmap swatch(20,20);swatch.fill(value);color->setIcon(QIcon(swatch));
        QSignalBlocker blocker(m_opacitySpin);m_opacitySpin->setValue(qRound(opacity*100));
    };
    connect(m_viewWidget,&StereoViewWidget::maskAppearanceChanged,this,syncAppearance);
    syncAppearance(m_viewWidget->maskColor(),m_viewWidget->maskOpacity());

    QDockWidget *selectionsDock=new QDockWidget(tr("History"),this);
    selectionsDock->setObjectName("selectionObjectsDock");
    selectionsDock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
    QWidget *selectionPanel=new QWidget(selectionsDock);
    QVBoxLayout *selectionLayout=new QVBoxLayout(selectionPanel); selectionLayout->setContentsMargins(4,4,4,4);
    QToolBar *objectTools=new QToolBar(selectionPanel);
    objectTools->setIconSize(QSize(18,18));
    QAction *editObject=objectTools->addAction(customIcon("curve"),tr("Edit points"));
    editObject->setObjectName("editHistoryPolygonAction");
    editObject->setToolTip(tr("Edit the selected polygon's control points"));
    QAction *renameObject=objectTools->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),tr("Rename"));
    renameObject->setToolTip(tr("Rename the selected object"));
    QAction *deleteObject=objectTools->addAction(customIcon("delete"),tr("Delete"));
    deleteObject->setToolTip(tr("Delete the selected object"));
    QAction *moveObjectUp=objectTools->addAction(customIcon("shiftUp"),tr("Move up"));
    moveObjectUp->setToolTip(tr("Apply this selection earlier"));
    QAction *moveObjectDown=objectTools->addAction(customIcon("shiftDown"),tr("Move down"));
    moveObjectDown->setToolTip(tr("Apply this selection later"));
    QAction *invertObject=objectTools->addAction(customIcon("swap"),tr("Switch Add/Subtract"));
    invertObject->setObjectName("invertSelectionModeAction");
    invertObject->setToolTip(tr("Switch the selected object's Add/Subtract mode"));
    QAction *movePolygon=objectTools->addAction(customIcon("pan"),tr("Move polygon"));
    movePolygon->setObjectName("movePolygonAction");movePolygon->setCheckable(true);
    movePolygon->setToolTip(tr("Drag the selected polygon as a whole in its eye. Shift-drag moves horizontally; Escape cancels a drag."));
    selectionLayout->addWidget(objectTools);
    QTreeWidget *objects=new QTreeWidget(selectionPanel);
    objects->setObjectName("selectionObjectsList");
    objects->setHeaderLabels({tr(""),tr("Selection"),tr("Eye"),tr("Mode")});
    objects->setRootIsDecorated(false); objects->setSelectionMode(QAbstractItemView::SingleSelection);
    objects->setEditTriggers(QAbstractItemView::NoEditTriggers);
    objects->header()->setSectionResizeMode(0,QHeaderView::Fixed); objects->setColumnWidth(0,30);
    objects->header()->setSectionResizeMode(1,QHeaderView::Stretch);
    objects->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    objects->header()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    selectionLayout->addWidget(objects); selectionsDock->setWidget(selectionPanel);
    addDockWidget(Qt::RightDockWidgetArea,selectionsDock);
    selectionsDock->setMinimumWidth(290);
    auto updateObjectActions=[objects,editObject,renameObject,deleteObject,moveObjectUp,moveObjectDown,invertObject,movePolygon]() {
        auto item=objects->currentItem();
        editObject->setEnabled(item && item->data(1,Qt::UserRole+1).toBool());
        renameObject->setEnabled(item!=nullptr); deleteObject->setEnabled(item!=nullptr);
        const int row=item ? objects->indexOfTopLevelItem(item) : -1;
        moveObjectUp->setEnabled(row>0);
        moveObjectDown->setEnabled(row>=0 && row<objects->topLevelItemCount()-1);
        invertObject->setEnabled(item!=nullptr);
        movePolygon->setEnabled(item && item->data(1,Qt::UserRole+1).toBool() && item->data(0,Qt::UserRole).toBool());
        if (!movePolygon->isEnabled() && movePolygon->isChecked()) movePolygon->setChecked(false);
        invertObject->setToolTip(item ? (item->data(3,Qt::UserRole).toBool() ? tr("Change this selection to Add") : tr("Change this selection to Subtract"))
                                      : tr("Switch the selected object's Add/Subtract mode"));
    };
    auto refreshObjects=[this,objects,updateObjectActions]() {
        const QString selected=objects->currentItem() ? objects->currentItem()->data(1,Qt::UserRole).toString() : QString();
        QSignalBlocker blocker(objects); objects->clear();
        for (const auto &object:m_viewWidget->selectionObjects()) {
            auto item=new QTreeWidgetItem(objects,{QString(),object.name,object.right ? tr("Right") : tr("Left"),object.subtract ? tr("Subtract") : tr("Add")});
            item->setData(1,Qt::UserRole,object.id); item->setData(1,Qt::UserRole+1,object.anchors.size()>=3);
            item->setData(0,Qt::UserRole,object.visible);
            item->setData(3,Qt::UserRole,object.subtract);
            item->setIcon(0,customIcon(object.visible ? "eye" : "eyeHidden"));
            item->setToolTip(0,object.visible ? tr("Hide selection") : tr("Show selection"));
            if (!object.visible) item->setForeground(1,palette().brush(QPalette::Disabled,QPalette::Text));
            item->setFlags(item->flags()|Qt::ItemIsEditable);
            if (object.id==selected) objects->setCurrentItem(item);
        }
        updateObjectActions();
    };
    connect(m_viewWidget,&StereoViewWidget::selectionObjectsChanged,this,refreshObjects);
    connect(objects,&QTreeWidget::currentItemChanged,this,[this,objects,updateObjectActions]() {
        updateObjectActions();
        m_viewWidget->selectSelection(objects->currentItem() ? objects->currentItem()->data(1,Qt::UserRole).toString() : QString());
    });
    connect(objects,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int column) {
        if (column==0) m_viewWidget->setSelectionVisible(item->data(1,Qt::UserRole).toString(),!item->data(0,Qt::UserRole).toBool());
    });
    connect(objects,&QTreeWidget::itemDoubleClicked,this,[objects](QTreeWidgetItem *item,int column) {
        if (column==1) objects->editItem(item,column);
    });
    connect(objects,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int column) {
        if (column==1) m_viewWidget->renameSelection(item->data(1,Qt::UserRole).toString(),item->text(1));
    });
    connect(editObject,&QAction::triggered,this,[this,objects]() {
        if (auto item=objects->currentItem()) m_viewWidget->editSelection(item->data(1,Qt::UserRole).toString());
    });
    connect(renameObject,&QAction::triggered,this,[this,objects]() {
        auto item=objects->currentItem(); if (!item) return;
        const QString id=item->data(1,Qt::UserRole).toString(); bool ok=false;
        const QString name=QInputDialog::getText(this,tr("Rename selection"),tr("Name:"),QLineEdit::Normal,item->text(1),&ok);
        if (ok) m_viewWidget->renameSelection(id,name);
    });
    connect(deleteObject,&QAction::triggered,this,[this,objects]() {
        if (auto item=objects->currentItem()) m_viewWidget->deleteSelection(item->data(1,Qt::UserRole).toString());
    });
    connect(moveObjectUp,&QAction::triggered,this,[this,objects]() {
        if (auto item=objects->currentItem()) m_viewWidget->moveSelection(item->data(1,Qt::UserRole).toString(),-1);
    });
    connect(moveObjectDown,&QAction::triggered,this,[this,objects]() {
        if (auto item=objects->currentItem()) m_viewWidget->moveSelection(item->data(1,Qt::UserRole).toString(),1);
    });
    connect(invertObject,&QAction::triggered,this,[this,objects]() {
        if (auto item=objects->currentItem()) m_viewWidget->toggleSelectionOperation(item->data(1,Qt::UserRole).toString());
    });
    connect(movePolygon,&QAction::toggled,this,[this,anaglyph,pan,freehand](bool enabled) {
        if (enabled) {
            anaglyph->setChecked(false);pan->setChecked(false);freehand->setChecked(false);
            if (auto clean=findChild<QAction *>("cleanPreviewAction")) clean->setChecked(false);
        }
        m_viewWidget->setPolygonMoveEnabled(enabled);
    });
    connect(m_viewWidget,&StereoViewWidget::polygonMoveModeChanged,this,[movePolygon](bool enabled) {
        QSignalBlocker blocker(movePolygon);movePolygon->setChecked(enabled);
    });
    QMenu *viewMenu=menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(maskPreview); viewMenu->addAction(cleanPreview); viewMenu->addAction(selectionsDock->toggleViewAction());
    // Regroup existing actions without duplicating their handlers or tool state.
    const auto brushActions=brushBar->actions();
    const auto selectionActions=selectionBar->actions();
    QList<QAction *> transformActions;for(auto action:transformBar->actions()) if(!qobject_cast<QWidgetAction *>(action) && !action->isSeparator())transformActions.append(action);
    for(auto bar:{fileViewBar,editMaskBar,transformBar,maskBar,brushBar,selectionBar,navigationBar})bar->clear();
    auto populate=[](QToolBar *bar,const QList<QAction *> &actions){for(auto action:actions)bar->addAction(action);};
    fileViewBar->setWindowTitle(tr("File"));
    populate(fileViewBar,{newAction,openAction,m_recentFilesMenu->menuAction(),findChild<QAction *>("closeProjectAction"),saveAction,exportAction});
    findChild<QAction *>("closeProjectAction")->setText(tr("Close"));
    navigationBar->setWindowTitle(tr("View"));
    populate(navigationBar,{fit,zoomIn,zoomOut,pan,swap,anaglyph,maskPreview,cleanPreview,previewFeather});
    editMaskBar->setWindowTitle(tr("Edit Frame"));
    auto editFrame=editMaskBar->addAction(tr("Edit points"));editFrame->setObjectName("editFramePointsAction");
    auto enterFrame=[this,brushMode,pan,anaglyph,freehand] {
        pan->setChecked(false);anaglyph->setChecked(false);freehand->setChecked(false);
        if(auto clean=findChild<QAction *>("cleanPreviewAction"))clean->setChecked(false);
        m_viewWidget->setBrushMode(0);brushMode->setCurrentIndex(-1);m_viewWidget->setFocus();
        statusBar()->showMessage(tr("Right-click an edge to add a point; left-drag dots to move them. Select dots and Delete to remove them."),8000);
    };
    connect(editFrame,&QAction::triggered,this,enterFrame);
    auto addFramePoint=editMaskBar->addAction(tr("Add Point"));addFramePoint->setObjectName("addFramePointAction");
    addFramePoint->setToolTip(tr("Click to enter frame editing, then right-click an existing frame edge away from a dot. Left-drag the new dot into place. Existing dots remain."));
    connect(addFramePoint,&QAction::triggered,this,enterFrame);
    deleteSelected->setText(tr("Delete Point"));deleteSelected->setToolTip(tr("Select frame dots with a left-click (Ctrl-click for multiple dots), then click Delete Point or press Delete. Undo restores them. While editing a lasso, Delete removes its latest segment."));
    populate(editMaskBar,{undo,redo,deleteSelected,clear,alignLeft,alignRight,alignTop,alignBottom,alignDepth,pictureFrame,lockH,lockV,snap});
    transformBar->setWindowTitle(tr("Edit Frame transforms"));
    addToolGroupLabel(transformBar,tr("Scale"));populate(transformBar,{scaleUp,scaleDown});transformBar->addSeparator();
    addToolGroupLabel(transformBar,tr("Move"));populate(transformBar,{shiftLeft,shiftRight,shiftUp,shiftDown});transformBar->addSeparator();
    addToolGroupLabel(transformBar,tr("Curve / Rotate"));transformBar->addAction(curve);for(auto rotation:m_rotationActions)transformBar->addAction(rotation);
    auto depthLabel=new QLabel(tr("Frame point depth: "));editMaskBar->addWidget(depthLabel);
    auto frameDepth=new QSpinBox;frameDepth->setObjectName("framePointDepth");frameDepth->setRange(-10000,10000);frameDepth->setSuffix(tr(" px"));editMaskBar->addWidget(frameDepth);
    connect(frameDepth,QOverload<int>::of(&QSpinBox::valueChanged),this,&MainWindow::updatePointDisparity);
    connect(m_viewWidget,&StereoViewWidget::selectionChanged,frameDepth,[frameDepth](int value){QSignalBlocker blocker(frameDepth);frameDepth->setValue(value);});
    maskBar->setWindowTitle(tr("Out-of-Frame appearance"));populate(maskBar,{color,pickColor,opacityWidgetAction,settings,clearPaint});
    brushBar->setWindowTitle(tr("Out-of-Frame tools"));populate(brushBar,brushActions);brushMode->setCurrentIndex(-1);
    selectionBar->setWindowTitle(tr("Out-of-Frame selection"));populate(selectionBar,selectionActions);
    addToolBarBreak();auto stereoBar=addToolBar(tr("Stereo Window"));configureToolbar(stereoBar);stereoBar->addAction(windowDepth);
    addToolBarBreak();auto createBar=addToolBar(tr("Create Frame"));configureToolbar(createBar);
    auto createPoints=createBar->addAction(tr("Create with points"));createPoints->setObjectName("createFramePointsAction");connect(createPoints,&QAction::triggered,this,enterFrame);
    frameShapes->setText(tr("Shapes…"));createBar->addAction(frameShapes);auto shapes=frameShapes;
    createBar->addAction(autoMaskWidgetAction);createBar->addAction(freehand);
    addToolBarBreak();auto helpBar=addToolBar(tr("Help"));configureToolbar(helpBar);help->setText(tr("User Guide"));populate(helpBar,{help,findChild<QAction *>("keyboardShortcutsAction")});
    const QList<QList<QToolBar *>> sections={{fileViewBar},{navigationBar},{stereoBar},{createBar},{editMaskBar,transformBar},{brushBar,selectionBar,maskBar},{helpBar}};
    const QStringList names={tr("File"),tr("View"),tr("Stereo Window"),tr("Create Frame"),tr("Edit Frame"),tr("Out-of-Frame"),tr("Help")};
    for(const auto &rows:sections)for(auto row:rows){removeToolBarBreak(row);removeToolBar(row);}
    bool firstRow=true;for(const auto &rows:sections)for(auto row:rows){if(!firstRow)addToolBarBreak();addToolBar(row);firstRow=false;}
    auto compact=new QToolBar(tr("Control sections"),this);compact->setObjectName("compactControls");configureToolbar(compact);
    insertToolBar(fileViewBar,compact);insertToolBarBreak(fileViewBar);
    auto menus=new QMenu(this);menus->setObjectName("mainControlsMenu");
    auto menuButton=new QToolButton(compact);menuButton->setText(tr("Main Menu"));menuButton->setMenu(menus);menuButton->setPopupMode(QToolButton::InstantPopup);compact->addWidget(menuButton);
    auto preferences=new QSettings(this);
    QList<QAction *> toggles;
    for(int i=0;i<sections.size();++i) {
        const auto rows=sections[i];const QString key=QString("organizedLayout/%1").arg(i);
        for(int j=0;j<rows.size();++j)rows[j]->setObjectName(j==0 ? QString("controlSection%1").arg(i) : QString("controlSection%1Row%2").arg(i).arg(j));
        auto toggle=new QAction(names[i],this);toggle->setObjectName(QString("sectionToggle%1").arg(i));toggle->setCheckable(true);toggles.append(toggle);
        const bool expanded=i!=2 && preferences->value(key,false).toBool();toggle->setChecked(expanded);for(auto row:rows)row->setVisible(expanded);
        connect(toggle,&QAction::toggled,this,[rows,preferences,key](bool shown){for(auto row:rows)row->setVisible(shown);preferences->setValue(key,shown);});
        if(i<6)compact->addAction(i==2 ? windowDepth : toggle);
        for(auto row:rows)for(auto action:row->actions())if(!qobject_cast<QWidgetAction *>(action))addAction(action);
    }
    compact->addSeparator();compact->addAction(selectionsDock->toggleViewAction());
    selectionsDock->toggleViewAction()->setToolTip(tr("Show or hide History: individual selection and brush edits, not a whole-project undo timeline"));
    auto layoutMenu=new QMenu(tr("Layout"),compact);
    auto expandAll=[toggles](bool shown){for(int i=0;i<toggles.size();++i)if(i!=2)toggles[i]->setChecked(shown);};
    layoutMenu->addAction(tr("Show all sections"),this,[expandAll]{expandAll(true);});layoutMenu->addAction(tr("Hide all sections"),this,[expandAll]{expandAll(false);});
    auto layoutButton=new QToolButton(compact);layoutButton->setText(tr("Layout"));layoutButton->setMenu(layoutMenu);layoutButton->setPopupMode(QToolButton::InstantPopup);compact->addWidget(layoutButton);compact->addAction(toggles[6]);
    stereoText->setText(tr("Text & Bubbles"));compact->addAction(stereoText);auto textTool=stereoText;
    for(auto menuAction:menuBar()->actions())if(menuAction->menu() && menuAction->text()==tr("&Edit")){menuAction->menu()->addSeparator();menuAction->menu()->addAction(shapes);menuAction->menu()->addAction(textTool);}
    auto guided=compact->addAction(tr("Guided Process"));guided->setObjectName("guidedProcessAction");connect(guided,&QAction::triggered,this,&MainWindow::startGuidedProcess);
    // Widget controls stay in their sections: each is directly reachable from Main Menu.
    for(int i=0;i<sections.size();++i) {
        if(i==2){menus->addAction(windowDepth);continue;}
        auto group=menus->addMenu(names[i]);group->setObjectName(QString("mainSectionMenu%1").arg(i));
        auto show=group->addAction(tr("Show %1 controls").arg(names[i]));connect(show,&QAction::triggered,this,[toggle=toggles[i]]{toggle->setChecked(true);});group->addSeparator();
        for(auto row:sections[i])for(auto action:row->actions()) {
            if(auto widgetAction=qobject_cast<QWidgetAction *>(action)) {
                auto widget=widgetAction->defaultWidget();
                if(auto combo=qobject_cast<QComboBox *>(widget)) {
                    QString label=combo==brushMode ? tr("Selection Tool") : combo==brushBackground ? tr("Out-of-Frame Background") : tr("Add / Subtract");
                    auto choices=group->addMenu(label);
                    for(int n=0;n<combo->count();++n){auto choice=choices->addAction(combo->itemText(n));connect(choice,&QAction::triggered,this,[combo,n]{combo->setCurrentIndex(n);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,n));});}
                } else if(auto spin=qobject_cast<QSpinBox *>(widget)) {
                    QString label=spin==frameDepth ? tr("Frame Point Depth") : spin==brushSize ? tr("Brush Size") : spin==colorTolerance ? tr("Color Tolerance") : tr("Background Opacity");
                    auto setValue=group->addAction(label+tr("…"));connect(setValue,&QAction::triggered,this,[this,spin,label]{bool ok=false;int value=QInputDialog::getInt(this,label,tr("Value"),spin->value(),spin->minimum(),spin->maximum(),spin->singleStep(),&ok);if(ok)spin->setValue(value);});
                } else if(widget==autoMaskButton) {
                    group->addMenu(autoMaskMenu);
                }
            } else group->addAction(action);
        }
    }
    menus->addAction(selectionsDock->toggleViewAction());menus->addMenu(layoutMenu);menus->addAction(guided);menus->addAction(textTool);menus->addSeparator();menus->addAction(m_checkUpdatesAction);menus->addAction(about);
    const QString historyKey="compactLayout/history";
    selectionsDock->setVisible(preferences->value(historyKey,false).toBool());
    connect(selectionsDock,&QDockWidget::visibilityChanged,this,[this,preferences,historyKey](bool shown){if(isVisible() && !isMinimized()) preferences->setValue(historyKey,shown);});
    refreshObjects();

}

#include <QCheckBox>

class SettingsDialog : public QDialog {
public:
    SettingsDialog(AppSettings &s, QWidget *parent = nullptr) : QDialog(parent), m_settings(s) {
        setWindowTitle(tr("Background & Export Settings"));
        setObjectName("maskSettingsDialog");
        QFormLayout *layout = new QFormLayout(this);

        m_maskColorBtn = new QPushButton(tr("Select Color..."));
        m_maskColorBtn->setObjectName("settingsMaskColorButton");
        updateColorBtn(m_maskColorBtn, m_settings.maskColor);
        connect(m_maskColorBtn, &QPushButton::clicked, [this](){
            QColor c = QColorDialog::getColor(m_settings.maskColor, this);
            if (c.isValid()) { m_settings.maskColor = c; updateColorBtn(m_maskColorBtn, c); }
        });
        layout->addRow(tr("Mask Color:"), m_maskColorBtn);

        m_opacitySpin = new QSpinBox;
        m_opacitySpin->setObjectName("settingsMaskOpacitySpin");
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
        layout->addRow(tr("Padding/gap color:"), m_bgColorBtn);
        m_bgColorBtn->setToolTip(tr("Fills exported padding and the space between the eyes; this is not the mask color."));

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

    AppSettings edited=m_settings;
    SettingsDialog dlg(edited, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings=edited;
        m_settings.save();
        m_viewWidget->setMaskSettings(m_settings.maskColor, m_settings.opacity, m_settings.featherAmount);
        m_viewWidget->setPaddingSettings(m_settings.padx, m_settings.pady, m_settings.bgColor, m_settings.interleavingSpace);
        m_viewWidget->setAutoSave(m_settings.autoSave);
        statusBar()->showMessage(tr("Settings updated"), 3000);
    }
}

void MainWindow::showHelp()
{
    QFile source(":/docs/user-guide.html");
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/help/r10";
    if (!QDir().mkpath(folder) || !source.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("User Guide"), tr("Could not prepare the user guide."));
        return;
    }
    const QString path = folder + "/user-guide.html";
    const QByteArray content = source.readAll();
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit()) {
        QMessageBox::warning(this, tr("User Guide"), tr("Could not save the user guide for your browser."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::information(this, tr("User Guide"), tr("Open this file in your web browser:\n%1").arg(path));
    }
}

void MainWindow::checkForUpdates()
{
    m_checkUpdatesAction->setEnabled(false);
    statusBar()->showMessage(tr("Checking GitHub for updates…"));
    m_updateChecker->check();
}

void MainWindow::showAbout()
{
    QMessageBox aboutBox(QMessageBox::Information, tr("About StereoMask"),
        tr("<h2>StereoMask v2.1.1</h2>"
           "<p>A precision masking tool for side-by-side stereo images.</p>"
           "<p><b>Original Stereomasken creator:</b> Bernd Paksa.</p>"
           "<p>StereoMask is inspired by his Stereomasken program and its creative "
           "3D framing and masking ideas. We gratefully acknowledge his contribution "
           "to the stereo photography community.</p>"
           "<p><a href='https://www.stereomasken.de/index_en.htm'>Original Stereomasken website</a></p>"
           "<p><b>Modern StereoMask Windows implementation:</b> Subbiah Rathinagiri (S. Rathinagiri)</p>"
           "<p><b>Mac port &amp; editing improvements:</b> Theodore Whitten</p>"
           "<p>A collaborative effort. StereoMask's MIT license is preserved.</p>"
           "<p>Original app: <b>GEMINI CLI</b> and Qt6.<br>Mac improvements: <b>Codex</b> assistance.</p>"
           "<p>Built for the stereo photography community.</p>"), QMessageBox::Ok, this);
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    for (QLabel *label : aboutBox.findChildren<QLabel *>()) label->setOpenExternalLinks(true);
    aboutBox.exec();
}

void MainWindow::closeProject()
{
    if (!m_viewWidget->isImageLoaded()) return;
    if (m_viewWidget->hasLasso()) {
        QMessageBox::information(this, tr("Unfinished outline"),
                                 tr("Apply or cancel the unfinished outline before closing the project."));
        return;
    }
    if (!m_settings.autoSave) {
        const auto choice = QMessageBox::question(this, tr("Close Project"),
            tr("Save your project before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::Save && !m_viewWidget->saveProject()) {
            QMessageBox::critical(this, tr("Save failed"), tr("The project could not be saved. It remains open."));
            return;
        }
    } else if (!m_viewWidget->saveProject()) {
        QMessageBox::critical(this, tr("Save failed"), tr("The project could not be saved. It remains open."));
        return;
    }
    if (!m_viewWidget->closeProject()) {
        QMessageBox::information(this, tr("Please wait"),
                                 tr("Wait for the active operation to finish before closing."));
        return;
    }
    statusBar()->showMessage(tr("Project closed"), 5000);
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
    if (m_viewWidget->lassoClosed()) m_viewWidget->finishLasso();
    else if (m_viewWidget->hasLasso()) { statusBar()->showMessage(tr("Close or apply the unfinished outline before saving.")); return; }
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
    if (m_viewWidget->lassoClosed()) m_viewWidget->finishLasso();
    if (m_viewWidget->hasLasso()) {
        statusBar()->showMessage(tr("Close or apply the unfinished outline before exporting."));
        return;
    }
    if (!m_viewWidget->isImageLoaded()) return;
    if (m_exportWatcher->isRunning()) {
        statusBar()->showMessage(tr("Export already in progress"), 3000);
        return;
    }

    QFileInfo info(m_viewWidget->imagePath());
    QString defaultName = info.absolutePath() + "/" + info.completeBaseName() + "_masked.png";
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Masked Image"), defaultName, tr("Images (*.png *.jpg)"));
    if (!fileName.isEmpty()) {
        QImage left = m_viewWidget->exportEye(false);
        QImage right = m_viewWidget->exportEye(true);
        QVector<MaskPoint> points = m_viewWidget->points();
        QColor maskColor = m_viewWidget->maskColor();
        float opacity = m_viewWidget->maskOpacity();
        int padx = m_viewWidget->padx();
        int pady = m_viewWidget->pady();
        QColor bgColor = m_viewWidget->bgColor();
        int interleavingSpace = m_viewWidget->interleavingSpace();
        int featherAmount = m_viewWidget->featherAmount();
        bool cutout = m_viewWidget->cutoutEnabled();
        int cutoutBackground = m_viewWidget->cutoutBackground();
        QColor cutoutColor = m_viewWidget->cutoutColor();
        const int previewFeather=(!cutout || cutoutBackground==4) && m_viewWidget->previewFeatherEnabled() ? featherAmount : 0;
        QImage revealLeft = m_viewWidget->openingMask(false,previewFeather), revealRight = m_viewWidget->openingMask(true,previewFeather);
        if (cutout && cutoutBackground == 0 && !fileName.endsWith(".png", Qt::CaseInsensitive)) {
            QMessageBox::information(this, tr("Transparent export"), tr("Choose a PNG filename to preserve transparency."));
            return;
        }
        statusBar()->showMessage(tr("Please wait... Exporting (0%)"));
        m_exportWatcher->setProperty("fileName", fileName);
        m_exportWatcher->setFuture(QtConcurrent::run([=]() {
            const QColor padding=(!cutout || cutoutBackground==4) ? bgColor : cutoutBackground==0 ? QColor(Qt::transparent) : cutoutBackground==1 ? QColor(Qt::black) : cutoutBackground==2 ? QColor(Qt::white) : cutoutColor;
            return StereoViewWidget::saveComposedPair(fileName,left,right,padx,pady,interleavingSpace,padding);
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

void MainWindow::adjustStereoWindow()
{
    if (!m_viewWidget->isImageLoaded()) return;
    if (m_viewWidget->hasLasso() || m_viewWidget->polygonMoveEnabled()) {
        QMessageBox::information(this, tr("Stereo Window"),
                                 tr("Apply or cancel the unfinished lasso and turn Move polygon off before adjusting the stereo window."));
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName("stereoWindowDialog");
    dialog.setWindowTitle(tr("Stereo Window — starting image depth"));
    auto layout = new QVBoxLayout(&dialog);

    auto explanation = new QLabel(
        tr("Move the slider while viewing the same side-by-side eye layout as the main window.\n"
           "Optionally enable Anaglyph preview to align red/cyan edges. This shifts the starting eye images; it does not adjust the custom frame."), &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto preview = new StereoWindowView(&dialog);
    preview->setObjectName("stereoWindowPreview");
    layout->addWidget(preview, 1);

    auto zoomRow = new QHBoxLayout;
    auto anaglyphPreview = new QCheckBox(tr("Anaglyph preview"), &dialog);
    anaglyphPreview->setObjectName("stereoWindowAnaglyph");
    anaglyphPreview->setChecked(false);
    anaglyphPreview->setToolTip(tr("Switch between the main side-by-side layout and a red/cyan inspection view."));
    zoomRow->addWidget(anaglyphPreview);
    for (const auto &entry : QList<QPair<QString, double>>{
             {tr("Zoom +"), 1.25}, {tr("Zoom −"), 0.8}, {tr("Fit (Ctrl+0)"), 0.0}}) {
        auto button = new QPushButton(entry.first, &dialog);
        zoomRow->addWidget(button);
        connect(button, &QPushButton::clicked, &dialog, [preview, factor = entry.second] {
            if (factor == 0.0) preview->fitImage(); else preview->zoomImage(factor);
        });
    }
    auto panHint = new QLabel(tr("Scroll to zoom; drag the image to inspect another area."), &dialog);
    zoomRow->addWidget(panHint);
    zoomRow->addStretch();
    layout->addLayout(zoomRow);

    for (const auto &entry : QList<QPair<QString, double>>{
             {"Ctrl++", 1.25}, {"Ctrl+=", 1.25}, {"Ctrl+-", 0.8}, {"Ctrl+0", 0.0}}) {
        auto shortcut = new QShortcut(QKeySequence(entry.first), &dialog);
        connect(shortcut, &QShortcut::activated, &dialog, [preview, factor = entry.second] {
            if (factor == 0.0) preview->fitImage(); else preview->zoomImage(factor);
        });
    }

    auto row = new QHBoxLayout;
    auto back = new QPushButton(tr("Push back"), &dialog);
    back->setObjectName("stereoWindowPushBack");
    row->addWidget(back);

    auto slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setObjectName("stereoWindowSlider");
    const int limit = qMax(1, m_viewWidget->leftImage().width() / 2);
    slider->setRange(-limit, limit);
    slider->setValue(m_viewWidget->stereoWindowOffset());
    row->addWidget(slider);

    auto forward = new QPushButton(tr("Pull forward"), &dialog);
    forward->setObjectName("stereoWindowPullForward");
    row->addWidget(forward);

    auto amount = new QSpinBox(&dialog);
    amount->setObjectName("stereoWindowPixels");
    amount->setRange(-limit, limit);
    amount->setValue(slider->value());
    amount->setSuffix(tr(" px"));
    amount->setMinimumHeight(44);
    amount->setMinimumWidth(140);
    amount->setStyleSheet("QSpinBox {padding-right: 36px;} "
                          "QSpinBox::up-button {width:32px; height:22px; subcontrol-origin:border; subcontrol-position:top right;} "
                          "QSpinBox::down-button {width:32px; height:22px; subcontrol-origin:border; subcontrol-position:bottom right;}");
    row->addWidget(amount);
    back->setMinimumHeight(44);
    forward->setMinimumHeight(44);

    auto step = [slider, amount](int direction) {
        amount->interpretText();
        slider->setValue(slider->value() + direction);
    };
    connect(back, &QPushButton::clicked, &dialog, [step] { step(-1); });
    connect(forward, &QPushButton::clicked, &dialog, [step] { step(1); });

    auto reset = new QPushButton(tr("Reset to original"), &dialog);
    row->addWidget(reset);
    layout->addLayout(row);

    auto updatePreview = [&] { preview->setImage(m_viewWidget->stereoWindowPreview(slider->value(), anaglyphPreview->isChecked())); };
    connect(slider, &QSlider::valueChanged, amount, &QSpinBox::setValue);
    connect(amount, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, &dialog, [&](int) { updatePreview(); });
    connect(anaglyphPreview, &QCheckBox::toggled, &dialog, [&](bool) { updatePreview(); });
    connect(reset, &QPushButton::clicked, &dialog, [&] { slider->setValue(0); });

    auto note = new QLabel(
        tr("Use this before framing and tracing. Existing frame points and selections stay in place.\n"
           "The canvas size stays unchanged; shifting can expose transparent side strips. Your original file stays untouched.\n"
           "Apply stores the adjustment in the project; Save keeps it and Export uses the shifted pair."), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    class HorizontalKeys : public QObject {
        std::function<void(int)> m_change;
    public:
        HorizontalKeys(QObject *parent, std::function<void(int)> callback)
            : QObject(parent), m_change(std::move(callback)) {}
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) return false;
            auto key = static_cast<QKeyEvent *>(event);
            if (key->modifiers() != Qt::NoModifier ||
                (key->key() != Qt::Key_Left && key->key() != Qt::Key_Right)) return false;
            if (event->type() == QEvent::KeyPress) m_change(key->key() == Qt::Key_Left ? -1 : 1);
            event->accept();
            return true;
        }
    };
    auto horizontalKeys = new HorizontalKeys(&dialog, step);
    for (auto child : dialog.findChildren<QWidget *>()) child->installEventFilter(horizontalKeys);
    dialog.installEventFilter(horizontalKeys);

    updatePreview();
    dialog.showMaximized();
    if (dialog.exec() == QDialog::Accepted) {
        m_viewWidget->setStereoWindowOffset(slider->value());
        statusBar()->showMessage(
            tr("Stereo window adjusted. Save your project to retain it; Undo restores the previous depth."), 6000);
    }
}

void MainWindow::adjustPictureFrame()
{
    if (!m_viewWidget->isImageLoaded() || m_viewWidget->points().size()<3) {
        QMessageBox::information(this,tr("Picture Frame"),tr("Make your basic frame with at least three points first."));return;
    }
    QDialog dialog(this);dialog.setObjectName("pictureFrameDialog");dialog.setWindowTitle(tr("Picture Frame"));
    auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;
    auto settings=m_viewWidget->pictureFrameSettings();
    auto enabled=new QCheckBox(tr("Show picture frame"),&dialog);enabled->setObjectName("pictureFrameEnabled");enabled->setChecked(settings.enabled);layout->addWidget(enabled);
    auto width=new QSpinBox(&dialog);width->setObjectName("pictureFrameWidth");width->setRange(1,1000);width->setValue(settings.width);width->setSuffix(tr(" px"));form->addRow(tr("Border width:"),width);
    auto style=new QComboBox(&dialog);style->setObjectName("pictureFrameStyle");style->addItems({tr("Flat"),tr("Beveled"),tr("Decorative Trim")});style->setCurrentIndex(settings.style);form->addRow(tr("Style:"),style);
    auto color=new QPushButton(tr("Choose color…"),&dialog);color->setObjectName("pictureFrameColor");color->setStyleSheet(QString("background:%1").arg(settings.color.name()));form->addRow(tr("Frame color:"),color);
    connect(color,&QPushButton::clicked,&dialog,[&]{const auto next=QColorDialog::getColor(settings.color,&dialog,tr("Picture frame color"));if(next.isValid()){settings.color=next;color->setStyleSheet(QString("background:%1").arg(next.name()));}});
    layout->addLayout(form);
    auto shadow=new QCheckBox(tr("Show drop shadow"),&dialog);shadow->setObjectName("dropShadowEnabled");shadow->setChecked(settings.shadowEnabled);form->addRow(shadow);
    auto shadowSpin=[&](const QString &label,int min,int max,int value){auto spin=new QSpinBox;spin->setRange(min,max);spin->setValue(value);form->addRow(label,spin);return spin;};
    auto shadowOpacity=shadowSpin(tr("Shadow opacity (%):"),0,100,settings.shadowOpacity);
    auto shadowDistance=shadowSpin(tr("Shadow distance (pixels):"),0,1000,settings.shadowDistance);
    auto shadowAngle=shadowSpin(tr("Shadow direction (degrees):"),0,359,settings.shadowAngle);
    auto shadowBlur=shadowSpin(tr("Shadow softness (pixels):"),0,200,settings.shadowBlur);
    auto shadowSpread=shadowSpin(tr("Shadow spread (pixels):"),0,200,settings.shadowSpread);
    auto shadowColor=new QPushButton(tr("Shadow color…"));form->addRow(shadowColor);
    connect(shadowColor,&QPushButton::clicked,&dialog,[&]{auto next=QColorDialog::getColor(settings.shadowColor,&dialog,tr("Shadow color"));if(next.isValid())settings.shadowColor=next;});
    auto shadowNote=new QLabel(tr("Direction: 0° right, 90° down. The shadow appears outside preserved areas, supports transparent PNG, and is clipped to the existing canvas."));shadowNote->setWordWrap(true);layout->addWidget(shadowNote);
    auto note=new QLabel(tr("The border grows outside your frame opening and follows its depth in each eye. Visible Add selections cover the border, so foreground objects appear over it. Width uses source-image pixels. The border is clipped to the existing canvas."),&dialog);note->setWordWrap(true);layout->addWidget(note);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){settings.enabled=enabled->isChecked();settings.width=width->value();settings.style=style->currentIndex();settings.shadowEnabled=shadow->isChecked();settings.shadowOpacity=shadowOpacity->value();settings.shadowDistance=shadowDistance->value();settings.shadowAngle=shadowAngle->value();settings.shadowBlur=shadowBlur->value();settings.shadowSpread=shadowSpread->value();m_viewWidget->setPictureFrameSettings(settings);}
}
