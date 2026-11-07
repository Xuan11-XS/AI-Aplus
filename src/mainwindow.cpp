#include "mainwindow.h"

#include "codeeditor.h"
#include "consolerunner.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>

namespace {

const char kTemplate[] = R"(#include <bits/stdc++.h>
using namespace std;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int a, b;
    if (!(cin >> a >> b)) return 0;
    cout << a + b << '\n';
    return 0;
}
)";

const QColor kErrorColor(0xA0, 0x00, 0x00);
const QColor kWarningColor(0x80, 0x50, 0x00);
const QColor kNoteColor(0x60, 0x60, 0x60);
const QColor kSuccessColor(0x00, 0x64, 0x00);
const QColor kHeaderColor(0x00, 0x00, 0x80);

QString seconds(int ms)
{
    return QString::number(ms / 1000.0, 'f', 2);
}

QString baseName(const QString &path)
{
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Aplus"));
    resize(1100, 760);
    setMinimumSize(760, 520);

    createCentralWidget();
    createActions();
    createToolBars();
    createMenus();
    createStatusBar();

    m_runner = new CompileRunner(this);
    m_console = new ConsoleRunner(this);
    connectRunner();
    connect(m_console, &ConsoleRunner::finished, this, &MainWindow::onConsoleFinished);
    loadSettings();

    m_compilerFound = CompileRunner::compilerAvailable();
    if (m_compilerFound) {
        m_compilerVersion = CompileRunner::compilerVersion();
        m_compilerLabel->setText(m_compilerVersion.isEmpty()
                                     ? tr("g++")
                                     : tr("g++ %1").arg(m_compilerVersion));
    } else {
        m_compilerLabel->setText(tr("未找到 g++"));
    }

    const QString initial = workspaceDir() + QStringLiteral("/main.cpp");
    if (!QFileInfo::exists(initial)) {
        QFile file(initial);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(kTemplate);
    }
    if (!loadFile(initial, false))
        newFile();

    if (m_input->toPlainText().isEmpty())
        m_input->setPlainText(QStringLiteral("1 2"));

    updateWindowTitle();
    updateActions();
    updateCursorStatus();
    statusBar()->showMessage(tr("就绪"));
    m_editor->setFocus();
}

void MainWindow::createCentralWidget()
{
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setObjectName(QStringLiteral("splitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(4);

    m_editorTabs = new QTabWidget(m_splitter);
    m_editorTabs->setDocumentMode(false);
    m_editorTabs->setTabsClosable(false);
    m_editorTabs->setMovable(false);
    m_editorTabs->setElideMode(Qt::ElideRight);

    m_editor = new CodeEditor(m_editorTabs);
    m_editor->setObjectName(QStringLiteral("editor"));
    m_editorTabs->addTab(m_editor, tr("未命名"));

    m_bottomTabs = new QTabWidget(m_splitter);
    m_bottomTabs->setObjectName(QStringLiteral("bottomTabs"));
    m_bottomTabs->setDocumentMode(false);

    m_messages = new QTableWidget(0, 3, m_bottomTabs);
    m_messages->setObjectName(QStringLiteral("messages"));
    m_messages->setHorizontalHeaderLabels(QStringList{tr("行"), tr("列"), tr("信息")});
    m_messages->verticalHeader()->setVisible(false);
    m_messages->verticalHeader()->setDefaultSectionSize(20);
    m_messages->horizontalHeader()->setHighlightSections(false);
    m_messages->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_messages->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_messages->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_messages->setColumnWidth(0, 44);
    m_messages->setColumnWidth(1, 44);
    m_messages->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_messages->setSelectionMode(QAbstractItemView::SingleSelection);
    m_messages->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messages->setShowGrid(false);
    m_messages->setWordWrap(false);
    m_messages->setTextElideMode(Qt::ElideNone);
    m_messages->setAlternatingRowColors(false);
    m_messages->setCornerButtonEnabled(false);
    m_messages->setFocusPolicy(Qt::StrongFocus);
    QFont tableFont = font();
    tableFont.setPointSize(9);
    m_messages->setFont(tableFont);
    m_messages->horizontalHeader()->setFont(tableFont);

    m_output = new QPlainTextEdit(m_bottomTabs);
    m_output->setObjectName(QStringLiteral("output"));
    m_output->setReadOnly(true);
    m_output->setUndoRedoEnabled(false);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(aplusMonoFont(10));
    m_output->setMaximumBlockCount(20000);
    m_output->setPlaceholderText(tr("程序输出显示在这里。"));

    m_input = new QPlainTextEdit(m_bottomTabs);
    m_input->setObjectName(QStringLiteral("input"));
    m_input->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_input->setFont(aplusMonoFont(10));
    m_input->setPlaceholderText(
        tr("程序的标准输入写在这里。\n独立控制台模式下，这里留空就可以在控制台窗口里手动输入。"));

    m_bottomTabs->addTab(m_messages, tr("编译信息"));
    m_bottomTabs->addTab(m_output, tr("运行输出"));
    m_bottomTabs->addTab(m_input, tr("程序输入"));

    m_splitter->addWidget(m_editorTabs);
    m_splitter->addWidget(m_bottomTabs);
    m_splitter->setStretchFactor(0, 4);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes(QList<int>{700, 250});

    setCentralWidget(m_splitter);

    connect(m_messages, &QTableWidget::cellDoubleClicked,
            this, &MainWindow::onMessageDoubleClicked);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
            this, &MainWindow::updateCursorStatus);
    connect(m_editor, &CodeEditor::fontPointSizeChanged, this, [this](int pointSize) {
        QSettings settings;
        settings.setValue(QStringLiteral("editor/fontSize"), pointSize);
        statusBar()->showMessage(tr("代码字号：%1 pt").arg(pointSize), 1500);
    });
    connect(m_editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool) { updateWindowTitle(); });
}

