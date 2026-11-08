#include "compilerunner.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

CompileRunner::CompileRunner(QObject *parent)
    : QObject(parent)
    , m_limitTimer(new QTimer(this))
{
    m_limitTimer->setSingleShot(true);
    connect(m_limitTimer, &QTimer::timeout, this, [this]() {
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
}

CompileRunner::~CompileRunner()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString CompileRunner::compilerProgram()
{
    const QString program = qEnvironmentVariable("GXX");
    return program.isEmpty() ? QStringLiteral("g++") : program;
}

bool CompileRunner::compilerAvailable()
{
    const QString program = compilerProgram();
    const QFileInfo info(program);
    if (info.isAbsolute())
        return info.isExecutable();
    return !QStandardPaths::findExecutable(program).isEmpty();
}

QString CompileRunner::compilerVersion()
{
    QProcess process;
    process.start(compilerProgram(), QStringList{QStringLiteral("--version")});
    if (!process.waitForFinished(3000))
        return QString();

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QRegularExpression version(QStringLiteral("(\\d+\\.\\d+\\.\\d+)"));
    const QRegularExpressionMatch match = version.match(output);
    if (match.hasMatch())
        return match.captured(1);

    return output.section(QLatin1Char('\n'), 0, 0).trimmed();
}

QString CompileRunner::exePathFor(const QString &sourcePath)
{
    const QFileInfo info(sourcePath);
    QString base = info.completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("a.out");
    if (base == info.fileName())
        base += QStringLiteral(".out");

    QString suffix;
#ifdef Q_OS_WIN
    suffix = QStringLiteral(".exe");
#endif
    return info.absolutePath() + QLatin1Char('/') + base + suffix;
}

void CompileRunner::compile(const QString &sourcePath, const Options &options)
{
    if (busy())
        return;

    const QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) {
        emit compileFinished(false, tr("源文件不存在：%1").arg(sourcePath), 0, QString(), false);
        return;
    }

    m_exePath = exePathFor(sourcePath);
    m_compileOutput.clear();
    m_timedOut = false;
    m_stopped = false;
    m_failedToStart = false;
    m_operation = Operation::Compiling;
    m_timer.start();

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(info.absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_process->setProcessEnvironment(environment);

    QStringList arguments;
    arguments << QStringLiteral("-std=") + options.standard
              << options.optimization
              << QStringLiteral("-fdiagnostics-color=never")
              << QStringLiteral("-pipe");
    if (options.warnings)
        arguments << QStringLiteral("-Wall");
    arguments << sourcePath << QStringLiteral("-o") << m_exePath;

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process)
            return;
        m_compileOutput += QString::fromUtf8(m_process->readAllStandardOutput());
        if (m_compileOutput.size() > 512 * 1024)
            m_compileOutput = m_compileOutput.left(512 * 1024) + tr("\n[编译输出过长，已截断]");
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_failedToStart = true;
        m_compileOutput += tr("无法启动编译器：%1").arg(compilerProgram());
        finishCompile(false, false);
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                const bool ok = status == QProcess::NormalExit && exitCode == 0;
                finishCompile(ok, m_stopped);
            });

    emit compileStarted();
    m_limitTimer->start(20000);
    m_process->start(compilerProgram(), arguments);
}

void CompileRunner::finishCompile(bool ok, bool stopped)
{
    if (m_operation != Operation::Compiling)
        return;

    m_operation = Operation::None;
    m_limitTimer->stop();

    if (m_timedOut)
        m_compileOutput += tr("\n编译超时（超过 20 秒），已终止。");
    if (stopped)
        m_compileOutput += tr("\n编译已停止。");

    QProcess *process = m_process;
    m_process = nullptr;
    if (process)
        process->deleteLater();

    emit compileFinished(ok && !m_timedOut && !stopped,
                         m_compileOutput.trimmed(),
                         int(m_timer.elapsed()),
                         m_exePath,
                         stopped);
}

void CompileRunner::run(const QString &exePath, const QString &workingDir,
                        const QString &input, int timeLimitMs)
{
    if (busy())
        return;

    const QFileInfo exeInfo(exePath);
    if (!exeInfo.exists() || !exeInfo.isExecutable()) {
        emit runOutput(tr("找不到可执行文件：%1\n").arg(exePath), true);
        emit runFinished(-1, true, false, false, 0);
        return;
    }

    m_timedOut = false;
    m_stopped = false;
    m_failedToStart = false;
    m_operation = Operation::Running;
    m_timer.start();

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(workingDir);
    m_process->setProgram(exePath);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process)
            return;
        const QString text = QString::fromUtf8(m_process->readAllStandardOutput());
        if (!text.isEmpty())
            emit runOutput(text, false);
    });

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process)
            return;
        const QString text = QString::fromUtf8(m_process->readAllStandardError());
        if (!text.isEmpty())
            emit runOutput(text, true);
    });

    connect(m_process, &QProcess::started, this, [this, input]() {
        if (!m_process)
            return;
        m_process->write(input.toUtf8());
        m_process->closeWriteChannel();
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_failedToStart = true;
        emit runOutput(tr("无法启动程序：%1\n").arg(m_process ? m_process->program() : QString()), true);
        finishRun(false);
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { finishRun(m_stopped); });

    emit runStarted();
    m_limitTimer->start(qMax(100, timeLimitMs));
    m_process->start();
}

void CompileRunner::finishRun(bool stopped)
{
    if (m_operation != Operation::Running)
        return;

    m_operation = Operation::None;
    m_limitTimer->stop();

    QProcess *process = m_process;
    m_process = nullptr;

    int exitCode = -1;
    bool crashed = true;

    if (process) {
        const QString out = QString::fromUtf8(process->readAllStandardOutput());
        if (!out.isEmpty())
            emit runOutput(out, false);
        const QString err = QString::fromUtf8(process->readAllStandardError());
        if (!err.isEmpty())
            emit runOutput(err, true);

        exitCode = process->exitCode();
        crashed = process->exitStatus() != QProcess::NormalExit;
        process->deleteLater();
    }

    if (m_failedToStart) {
        exitCode = -1;
        crashed = true;
    }

    const bool timedOut = m_timedOut;
    const bool wasStopped = stopped || m_stopped;
    m_failedToStart = false;

    emit runFinished(exitCode, crashed, timedOut, wasStopped, int(m_timer.elapsed()));
}

void CompileRunner::stop()
{
    if (!m_process || m_operation == Operation::None)
        return;

    m_stopped = true;
    m_process->kill();
}
