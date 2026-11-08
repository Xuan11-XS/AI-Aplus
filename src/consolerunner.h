#ifndef APLUS_CONSOLERUNNER_H
#define APLUS_CONSOLERUNNER_H

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <memory>

class QTemporaryDir;

class ConsoleRunner : public QObject
{
    Q_OBJECT

public:
    explicit ConsoleRunner(QObject *parent = nullptr);
    ~ConsoleRunner() override;

    static bool available();
    bool busy() const { return m_running; }

    bool start(const QString &exePath, const QString &workingDir,
               const QString &input, int timeLimitMs, const QString &workspaceDir);
    void stop();

signals:
    void started();
    void finished(int exitCode, bool timedOut, bool stopped, int elapsedMs);

private:
    void pollStatus();
    void killProgramGroup();
    void resetTerminal();

    QProcess *m_terminal = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_watchdog = nullptr;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QString m_statusFile;
    QString m_pidFile;
    QElapsedTimer m_timer;
    bool m_running = false;
};

#endif // APLUS_CONSOLERUNNER_H