void MainWindow::createActions()
{
    const QIcon newIcon = style()->standardIcon(QStyle::SP_FileIcon);
    const QIcon openIcon = style()->standardIcon(QStyle::SP_DirOpenIcon);
    const QIcon saveIcon = style()->standardIcon(QStyle::SP_DialogSaveButton);

    m_newAction = new QAction(newIcon, tr("新建(&N)"), this);
    m_newAction->setObjectName(QStringLiteral("newAction"));
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setStatusTip(tr("新建文件"));
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newFile);

    m_openAction = new QAction(openIcon, tr("打开(&O)..."), this);
    m_openAction->setObjectName(QStringLiteral("openAction"));
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("打开文件"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_saveAction = new QAction(saveIcon, tr("保存(&S)"), this);
    m_saveAction->setObjectName(QStringLiteral("saveAction"));
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(tr("保存文件"));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::save);

    m_saveAsAction = new QAction(tr("另存为(&A)..."), this);
    m_saveAsAction->setObjectName(QStringLiteral("saveAsAction"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setStatusTip(tr("另存为"));
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

    m_closeAction = new QAction(tr("关闭(&C)"), this);
    m_closeAction->setShortcut(QKeySequence::Close);
    connect(m_closeAction, &QAction::triggered, this, &QWidget::close);

    m_quitAction = new QAction(tr("退出(&X)"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("撤销(&U)"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, m_editor, &QPlainTextEdit::undo);

    m_redoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("重做(&R)"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, m_editor, &QPlainTextEdit::redo);

    m_cutAction = new QAction(tr("剪切(&T)"), this);
    m_cutAction->setShortcut(QKeySequence::Cut);
    m_cutAction->setEnabled(false);
    connect(m_cutAction, &QAction::triggered, m_editor, &QPlainTextEdit::cut);

    m_copyAction = new QAction(tr("复制(&C)"), this);
    m_copyAction->setShortcut(QKeySequence::Copy);
    m_copyAction->setEnabled(false);
    connect(m_copyAction, &QAction::triggered, m_editor, &QPlainTextEdit::copy);

    m_pasteAction = new QAction(tr("粘贴(&P)"), this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, m_editor, &QPlainTextEdit::paste);

    m_selectAllAction = new QAction(tr("全选(&A)"), this);
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, m_editor, &QPlainTextEdit::selectAll);

    m_commentAction = new QAction(tr("注释/取消注释(&/)"), this);
    m_commentAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));
    m_commentAction->setStatusTip(tr("注释或取消注释选中行"));
    connect(m_commentAction, &QAction::triggered, this, &MainWindow::toggleComment);

    m_findAction = new QAction(tr("查找(&F)..."), this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, &MainWindow::findText);

    m_findNextAction = new QAction(tr("查找下一个(&N)"), this);
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    connect(m_findNextAction, &QAction::triggered, this, &MainWindow::findNext);

    m_gotoAction = new QAction(tr("转到行(&G)..."), this);
    m_gotoAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    connect(m_gotoAction, &QAction::triggered, this, &MainWindow::gotoLine);

    m_consoleAction = new QAction(tr("在独立控制台中运行(&O)"), this);
    m_consoleAction->setCheckable(true);
    m_consoleAction->setStatusTip(tr("用系统终端窗口运行程序，类似 Dev-C++ 的控制台"));
    connect(m_consoleAction, &QAction::toggled, this, [this](bool checked) {
        m_consoleMode = checked;
        saveSettings();
        statusBar()->showMessage(checked ? tr("运行方式：独立控制台")
                                         : tr("运行方式：内嵌输出"),
                                 3000);
    });

    const QIcon compileIcon = QIcon::fromTheme(
        QStringLiteral("applications-development"),
        style()->standardIcon(QStyle::SP_BrowserReload));
    m_compileAction = new QAction(compileIcon, tr("编译(&C)"), this);
    m_compileAction->setObjectName(QStringLiteral("compileAction"));
    m_compileAction->setShortcut(QKeySequence(Qt::Key_F9));
    m_compileAction->setIconText(tr("编译"));
    m_compileAction->setToolTip(tr("编译 (F9)"));
    m_compileAction->setStatusTip(tr("编译当前文件"));
    connect(m_compileAction, &QAction::triggered, this, &MainWindow::compile);

    const QIcon runIcon = QIcon::fromTheme(
        QStringLiteral("media-playback-start"),
        style()->standardIcon(QStyle::SP_MediaPlay));
    m_runAction = new QAction(runIcon, tr("运行(&R)"), this);
    m_runAction->setObjectName(QStringLiteral("runAction"));
    m_runAction->setShortcut(QKeySequence(Qt::Key_F10));
    m_runAction->setIconText(tr("运行"));
    m_runAction->setToolTip(tr("运行 (F10)"));
    m_runAction->setStatusTip(tr("运行上次编译的程序"));
    connect(m_runAction, &QAction::triggered, this, &MainWindow::run);

    const QIcon runBuildIcon = QIcon::fromTheme(
        QStringLiteral("system-run"),
        style()->standardIcon(QStyle::SP_MediaPlay));
    m_compileRunAction = new QAction(runBuildIcon, tr("编译并运行(&B)"), this);
    m_compileRunAction->setObjectName(QStringLiteral("compileRunAction"));
    m_compileRunAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_compileRunAction->setIconText(tr("编译运行"));
    m_compileRunAction->setToolTip(tr("编译并运行 (F11)"));
    m_compileRunAction->setStatusTip(tr("编译并运行当前文件"));
    connect(m_compileRunAction, &QAction::triggered, this, &MainWindow::compileAndRun);

    const QIcon stopIcon = QIcon::fromTheme(
        QStringLiteral("process-stop"),
        style()->standardIcon(QStyle::SP_MediaStop));
    m_stopAction = new QAction(stopIcon, tr("停止(&S)"), this);
    m_stopAction->setObjectName(QStringLiteral("stopAction"));
    m_stopAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+F2")));
    m_stopAction->setIconText(tr("停止"));
    m_stopAction->setToolTip(tr("停止 (Ctrl+F2)"));
    m_stopAction->setEnabled(false);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopRun);

    m_optionsAction = new QAction(tr("编译选项(&O)..."), this);
    m_optionsAction->setStatusTip(tr("设置语言标准、优化级别和时间限制"));
    connect(m_optionsAction, &QAction::triggered, this, &MainWindow::showOptions);

    m_aboutAction = new QAction(tr("关于 Aplus(&A)"), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    connect(m_editor, &QPlainTextEdit::copyAvailable, m_cutAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::copyAvailable, m_copyAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::undoAvailable, m_undoAction, &QAction::setEnabled);
    connect(m_editor, &QPlainTextEdit::redoAvailable, m_redoAction, &QAction::setEnabled);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_closeAction);
    fileMenu->addAction(m_quitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(m_selectAllAction);
    editMenu->addSeparator();
    editMenu->addAction(m_commentAction);

    QMenu *searchMenu = menuBar()->addMenu(tr("搜索(&S)"));
    searchMenu->addAction(m_findAction);
    searchMenu->addAction(m_findNextAction);
    searchMenu->addSeparator();
    searchMenu->addAction(m_gotoAction);

    QMenu *runMenu = menuBar()->addMenu(tr("运行(&R)"));
    runMenu->addAction(m_consoleAction);
    runMenu->addSeparator();
    runMenu->addAction(m_compileAction);
    runMenu->addAction(m_runAction);
    runMenu->addAction(m_compileRunAction);
    runMenu->addSeparator();
    runMenu->addAction(m_stopAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    QAction *toolBarAction = m_toolBar->toggleViewAction();
    toolBarAction->setText(tr("标准工具栏"));
    viewMenu->addAction(toolBarAction);

    QMenu *toolsMenu = menuBar()->addMenu(tr("工具(&T)"));
    toolsMenu->addAction(m_optionsAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolBars()
{
    m_toolBar = addToolBar(tr("标准"));
    m_toolBar->setObjectName(QStringLiteral("standardToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_toolBar->addAction(m_newAction);
    m_toolBar->addAction(m_openAction);
    m_toolBar->addAction(m_saveAction);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_undoAction);
    m_toolBar->addAction(m_redoAction);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_compileAction);
    m_toolBar->addAction(m_runAction);
    m_toolBar->addAction(m_compileRunAction);
    m_toolBar->addAction(m_stopAction);

    for (QAction *action : {m_compileAction, m_runAction, m_compileRunAction, m_stopAction}) {
        if (QWidget *widget = m_toolBar->widgetForAction(action)) {
            if (auto *button = qobject_cast<QToolButton *>(widget))
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
    }
}

void MainWindow::createStatusBar()
{
    auto makeLabel = [this](const QString &text, int width) {
        auto *label = new QLabel(text, this);
        label->setFrameShape(QFrame::Panel);
        label->setFrameShadow(QFrame::Sunken);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumWidth(width);
        QFont labelFont = label->font();
        labelFont.setPointSize(9);
        label->setFont(labelFont);
        return label;
    };

    statusBar()->setSizeGripEnabled(true);
    statusBar()->showMessage(tr("就绪"));

    m_positionLabel = makeLabel(tr("行 1, 列 1"), 96);
    m_modeLabel = makeLabel(tr("插入"), 52);
    m_encodingLabel = makeLabel(QStringLiteral("UTF-8"), 60);
    m_standardLabel = makeLabel(QStringLiteral("C++17"), 64);
    m_compilerLabel = makeLabel(QStringLiteral("g++"), 116);

    statusBar()->addPermanentWidget(m_positionLabel);
    statusBar()->addPermanentWidget(m_modeLabel);
    statusBar()->addPermanentWidget(m_encodingLabel);
    statusBar()->addPermanentWidget(m_standardLabel);
    statusBar()->addPermanentWidget(m_compilerLabel);
}

void MainWindow::connectRunner()
{
    connect(m_runner, &CompileRunner::compileStarted, this, [this]() {
        m_bottomTabs->setCurrentWidget(m_messages);
        statusBar()->showMessage(tr("正在编译..."));
        updateActions();
    });

    connect(m_runner, &CompileRunner::compileFinished,
            this, &MainWindow::onCompileFinished);

    connect(m_runner, &CompileRunner::runStarted, this, [this]() {
        m_bottomTabs->setCurrentWidget(m_output);
        statusBar()->showMessage(tr("正在运行..."));
        updateActions();
    });

    connect(m_runner, &CompileRunner::runOutput, this, &MainWindow::onRunOutput);
    connect(m_runner, &CompileRunner::runFinished, this, &MainWindow::onRunFinished);
}

void MainWindow::loadSettings()
{
    QSettings settings;
    m_options.standard = settings.value(QStringLiteral("compiler/standard"),
                                        QStringLiteral("c++17")).toString();
    m_options.optimization = settings.value(QStringLiteral("compiler/optimization"),
                                            QStringLiteral("-O2")).toString();
    m_options.warnings = settings.value(QStringLiteral("compiler/warnings"), true).toBool();
    m_options.timeLimitMs = settings.value(QStringLiteral("compiler/timeLimit"), 2000).toInt();

    const QStringList standards{
        QStringLiteral("c++11"), QStringLiteral("c++14"), QStringLiteral("c++17"),
        QStringLiteral("c++20"), QStringLiteral("c++23")};
    if (!standards.contains(m_options.standard))
        m_options.standard = QStringLiteral("c++17");

    const QStringList optimizations{
        QStringLiteral("-O0"), QStringLiteral("-O1"),
        QStringLiteral("-O2"), QStringLiteral("-O3")};
    if (!optimizations.contains(m_options.optimization))
        m_options.optimization = QStringLiteral("-O2");

    m_options.timeLimitMs = qBound(200, m_options.timeLimitMs, 30000);

    const int fontSize = settings.value(QStringLiteral("editor/fontSize"), 10).toInt();
    m_editor->setEditorFontPointSize(fontSize);

    const bool consoleAvailable = ConsoleRunner::available();
    m_consoleAction->setEnabled(consoleAvailable);
    if (!consoleAvailable) {
#ifdef Q_OS_WIN
        m_consoleAction->setToolTip(tr("没有找到 PowerShell，运行会退回内嵌输出"));
#else
        m_consoleAction->setToolTip(tr("未找到可用的终端模拟器（kgx / gnome-terminal / konsole / xfce4-terminal）"));
#endif
        m_consoleMode = false;
    } else {
        m_consoleMode = settings.value(QStringLiteral("run/consoleMode"), true).toBool();
    }
    {
        const QSignalBlocker blocker(m_consoleAction);
        m_consoleAction->setChecked(m_consoleMode);
    }

    m_lastDir = settings.value(QStringLiteral("files/lastDir"), workspaceDir()).toString();
    if (!QDir(m_lastDir).exists())
        m_lastDir = workspaceDir();

    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray splitterState = settings.value(QStringLiteral("window/splitter")).toByteArray();
    if (!splitterState.isEmpty())
        m_splitter->restoreState(splitterState);

    updateStdStatus();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("compiler/standard"), m_options.standard);
    settings.setValue(QStringLiteral("compiler/optimization"), m_options.optimization);
    settings.setValue(QStringLiteral("compiler/warnings"), m_options.warnings);
    settings.setValue(QStringLiteral("compiler/timeLimit"), m_options.timeLimitMs);
    settings.setValue(QStringLiteral("run/consoleMode"), m_consoleMode);
    settings.setValue(QStringLiteral("editor/fontSize"), m_editor->editorFontPointSize());
    settings.setValue(QStringLiteral("files/lastDir"), m_lastDir);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    saveSettings();
    event->accept();
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;

    const QString name = m_currentPath.isEmpty() ? tr("未命名") : baseName(m_currentPath);
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Aplus"),
        tr("是否保存对 \"%1\" 的修改？").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Save)
        return saveFile();
    if (answer == QMessageBox::Discard)
        return true;
    return false;
}

bool MainWindow::prepareBuild()
{
    if (!m_compilerFound) {
        QMessageBox::critical(this, tr("缺少编译器"),
                              tr("没有找到 g++。\n\n"
                                 "请先安装 g++，或者用 GXX 环境变量指定编译器路径。"));
        return false;
    }

    if (m_currentPath.isEmpty() || m_editor->document()->isModified()) {
        if (!saveFile())
            return false;
    }

    return !m_currentPath.isEmpty();
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;

    m_currentPath.clear();
    m_exePath.clear();
    m_pendingRun = false;

    m_editor->setPlainText(QString::fromUtf8(kTemplate));
    m_editor->document()->setModified(true);
    m_editor->setErrorLine(0);
    m_input->clear();
    m_output->clear();
    clearMessages();

    updateWindowTitle();
    updateActions();
    statusBar()->showMessage(tr("新建文件"), 3000);
    m_editor->setFocus();
}

void MainWindow::openFile()
{
    if (!maybeSave())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开文件"), m_lastDir,
        tr("C++ 文件 (*.cpp *.cc *.cxx *.c);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    loadFile(path);
}

bool MainWindow::loadFile(const QString &path, bool showMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("打开失败"),
                             tr("无法打开文件：\n%1\n\n%2").arg(path, file.errorString()));
        return false;
    }

    const QString text = QString::fromUtf8(file.readAll());
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    m_editor->setErrorLine(0);

    m_currentPath = QFileInfo(path).absoluteFilePath();
    m_exePath.clear();
    const QString expectedExe = CompileRunner::exePathFor(m_currentPath);
    if (QFileInfo(expectedExe).isExecutable())
        m_exePath = expectedExe;

    m_output->clear();
    clearMessages();
    m_pendingRun = false;
    m_lastDir = QFileInfo(m_currentPath).absolutePath();

    updateWindowTitle();
    updateActions();

    if (showMessage)
        statusBar()->showMessage(tr("已打开 %1").arg(m_currentPath), 5000);

    m_editor->setFocus();
    return true;
}

void MainWindow::save()
{
    saveFile();
}

void MainWindow::saveAs()
{
    saveFileAs();
}

bool MainWindow::saveFile()
{
    if (m_currentPath.isEmpty())
        return saveFileAs();

    QSaveFile file(m_currentPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("保存失败"),
                             tr("无法写入文件：\n%1\n\n%2").arg(m_currentPath, file.errorString()));
        return false;
    }

    const QByteArray data = m_editor->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        QMessageBox::warning(this, tr("保存失败"),
                             tr("保存文件时出错：\n%1").arg(m_currentPath));
        return false;
    }

    m_editor->document()->setModified(false);
    m_lastDir = QFileInfo(m_currentPath).absolutePath();
    updateWindowTitle();
    updateActions();
    statusBar()->showMessage(tr("已保存 %1").arg(m_currentPath), 4000);
    return true;
}

