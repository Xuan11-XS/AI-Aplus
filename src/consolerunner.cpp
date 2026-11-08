#include "consolerunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <csignal>

namespace {

enum class TerminalKind { Kgx, GnomeTerminal, Konsole, Xfce };

struct TerminalSpec
{
    TerminalKind kind;
    QString program;
};

const char kRunScript[] = R"bash(#!/bin/bash
exe="$1"
workdir="$2"
limit_ms="$3"
input_file="$4"
status_file="$5"
pid_file="$6"

cd "$workdir" || exit 1
printf '\033]0;Aplus 运行\007'

# 保存终端输入；壳脚本扛住 Ctrl+C，子进程仍会收到 SIGINT
exec 3<&0
trap ':' INT

if [ "$limit_ms" -gt 0 ]; then
    whole=$((limit_ms / 1000))
    frac=$((limit_ms % 1000))
    limit_s="${whole}.$(printf '%03d' "$frac")"
    timeout_args=(timeout -k 0.3 "$limit_s")
else
    timeout_args=()
fi

start=$(date +%s%N)

if [ -n "$input_file" ] && [ -f "$input_file" ]; then
    # 输入来自文件：放进独立会话，Ctrl+F2 整体停止
    setsid "${timeout_args[@]}" "$exe" < "$input_file" &
else
    # 交互式：显式从终端读输入（bash 会给裸后台任务接 /dev/null），
    # 且留在前台进程组里，键盘和 Ctrl+C 都能到达程序
    ( exec "$exe" ) <&3 &
fi

child=$!
echo "$child" > "$pid_file"
wait "$child"
code=$?

end=$(date +%s%N)
ms=$(( (end - start) / 1000000 ))

if [ "$code" -eq 124 ]; then
    echo
    echo "[Aplus] 超出时间限制（${limit_ms} ms），程序已被终止。"
fi

echo
echo "----------------------------------------"
if [ "$code" -eq 124 ]; then
    echo "Process timed out after ${ms} ms."
else
    echo "Process exited after ${ms} ms with return value ${code}."
fi

printf '%s\n%s\n' "$code" "$ms" > "$status_file"
echo
# 先排掉程序留下的回车等残留输入，再等一个真正的按键
while IFS= read -r -n 1 -t 0.15; do :; done
read -n 1 -s -r -p "按任意键关闭窗口..."
echo
)bash";

QList<TerminalSpec> terminalCandidates()
{
    QList<TerminalSpec> candidates;
    const auto add = [&candidates](TerminalKind kind, const char *name) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!path.isEmpty())
            candidates.append(TerminalSpec{kind, path});
    };

    add(TerminalKind::Kgx, "kgx");
    add(TerminalKind::GnomeTerminal, "gnome-terminal");
    add(TerminalKind::Konsole, "konsole");
    add(TerminalKind::Xfce, "xfce4-terminal");
    return candidates;
}

QStringList terminalArguments(TerminalKind kind, const QString &workDir,
                              const QString &scriptPath, const QStringList &programArgs)
{
    QStringList arguments;
    switch (kind) {
    case TerminalKind::Kgx:
        arguments << QStringLiteral("--")
                  << QStringLiteral("bash") << scriptPath;
        break;
    case TerminalKind::GnomeTerminal:
        arguments << QStringLiteral("--title=Aplus 运行")
                  << QStringLiteral("--")
                  << QStringLiteral("bash") << scriptPath;
        break;
    case TerminalKind::Konsole:
        arguments << QStringLiteral("-e")
                  << QStringLiteral("bash") << scriptPath;
        break;
    case TerminalKind::Xfce:
        arguments << QStringLiteral("-x")
                  << QStringLiteral("bash") << scriptPath;
        break;
    }

    Q_UNUSED(workDir)
    arguments << programArgs;
    return arguments;
}

} // namespace

ConsoleRunner::ConsoleRunner(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_watchdog(new QTimer(this))
{
    m_pollTimer->setInterval(100);
    connect(m_pollTimer, &QTimer::timeout, this, &ConsoleRunner::pollStatus);

    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        if (!m_running)
            return;
        m_running = false;
        m_pollTimer->stop();
        emit finished(-1, false, false, int(m_timer.elapsed()));
    });
}

ConsoleRunner::~ConsoleRunner()
{
    if (m_running)
        killProgramGroup();
    resetTerminal();
}

