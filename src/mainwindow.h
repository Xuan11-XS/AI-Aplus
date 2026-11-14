#ifndef APLUS_MAINWINDOW_H
#define APLUS_MAINWINDOW_H

#include <QColor>
#include <QMainWindow>

#include "compilerunner.h"
#include "problemfetcher.h"

class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QTextBrowser;
class QToolBar;
class CodeEditor;
class ConsoleRunner;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newFile();
    void openFile();
    void save();
    void saveAs();
    void compile();
    void run();
    void compileAndRun();
    void stopRun();
    void findText();
    void findNext();
    void gotoLine();
    void toggleComment();
    void showOptions();
    void showAbout();
    void updateCursorStatus();
    void updateWindowTitle();
    void updateActions();
    void onCompileFinished(bool ok, const QString &diagnostics, int elapsedMs,
                           const QString &exePath, bool stopped);
    void onRunOutput(const QString &text, bool error);
    void onRunFinished(int exitCode, bool crashed, bool timedOut, bool stopped, int elapsedMs);
    void onConsoleFinished(int exitCode, bool timedOut, bool stopped, int elapsedMs);
    void onMessageDoubleClicked(int row, int column);
    void fetchProblem();
    void onProblemFetched(const ProblemFetcher::Problem &problem);
    void onProblemFailed(const QString &message);
    void fillSample();
    void createProblemFile();

private:
    void createActions();
    void createMenus();
    void createToolBars();
    void createCentralWidget();
    void createProblemTab();
    void createStatusBar();
    void connectRunner();
    void loadSettings();
    void saveSettings();
    bool maybeSave();
    bool prepareBuild();
    bool loadFile(const QString &path, bool showMessage = true);
    bool saveFile();
    bool saveFileAs();
    void startCompile();
    void startRun();
    void clearMessages();
    void addMessage(const QString &file, int line, int column,
                    const QString &text, const QColor &color);
    void parseDiagnostics(const QString &diagnostics, int &errorCount, int &warningCount);
    void appendColored(const QString &text, const QColor &color);
    void appendOutputLine(const QString &text, const QColor &color);
    void updateStdStatus();
    QString workspaceDir() const;
    QString suggestedPath() const;
    QString normalizeDiagnosticPath(const QString &path) const;
    bool isCurrentFile(const QString &path) const;

    CodeEditor *m_editor = nullptr;
    QTabWidget *m_editorTabs = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    QSplitter *m_splitter = nullptr;
    QTableWidget *m_messages = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPlainTextEdit *m_input = nullptr;

    QLabel *m_positionLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_encodingLabel = nullptr;
    QLabel *m_standardLabel = nullptr;
    QLabel *m_compilerLabel = nullptr;

    QToolBar *m_toolBar = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_commentAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_gotoAction = nullptr;
    QAction *m_consoleAction = nullptr;
    QAction *m_compileAction = nullptr;
    QAction *m_runAction = nullptr;
    QAction *m_compileRunAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_optionsAction = nullptr;
    QAction *m_aboutAction = nullptr;

    CompileRunner *m_runner = nullptr;
    ConsoleRunner *m_console = nullptr;
    ProblemFetcher *m_problemFetcher = nullptr;

    QWidget *m_problemTab = nullptr;
    QLineEdit *m_problemEdit = nullptr;
    QTextBrowser *m_problemView = nullptr;
    QComboBox *m_sampleBox = nullptr;
    QPushButton *m_fetchButton = nullptr;
    QPushButton *m_fillSampleButton = nullptr;
    QPushButton *m_newFileButton = nullptr;
    ProblemFetcher::Problem m_problem;
    CompileRunner::Options m_options;

    QString m_currentPath;
    QString m_exePath;
    QString m_searchText;
    QString m_lastDir;
    QString m_compilerVersion;
    bool m_compilerFound = false;
    bool m_pendingRun = false;
    bool m_consoleMode = true;
    int m_firstErrorLine = 0;
};

#endif // APLUS_MAINWINDOW_H