bool MainWindow::saveFileAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("另存为"), suggestedPath(),
        tr("C++ 文件 (*.cpp *.cc *.cxx *.c);;所有文件 (*)"));
    if (path.isEmpty())
        return false;

    QString target = path;
    if (QFileInfo(target).suffix().isEmpty())
        target += QStringLiteral(".cpp");

    const QString oldPath = m_currentPath;
    m_currentPath = QFileInfo(target).absoluteFilePath();
    m_exePath.clear();

    if (!saveFile()) {
        m_currentPath = oldPath;
        updateWindowTitle();
        return false;
    }

    return true;
}

void MainWindow::compile()
{
    if (m_runner->busy())
        return;
    if (!prepareBuild())
        return;

    m_pendingRun = false;
    startCompile();
}

void MainWindow::compileAndRun()
{
    if (m_runner->busy())
        return;
    if (!prepareBuild())
        return;

    m_pendingRun = true;
    startCompile();
}

void MainWindow::run()
{
    if (m_runner->busy())
        return;

    if (m_exePath.isEmpty() || !QFileInfo(m_exePath).isExecutable()) {
        QMessageBox::information(this, tr("运行"),
                                 tr("还没有可运行的程序。\n请先编译（F9），或直接按 F11 编译并运行。"));
        return;
    }

    startRun();
}