bool ConsoleRunner::available()
{
#if defined(Q_OS_UNIX)
    return !terminalCandidates().isEmpty();
#else
    return false;
#endif
}

bool ConsoleRunner::start(const QString &exePath, const QString &workingDir,
                          const QString &input, int timeLimitMs,
                          const QString &workspaceDir)
{
    if (m_running)
        return false;

    resetTerminal();

    const QList<TerminalSpec> terminals = terminalCandidates();
    if (terminals.isEmpty())
        return false;

    QDir().mkpath(workspaceDir);
    m_tempDir = std::make_unique<QTemporaryDir>(
        QDir(workspaceDir).filePath(QStringLiteral(".aplus-run-XXXXXX")));
    if (!m_tempDir->isValid()) {
        m_tempDir.reset();
        return false;
    }

    const QString directory = m_tempDir->path();
    const QString scriptPath = directory + QStringLiteral("/run.sh");
    const QString statusPath = directory + QStringLiteral("/status");
    const QString pidPath = directory + QStringLiteral("/pid");
    const QString inputPath = input.isEmpty() ? QString()
                                              : directory + QStringLiteral("/input.txt");

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_tempDir.reset();
        return false;
    }
    script.write(kRunScript);
    script.close();

    if (!inputPath.isEmpty()) {
        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_tempDir.reset();
            return false;
        }
        inputFile.write(input.toUtf8());
        inputFile.close();
    }

    m_statusFile = statusPath;
    m_pidFile = pidPath;

    const QStringList programArgs{
        exePath,
        workingDir,
        QString::number(timeLimitMs),
        inputPath,
        statusPath,
        pidPath,
    };

    bool launched = false;
    for (const TerminalSpec &terminal : terminals) {
        auto *process = new QProcess(this);
        process->setProgram(terminal.program);
        process->setArguments(terminalArguments(terminal.kind, workingDir, scriptPath, programArgs));
        process->setWorkingDirectory(workingDir);
        process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        process->start();

        if (process->waitForStarted(2500)) {
            m_terminal = process;
            launched = true;
            break;
        }

        process->deleteLater();
    }

    if (!launched) {
        m_tempDir.reset();
        return false;
    }

    m_timer.start();
    m_running = true;
    m_pollTimer->start();
    m_watchdog->start(timeLimitMs > 0 ? qMax(30000, timeLimitMs + 30000) : 600000);

    emit started();
    return true;
}

void ConsoleRunner::pollStatus()
{
    if (!m_running)
        return;

    const QFileInfo info(m_statusFile);
    if (!info.exists() || info.size() <= 0)
        return;

    QFile file(m_statusFile);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    bool codeOk = false;
    const int exitCode = lines.value(0).trimmed().toInt(&codeOk);
    if (!codeOk)
        return;

    int elapsedMs = lines.value(1).trimmed().toInt();
    if (elapsedMs <= 0)
        elapsedMs = int(m_timer.elapsed());

    m_running = false;
    m_pollTimer->stop();
    m_watchdog->stop();

    emit finished(exitCode, exitCode == 124, false, elapsedMs);
}

void ConsoleRunner::stop()
{
    if (!m_running)
        return;

    killProgramGroup();

    m_running = false;
    m_pollTimer->stop();
    m_watchdog->stop();
    emit finished(-1, false, true, int(m_timer.elapsed()));
}

void ConsoleRunner::killProgramGroup()
{
    qint64 pid = 0;
    QFile pidFile(m_pidFile);
    if (pidFile.open(QIODevice::ReadOnly))
        pid = pidFile.readAll().trimmed().toLongLong();

#ifdef Q_OS_UNIX
    if (pid > 0) {
        // 先杀程序本身（交互模式程序在壳进程组里），再清独立会话的整组
        ::kill(static_cast<pid_t>(pid), SIGKILL);
        ::kill(static_cast<pid_t>(-pid), SIGKILL);
    }
#endif

    if (pid <= 0 && m_terminal && m_terminal->state() != QProcess::NotRunning)
        m_terminal->kill();
}

void ConsoleRunner::resetTerminal()
{
    if (!m_terminal)
        return;

    if (m_terminal->state() != QProcess::NotRunning) {
        m_terminal->kill();
        m_terminal->waitForFinished(500);
    }
    m_terminal->deleteLater();
    m_terminal = nullptr;
}
