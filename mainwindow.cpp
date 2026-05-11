#include "mainwindow.h"
#include "stereoviewwidget.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QSlider>
#include <QActionGroup>
#include <QToolBar>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QFile>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QMessageBox>

void AppSettings::load() {
    QString path = QCoreApplication::applicationDirPath() + "/settings.json";
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isNull()) fromJson(doc.object());
    }
}

void AppSettings::save() const {
    QString path = QCoreApplication::applicationDirPath() + "/settings.json";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(toJson()).toJson());
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

        QLabel *text = new QLabel(tr("<h1>StereoMask v1.2</h1>"
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

    setWindowTitle(tr("StereoMask - Qt6"));
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

void MainWindow::createToolbar()
{
    QToolBar *tb = addToolBar(tr("Main Toolbar"));
    tb->setMovable(false);
    tb->setIconSize(QSize(24, 24));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto addBtn = [&](const QString &text, const char* member, const QString &tip, bool checkable = false) {
        QAction *a = tb->addAction(text, this, member);
        a->setToolTip(tip);
        a->setCheckable(checkable);
        return a;
    };

    addBtn("📄", SLOT(newProject()), tr("New Project (Open Image)"));
    addBtn("📂", SLOT(openProject()), tr("Open Mask Project (.msk)"));
    addBtn("💾", SLOT(saveProject()), tr("Save Mask Project (Ctrl+S)"));
    addBtn("🖼️", SLOT(exportImage()), tr("Export Masked Image"));
    tb->addSeparator();

    QAction *undo = m_viewWidget->undoStack()->createUndoAction(this, "↩️");
    undo->setToolTip(tr("Undo"));
    tb->addAction(undo);
    
    QAction *redo = m_viewWidget->undoStack()->createRedoAction(this, "↪️");
    redo->setToolTip(tr("Redo"));
    tb->addAction(redo);
    
    tb->addAction("🗑️", [this](){ m_viewWidget->deleteSelectedPoints(); })->setToolTip(tr("Delete Selected"));
    tb->addAction("🧹", this, SLOT(clearPoints()))->setToolTip(tr("Clear All Points"));
    tb->addSeparator();

    QAction *anaglyph = addBtn("🎨", SLOT(setAnaglyphMode(bool)), tr("Toggle Anaglyph"), true);
    connect(anaglyph, &QAction::toggled, this, &MainWindow::setAnaglyphMode);
    
    QAction *swap = addBtn("↔️", SLOT(toggleSwapSides(bool)), tr("Swap Left/Right"), true);
    connect(swap, &QAction::toggled, this, &MainWindow::toggleSwapSides);
    
    QAction *pan = addBtn("🖱️", nullptr, tr("Pan Mode (P)"), true);
    pan->setShortcut(Qt::Key_P);
    connect(pan, &QAction::toggled, m_viewWidget, &StereoViewWidget::setPanMode);
    tb->addSeparator();

    QAction *lockH = addBtn("🔒H", nullptr, tr("Lock Horizontal"), true);
    connect(lockH, &QAction::toggled, m_viewWidget, &StereoViewWidget::setLockHorizontal);

    QAction *lockV = addBtn("🔒V", nullptr, tr("Lock Vertical"), true);
    connect(lockV, &QAction::toggled, m_viewWidget, &StereoViewWidget::setLockVertical);
    
    QAction *snap = addBtn("🧲", nullptr, tr("Snap to Points (S)"), true);
    snap->setShortcut(Qt::Key_S);
    snap->setChecked(m_settings.snapEnabled);
    connect(snap, &QAction::toggled, this, [this](bool checked){
        m_settings.snapEnabled = checked;
        m_settings.save();
        m_viewWidget->setSnapEnabled(checked);
    });
    tb->addSeparator();

    tb->addAction("⬅️", [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignLeft); })->setToolTip(tr("Align Left"));
    tb->addAction("➡️", [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignRight); })->setToolTip(tr("Align Right"));
    tb->addAction("⬆️", [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignTop); })->setToolTip(tr("Align Top"));
    tb->addAction("⬇️", [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignBottom); })->setToolTip(tr("Align Bottom"));
    tb->addAction("📏", [this](){ m_viewWidget->alignSelectedPoints(StereoViewWidget::AlignDepth); })->setToolTip(tr("Align Depth"));
    tb->addSeparator();

    tb->addAction("➕", [this](){ m_viewWidget->transformSelectedPoints(1.1f, 1.1f, 0, 0); })->setToolTip(tr("Scale Up"));
    tb->addAction("➖", [this](){ m_viewWidget->transformSelectedPoints(0.9f, 0.9f, 0, 0); })->setToolTip(tr("Scale Down"));
    tb->addAction("⇇", [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, -10, 0); })->setToolTip(tr("Shift Left"));
    tb->addAction("⇉", [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 10, 0); })->setToolTip(tr("Shift Right"));
    tb->addAction("⇈", [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 0, -10); })->setToolTip(tr("Shift Up"));
    tb->addAction("⇊", [this](){ m_viewWidget->transformSelectedPoints(1.0f, 1.0f, 0, 10); })->setToolTip(tr("Shift Down"));
    tb->addSeparator();

    QActionGroup *rotGroup = new QActionGroup(this);
    rotGroup->setExclusive(true);
    auto addRotBtn = [&](const QString &text, StereoViewWidget::RotationAxis axis, const QString &tip) {
        QAction *a = tb->addAction(text, [this, axis](bool checked){
            m_viewWidget->setRotationMode(checked ? axis : StereoViewWidget::AxisNone);
        });
        a->setToolTip(tip);
        a->setCheckable(true);
        rotGroup->addAction(a);
        return a;
    };
    addRotBtn("🔄X", StereoViewWidget::AxisX, tr("Rotate Mode X (Tilt Up/Down)"));
    addRotBtn("🔄Y", StereoViewWidget::AxisY, tr("Rotate Mode Y (Tilt Left/Right)"));
    addRotBtn("🔄Z", StereoViewWidget::AxisZ, tr("Rotate Mode Z (Roll)"));
    tb->addSeparator();

    tb->addAction("🖍️", [this](){
        QColor c = QColorDialog::getColor(m_viewWidget->maskColor(), this, tr("Select Mask Color"));
        if (c.isValid()) m_viewWidget->setMaskColor(c);
    })->setToolTip(tr("Quick Mask Color"));

    m_opacitySpin = new QSpinBox;
    m_opacitySpin->setRange(0, 100);
    m_opacitySpin->setSuffix("%");
    m_opacitySpin->setToolTip(tr("Quick Mask Opacity"));
    m_opacitySpin->setValue(m_viewWidget->maskOpacity() * 100);
    connect(m_opacitySpin, &QSpinBox::valueChanged, this, [this](int v){
        m_viewWidget->setMaskOpacity(v / 100.0f);
    });
    tb->addWidget(m_opacitySpin);
    tb->addSeparator();

    addBtn("⚙️", SLOT(showSettings()), tr("Mask Settings"));
    addBtn("➰", SLOT(toggleCurve()), tr("Toggle Curve (C)"));
    addBtn("❓", SLOT(showHelp()), tr("Help / Shortcuts"));
    addBtn("ℹ️", SLOT(showAbout()), tr("About StereoMask"));
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
           "📄 <b>New Project:</b> Start with a new image.<br>"
           "📂 <b>Open Project:</b> Load an existing .msk file.<br>"
           "💾 <b>Save Project:</b> Manually save current points (Ctrl+S).<br>"
           "🖼️ <b>Export:</b> Render the final masked image.<br>"
           "↩️/↪️ <b>Undo/Redo:</b> Standard edit history.<br>"
           "🎨 <b>Anaglyph:</b> Toggle 3D anaglyph preview.<br>"
           "↔️ <b>Swap Sides:</b> Swap Left/Right eye views.<br>"
           "🖱️ <b>Pan Mode:</b> Move image view (Shortcut: P).<br>"
           "🧲 <b>Snap:</b> Toggle snapping to nearby points (Shortcut: S).<br>"
           "➰ <b>Curve:</b> Toggle Bezier curve on selected point segment.<br>"
           "🔒H/V: Lock point movement horizontally or vertically.<br>"
           "⬅️➡️⬆️⬇️📏: Align selected points.<br>"
           "➕➖⇇⇉⇈⇊: Transform/Shift selected points.<br>"
           "⚙️ <b>Settings:</b> Configure colors, feathering, padding, and AutoSave.<br>"
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
        tr("<h2>StereoMask v1.2</h2>"
           "<p>A precision masking tool for side-by-side stereo images.</p>"
           "<p><b>Author:</b> S. Rathinagiri</p>"
           "<p>Developed using <b>GEMINI CLI</b> and Qt6.</p>"
           "<p>Built with ❤️ for the stereo photography community.</p>"));
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
    QFileInfo info(m_viewWidget->imagePath());
    QString defaultName = info.absolutePath() + "/" + info.completeBaseName() + "_masked.png";
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Masked Image"), defaultName, tr("Images (*.png *.jpg)"));
    if (!fileName.isEmpty()) {
        statusBar()->showMessage(tr("Please wait... Exporting (0%)"));
        qApp->processEvents();

        m_viewWidget->saveImage(fileName, m_viewWidget->maskColor(), m_viewWidget->maskOpacity(), 
            m_viewWidget->padx(), m_viewWidget->pady(), m_viewWidget->bgColor(), m_viewWidget->interleavingSpace(),
            [this](int pct) {
                statusBar()->showMessage(tr("Please wait... Exporting (%1%)").arg(pct));
                qApp->processEvents();
            }
        );

        statusBar()->showMessage(tr("Image exported to %1").arg(QFileInfo(fileName).fileName()), 5000);
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