void MainWindow::startCompile()
{
    clearMessages();
    m_firstErrorLine = 0;
    m_editor->setErrorLine(0);
    m_exePath.clear();
    m_bottomTabs->setCurrentWidget(m_messages);
    updateActions();

    statusBar()->showMessage(tr("正在编译 %1 ...").arg(baseName(m_currentPath)));
    m_runner->compile(m_currentPath, m_options);
    updateActions();
}

void MainWindow::startRun()
{
    if (m_exePath.isEmpty() || !QFileInfo(m_exePath).isExecutable()) {
        QMessageBox::information(this, tr("运行"), tr("请先编译当前文件。"));
        return;
    }

    if (m_consoleMode && m_console && m_console->available()) {
        const QString input = m_input->toPlainText();
        if (m_console->start(m_exePath, QFileInfo(m_currentPath).absolutePath(),
                             input, 0, workspaceDir())) {
            m_output->clear();
            m_bottomTabs->setCurrentWidget(m_output);
            appendOutputLine(tr("==== 在独立控制台中运行 %1 ====").arg(baseName(m_exePath)),
                             kHeaderColor);
            if (!input.isEmpty())
                appendOutputLine(tr("（标准输入取自“程序输入”标签页）"), kNoteColor);
            statusBar()->showMessage(tr("正在独立控制台中运行..."));
            updateActions();
            return;
        }

        statusBar()->showMessage(tr("无法打开终端窗口，改用内嵌输出"), 5000);
    }

    m_output->clear();
    m_bottomTabs->setCurrentWidget(m_output);
    appendOutputLine(tr("==== 运行 %1 ====").arg(baseName(m_exePath)), kHeaderColor);
    statusBar()->showMessage(tr("正在运行..."));
    m_runner->run(m_exePath, QFileInfo(m_currentPath).absolutePath(),
                  m_input->toPlainText(), m_options.timeLimitMs);
    updateActions();
}

