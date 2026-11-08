#ifndef APLUS_COMPILERUNNER_H
#define APLUS_COMPILERUNNER_H

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class CompileRunner : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString standard = QStringLiteral("c++17");
        QString optimization = QStringLiteral("-O2");
        bool warnings = true;
        int timeLimitMs = 2000;
    };

    explicit CompileRunner(QObject *parent = nullptr);
    ~CompileRunner() override;

    bool busy() const { return m_operation != Operation::None; }

    void compile(const QString &sourcePath, const Options &options);
    void run(const QString &exePath, const QString &workingDir, const QString &input, int timeLimitMs);
    void stop();

    static QString compilerProgram();
    static bool compilerAvailable();
    static QString compilerVersion();
    static QString exePathFor(const QString &sourcePath);

signals:
    void compileStarted();
    void compileFinished(bool ok, const QString &diagnostics, int elapsedMs,
                         const QString &exePath, bool stopped);
    void runStarted();
    void runOutput(const QString &text, bool error);
    void runFinished(int exitCode, bool crashed, bool timedOut, bool stopped, int elapsedMs);

private:
    enum class Operation { None, Compiling, Running };

    void finishCompile(bool ok, bool stopped);
    void finishRun(bool stopped);

    Operation m_operation = Operation::None;
    QProcess *m_process = nullptr;
    QElapsedTimer m_timer;
    QTimer *m_limitTimer = nullptr;
    QString m_compileOutput;
    QString m_exePath;
    bool m_timedOut = false;
    bool m_stopped = false;
    bool m_failedToStart = false;
};

#endif // APLUS_COMPILERUNNER_H