void MainWindow::stopRun()
{
    const bool compilerBusy = m_runner->busy();
    const bool consoleBusy = m_console && m_console->busy();

    if (!compilerBusy && !consoleBusy)
        return;

    statusBar()->showMessage(tr("正在停止..."));
    if (compilerBusy)
        m_runner->stop();
    if (consoleBusy)
        m_console->stop();
}

void MainWindow::onConsoleFinished(int exitCode, bool timedOut, bool stopped, int elapsedMs)
{
    updateActions();

    if (stopped) {
        appendOutputLine(tr("==== 已停止 ===="), kHeaderColor);
        statusBar()->showMessage(tr("已停止"));
        return;
    }

    if (timedOut) {
        appendOutputLine(tr("==== 超出时间限制（%1 ms），程序已被终止 ====")
                             .arg(m_options.timeLimitMs),
                         kErrorColor);
        statusBar()->showMessage(tr("超出时间限制"));
        return;
    }

    if (exitCode < 0) {
        appendOutputLine(tr("==== 控制台已关闭或程序未启动 ===="), kErrorColor);
        statusBar()->showMessage(tr("运行结束"));
        return;
    }

    appendOutputLine(tr("==== 程序结束，退出码 %1，用时 %2 ms ====")
                         .arg(exitCode)
                         .arg(elapsedMs),
                     kHeaderColor);

    if (exitCode == 0)
        statusBar()->showMessage(tr("运行完成（独立控制台）"));
    else
        statusBar()->showMessage(tr("运行结束（退出码 %1）").arg(exitCode));
}

void MainWindow::onCompileFinished(bool ok, const QString &diagnostics,
                                   int elapsedMs, const QString &exePath, bool stopped)
{
    updateActions();

    if (stopped) {
        statusBar()->showMessage(tr("编译已停止"));
        m_pendingRun = false;
        return;
    }

    clearMessages();
    m_firstErrorLine = 0;
    int errorCount = 0;
    int warningCount = 0;
    parseDiagnostics(diagnostics, errorCount, warningCount);

    if (ok) {
        m_exePath = exePath;
        if (m_messages->rowCount() == 0) {
            addMessage(QString(), 0, 0, tr("编译成功"), kSuccessColor);
        }
        if (warningCount > 0) {
            statusBar()->showMessage(tr("编译成功（%1 条警告，用时 %2 秒）")
                                         .arg(warningCount)
                                         .arg(seconds(elapsedMs)));
        } else {
            statusBar()->showMessage(tr("编译成功（用时 %1 秒）").arg(seconds(elapsedMs)));
        }
        updateActions();

        if (m_pendingRun) {
            m_pendingRun = false;
            startRun();
        }
        return;
    }

    m_exePath.clear();
    m_pendingRun = false;

    if (m_messages->rowCount() == 0) {
        addMessage(QString(), 0, 0,
                   diagnostics.isEmpty() ? tr("编译失败") : diagnostics,
                   kErrorColor);
    }

    if (m_messages->rowCount() > 0)
        m_messages->selectRow(0);
    m_bottomTabs->setCurrentWidget(m_messages);
    m_editor->setErrorLine(m_firstErrorLine);

    statusBar()->showMessage(tr("编译失败（%1 个错误，用时 %2 秒）")
                                 .arg(qMax(1, errorCount))
                                 .arg(seconds(elapsedMs)));
    updateActions();
}

void MainWindow::parseDiagnostics(const QString &diagnostics,
                                  int &errorCount, int &warningCount)
{
    errorCount = 0;
    warningCount = 0;
    m_firstErrorLine = 0;

    static const QRegularExpression pattern(
        QStringLiteral("^(.+?):(\\d+):(\\d+):\\s*(fatal error|error|warning|note):\\s*(.*)$"));

    const QStringList lines = diagnostics.split(QLatin1Char('\n'));
    int matched = 0;

    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        if (line.trimmed().isEmpty())
            continue;

        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch())
            continue;

        const QString file = normalizeDiagnosticPath(match.captured(1));
        const int lineNumber = match.captured(2).toInt();
        const int column = match.captured(3).toInt();
        const QString severity = match.captured(4);
        const QString message = match.captured(5);

        QColor color = kNoteColor;
        if (severity == QLatin1String("error") || severity == QLatin1String("fatal error")) {
            color = kErrorColor;
            ++errorCount;
            if (m_firstErrorLine == 0 && isCurrentFile(file))
                m_firstErrorLine = lineNumber;
        } else if (severity == QLatin1String("warning")) {
            color = kWarningColor;
            ++warningCount;
        }

        addMessage(file, lineNumber, column, message, color);
        ++matched;
    }

    if (matched == 0) {
        int added = 0;
        for (QString line : lines) {
            line.remove(QLatin1Char('\r'));
            if (line.trimmed().isEmpty())
                continue;
            addMessage(QString(), 0, 0, line.trimmed(), kNoteColor);
            if (++added >= 200)
                break;
        }
    } else {
        for (QString line : lines) {
            line.remove(QLatin1Char('\r'));
            if (line.trimmed().isEmpty() || pattern.match(line).hasMatch())
                continue;
            if (line.contains(QLatin1String("undefined reference"))
                || line.contains(QLatin1String("ld returned"))
                || line.startsWith(QLatin1String("collect2"))) {
                addMessage(QString(), 0, 0, line.trimmed(), kErrorColor);
            }
        }
    }

    if (m_messages->rowCount() > 0)
        m_messages->scrollToTop();
}

void MainWindow::addMessage(const QString &file, int line, int column,
                            const QString &text, const QColor &color)
{
    const int row = m_messages->rowCount();
    m_messages->insertRow(row);

    auto *lineItem = new QTableWidgetItem(line > 0 ? QString::number(line) : QString());
    lineItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lineItem->setForeground(color);
    lineItem->setData(Qt::UserRole, file);

    auto *columnItem = new QTableWidgetItem(column > 0 ? QString::number(column) : QString());
    columnItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    columnItem->setForeground(color);

    auto *textItem = new QTableWidgetItem(text);
    textItem->setForeground(color);
    if (!file.isEmpty())
        textItem->setToolTip(QStringLiteral("%1:%2:%3\n%4").arg(file).arg(line).arg(column).arg(text));
    else
        textItem->setToolTip(text);

    m_messages->setItem(row, 0, lineItem);
    m_messages->setItem(row, 1, columnItem);
    m_messages->setItem(row, 2, textItem);
}

void MainWindow::onRunOutput(const QString &text, bool error)
{
    appendColored(text, error ? kErrorColor : QColor(0x00, 0x00, 0x00));
}

void MainWindow::onRunFinished(int exitCode, bool crashed, bool timedOut,
                               bool stopped, int elapsedMs)
{
    updateActions();

    if (stopped) {
        appendOutputLine(tr("==== 已停止 ===="), kHeaderColor);
        statusBar()->showMessage(tr("已停止"));
        return;
    }

    if (timedOut) {
        appendOutputLine(tr("==== 超出时间限制（%1 ms），程序已被终止 ====")
                             .arg(m_options.timeLimitMs),
                         kErrorColor);
        statusBar()->showMessage(tr("超出时间限制"));
        return;
    }

    if (crashed) {
        if (exitCode < 0) {
            appendOutputLine(tr("==== 无法启动程序 ===="), kErrorColor);
            statusBar()->showMessage(tr("启动失败"));
        } else {
            appendOutputLine(tr("==== 程序异常退出（退出码 %1，用时 %2 ms） ====")
                                 .arg(exitCode)
                                 .arg(elapsedMs),
                             kErrorColor);
            statusBar()->showMessage(tr("运行错误"));
        }
        return;
    }

    appendOutputLine(tr("==== 程序结束，退出码 %1，用时 %2 ms ====")
                         .arg(exitCode)
                         .arg(elapsedMs),
                     kHeaderColor);

    if (exitCode == 0)
        statusBar()->showMessage(tr("运行完成"));
    else
        statusBar()->showMessage(tr("运行结束（退出码 %1）").arg(exitCode));
}

void MainWindow::appendColored(const QString &text, const QColor &color)
{
    if (text.isEmpty())
        return;

    QScrollBar *bar = m_output->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 4;

    QTextCharFormat format;
    format.setForeground(color);

    QTextCursor cursor(m_output->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text, format);

    if (atBottom)
        bar->setValue(bar->maximum());
}

void MainWindow::appendOutputLine(const QString &text, const QColor &color)
{
    appendColored(text + QLatin1Char('\n'), color);
}

void MainWindow::onMessageDoubleClicked(int row, int column)
{
    Q_UNUSED(column)

    const QTableWidgetItem *lineItem = m_messages->item(row, 0);
    if (!lineItem)
        return;

    const int line = lineItem->text().toInt();
    if (line <= 0)
        return;

    const QString file = lineItem->data(Qt::UserRole).toString();
    if (!isCurrentFile(file)) {
        statusBar()->showMessage(tr("该信息属于其他文件：%1").arg(file), 6000);
        return;
    }

    const QTableWidgetItem *columnItem = m_messages->item(row, 1);
    const int columnNumber = columnItem ? columnItem->text().toInt() : 1;
    m_editor->gotoLine(line, qMax(1, columnNumber));
}

void MainWindow::updateCursorStatus()
{
    const QTextCursor cursor = m_editor->textCursor();
    m_positionLabel->setText(tr("行 %1, 列 %2")
                                 .arg(cursor.blockNumber() + 1)
                                 .arg(cursor.positionInBlock() + 1));
}

void MainWindow::updateWindowTitle()
{
    const QString name = m_currentPath.isEmpty() ? tr("未命名") : baseName(m_currentPath);
    const bool modified = m_editor->document()->isModified();
    const QString prefix = modified ? QStringLiteral("*") : QString();

    setWindowTitle(QStringLiteral("%1%2 - Aplus").arg(prefix, name));
    if (m_editorTabs->count() > 0)
        m_editorTabs->setTabText(0, prefix + name);
}

void MainWindow::updateActions()
{
    if (!m_runner)
        return;

    const bool busy = m_runner->busy() || (m_console && m_console->busy());
    const bool hasExe = !m_exePath.isEmpty() && QFileInfo(m_exePath).isExecutable();

    m_compileAction->setEnabled(!busy);
    m_compileRunAction->setEnabled(!busy);
    m_runAction->setEnabled(!busy && hasExe);
    m_stopAction->setEnabled(busy);
}

void MainWindow::findText()
{
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, tr("查找"), tr("查找内容："), QLineEdit::Normal, m_searchText, &ok);
    if (!ok || text.isEmpty())
        return;

    m_searchText = text;

    if (!m_editor->find(text)) {
        QTextCursor cursor(m_editor->document());
        cursor.movePosition(QTextCursor::Start);
        m_editor->setTextCursor(cursor);
        if (!m_editor->find(text)) {
            statusBar()->showMessage(tr("未找到 \"%1\"").arg(text), 4000);
            return;
        }
    }

    statusBar()->showMessage(tr("已找到 \"%1\"").arg(text), 2000);
}

void MainWindow::findNext()
{
    if (m_searchText.isEmpty()) {
        findText();
        return;
    }

    if (!m_editor->find(m_searchText)) {
        QTextCursor cursor(m_editor->document());
        cursor.movePosition(QTextCursor::Start);
        m_editor->setTextCursor(cursor);
        if (!m_editor->find(m_searchText)) {
            statusBar()->showMessage(tr("未找到 \"%1\"").arg(m_searchText), 4000);
        }
    }
}

void MainWindow::gotoLine()
{
    bool ok = false;
    const int line = QInputDialog::getInt(
        this, tr("转到行"), tr("行号："),
        m_editor->textCursor().blockNumber() + 1,
        1, qMax(1, m_editor->blockCount()), 1, &ok);
    if (!ok)
        return;

    m_editor->gotoLine(line);
}

void MainWindow::toggleComment()
{
    QTextCursor cursor = m_editor->textCursor();
    QTextDocument *document = m_editor->document();

    const bool hasSelection = cursor.hasSelection();
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();

    QTextBlock first = document->findBlock(start);
    QTextBlock last = hasSelection ? document->findBlock(end) : first;
    if (hasSelection && last != first && end == last.position())
        last = last.previous();

    bool allCommented = true;
    for (QTextBlock block = first;; block = block.next()) {
        const QString text = block.text().trimmed();
        if (!text.isEmpty() && !text.startsWith(QLatin1String("//"))) {
            allCommented = false;
            break;
        }
        if (block == last)
            break;
    }

    QTextCursor edit(document);
    edit.beginEditBlock();
    int delta = 0;

    for (QTextBlock block = first;; block = block.next()) {
        const QString text = block.text();
        int indent = 0;
        while (indent < text.size() && text.at(indent).isSpace())
            ++indent;

        if (indent < text.size()) {
            QTextCursor blockCursor(block);
            blockCursor.setPosition(block.position() + indent);
            if (allCommented && text.mid(indent).startsWith(QLatin1String("//"))) {
                blockCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                blockCursor.removeSelectedText();
                delta -= 2;
            } else if (!allCommented) {
                blockCursor.insertText(QStringLiteral("//"));
                delta += 2;
            }
        }

        if (block == last)
            break;
    }

    edit.endEditBlock();

    if (hasSelection) {
        QTextCursor selection(document);
        selection.setPosition(start);
        selection.setPosition(qMax(start, end + delta), QTextCursor::KeepAnchor);
        m_editor->setTextCursor(selection);
    }
}

void MainWindow::showOptions()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("编译选项"));

    auto *layout = new QFormLayout(&dialog);
    layout->setLabelAlignment(Qt::AlignRight);

    auto *standardBox = new QComboBox(&dialog);
    standardBox->addItem(QStringLiteral("C++11"), QStringLiteral("c++11"));
    standardBox->addItem(QStringLiteral("C++14"), QStringLiteral("c++14"));
    standardBox->addItem(QStringLiteral("C++17"), QStringLiteral("c++17"));
    standardBox->addItem(QStringLiteral("C++20"), QStringLiteral("c++20"));
    standardBox->addItem(QStringLiteral("C++23"), QStringLiteral("c++23"));
    standardBox->setCurrentIndex(qMax(0, standardBox->findData(m_options.standard)));

    auto *optimizationBox = new QComboBox(&dialog);
    optimizationBox->addItem(QStringLiteral("-O0（不优化）"), QStringLiteral("-O0"));
    optimizationBox->addItem(QStringLiteral("-O1"), QStringLiteral("-O1"));
    optimizationBox->addItem(QStringLiteral("-O2（推荐）"), QStringLiteral("-O2"));
    optimizationBox->addItem(QStringLiteral("-O3"), QStringLiteral("-O3"));
    optimizationBox->setCurrentIndex(qMax(0, optimizationBox->findData(m_options.optimization)));

    auto *timeLimitBox = new QSpinBox(&dialog);
    timeLimitBox->setRange(200, 30000);
    timeLimitBox->setSingleStep(100);
    timeLimitBox->setSuffix(QStringLiteral(" ms"));
    timeLimitBox->setValue(m_options.timeLimitMs);

    auto *warningsBox = new QCheckBox(tr("显示编译警告（-Wall）"), &dialog);
    warningsBox->setChecked(m_options.warnings);

    layout->addRow(tr("语言标准："), standardBox);
    layout->addRow(tr("优化级别："), optimizationBox);
    layout->addRow(tr("时间限制："), timeLimitBox);
    layout->addRow(QString(), warningsBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    dialog.setMinimumWidth(360);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_options.standard = standardBox->currentData().toString();
    m_options.optimization = optimizationBox->currentData().toString();
    m_options.timeLimitMs = timeLimitBox->value();
    m_options.warnings = warningsBox->isChecked();

    saveSettings();
    updateStdStatus();
    statusBar()->showMessage(tr("编译选项已更新，下次编译时生效"), 4000);
}

void MainWindow::showAbout()
{
    const QString compiler = m_compilerFound
                                 ? tr("g++ %1").arg(m_compilerVersion)
                                 : tr("未找到");
    QMessageBox::about(
        this, tr("关于 Aplus"),
        tr("<h3>Aplus %1</h3>"
           "<p>面向算法竞赛的简单 C++ 编辑器。</p>"
           "<p>按 F9 编译，F10 运行，F11 编译并运行。</p>"
           "<p>界面基于 Qt %2，编译器：%3</p>")
            .arg(QCoreApplication::applicationVersion(), QStringLiteral(QT_VERSION_STR), compiler));
}

void MainWindow::updateStdStatus()
{
    QString standard = m_options.standard;
    if (standard.startsWith(QLatin1String("c++")))
        standard = QStringLiteral("C++") + standard.mid(3);
    m_standardLabel->setText(standard);
}

QString MainWindow::workspaceDir() const
{
    const QString current = QDir::current().absoluteFilePath(QStringLiteral("workspace"));
    const QString besideApp = QDir(QCoreApplication::applicationDirPath())
                                  .absoluteFilePath(QStringLiteral("workspace"));
    const QString chosen = QDir(current).exists() ? current : besideApp;
    QDir().mkpath(chosen);
    return QDir(chosen).absolutePath();
}

QString MainWindow::suggestedPath() const
{
    if (!m_currentPath.isEmpty())
        return m_currentPath;
    return workspaceDir() + QStringLiteral("/solution.cpp");
}

QString MainWindow::normalizeDiagnosticPath(const QString &path) const
{
    const QFileInfo info(path);
    if (info.isAbsolute())
        return QDir::cleanPath(info.absoluteFilePath());

    const QString base = m_currentPath.isEmpty()
                             ? QDir::currentPath()
                             : QFileInfo(m_currentPath).absolutePath();
    return QDir::cleanPath(QDir(base).absoluteFilePath(path));
}

bool MainWindow::isCurrentFile(const QString &path) const
{
    if (m_currentPath.isEmpty() || path.isEmpty())
        return false;
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath())
           == QDir::cleanPath(QFileInfo(m_currentPath).absoluteFilePath());
}

void MainWindow::clearMessages()
{
    m_messages->setRowCount(0);
}
