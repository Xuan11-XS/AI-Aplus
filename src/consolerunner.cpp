#include "consolerunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <csignal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_UNIX

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

#endif // Q_OS_UNIX

#ifdef Q_OS_WIN

// Windows 的独立控制台：用 PowerShell 开一个新控制台窗口跑程序。
// 和 Linux 版一样，壳脚本负责计时、把退出码和耗时写进 status 文件，
// Linux 版的陷阱在这里对应成两件事：
//   * 壳进程忽略 Ctrl+C（SetConsoleCtrlHandler(NULL, TRUE)），按键照旧送到程序；
//   * 程序的标准输入直接连控制台，交互式输入可用。
const char kRunScript[] = R"ps1(param(
    [string]$Exe,
    [string]$WorkDir,
    [int]$LimitMs,
    [string]$InputFile,
    [string]$StatusFile,
    [string]$PidFile
)

$ErrorActionPreference = 'Stop'
$exitCode = -1
$elapsedMs = 0
$timedOut = $false

function Write-Status([int]$Code, [long]$Milliseconds) {
    try {
        $text = "$Code`r`n$Milliseconds`r`n"
        [System.IO.File]::WriteAllText($StatusFile, $text, [System.Text.Encoding]::ASCII)
    } catch {
    }
}

# PowerShell 自己忽略 Ctrl+C，Ctrl+C 只交给运行中的程序（对应 Linux 的 trap ':' INT）
try {
    Add-Type -Namespace AplusNative -Name ConsoleHost -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool SetConsoleCtrlHandler(System.IntPtr handler, bool add);
'@ -ErrorAction Stop
    [void][AplusNative.ConsoleHost]::SetConsoleCtrlHandler([System.IntPtr]::Zero, $true)
} catch {
}

try {
    Set-Location -LiteralPath $WorkDir
} catch {
}

try {
    $Host.UI.RawUI.WindowTitle = 'Aplus 运行'
} catch {
}

$fromFile = ($InputFile -ne '') -and (Test-Path -LiteralPath $InputFile)

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $Exe
$startInfo.WorkingDirectory = $WorkDir
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardInput = $fromFile

$program = New-Object System.Diagnostics.Process
$program.StartInfo = $startInfo

$watch = [System.Diagnostics.Stopwatch]::StartNew()
try {
    [void]$program.Start()
} catch {
    $watch.Stop()
    Write-Host ''
    Write-Host "[Aplus] 无法启动程序：$Exe"
    Write-Host $_.Exception.Message
    $exitCode = -1
    $elapsedMs = $watch.ElapsedMilliseconds
    Write-Status $exitCode $elapsedMs
    Write-Host ''
    Write-Host '按任意键关闭窗口...' -NoNewline
    try { [void]$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') } catch { [void](Read-Host) }
    Write-Host ''
    exit
}

try {
    [System.IO.File]::WriteAllText($PidFile, [string]$program.Id, [System.Text.Encoding]::ASCII)
} catch {
}

if ($fromFile) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($InputFile)
        $stream = $program.StandardInput.BaseStream
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        $program.StandardInput.Close()
    } catch {
    }
}

if ($LimitMs -gt 0) {
    if (-not $program.WaitForExit($LimitMs)) {
        $timedOut = $true
        & taskkill.exe /F /T /PID $program.Id 2>$null | Out-Null
        [void]$program.WaitForExit(5000)
    }
} else {
    $program.WaitForExit()
}

$watch.Stop()
$elapsedMs = [long]$watch.ElapsedMilliseconds

if ($timedOut) {
    $exitCode = 124
    Write-Host ''
    Write-Host "[Aplus] 超出时间限制（$LimitMs ms），程序已被终止。"
} else {
    try {
        $exitCode = $program.ExitCode
    } catch {
        $exitCode = -1
    }
}

Write-Host ''
Write-Host '----------------------------------------'
if ($timedOut) {
    Write-Host "Process timed out after $elapsedMs ms."
} else {
    Write-Host "Process exited after $elapsedMs ms with return value $exitCode."
}
Write-Status $exitCode $elapsedMs
Write-Host ''

# 先排掉程序留下的回车等残留输入，再等一个真正的按键
try {
    Write-Host '按任意键关闭窗口...' -NoNewline
    [void]$Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown')
} catch {
    [void](Read-Host)
}
Write-Host ''
)ps1";

QString windowsShellProgram()
{
    static const QString program = [] {
        const QString systemRoot = qEnvironmentVariable("SystemRoot");
        if (!systemRoot.isEmpty()) {
            const QString legacy = systemRoot
                                   + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
            if (QFileInfo::exists(legacy))
                return legacy;
        }
        for (const char *name : {"powershell", "pwsh"}) {
            const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
            if (!found.isEmpty())
                return found;
        }
        return QString();
    }();
    return program;
}

#endif // Q_OS_WIN

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
#elif defined(Q_OS_WIN)
    return !windowsShellProgram().isEmpty();
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

#ifdef Q_OS_UNIX
    const QList<TerminalSpec> terminals = terminalCandidates();
    if (terminals.isEmpty())
        return false;
#elif defined(Q_OS_WIN)
    const QString shell = windowsShellProgram();
    if (shell.isEmpty())
        return false;
#else
    Q_UNUSED(exePath)
    Q_UNUSED(workingDir)
    Q_UNUSED(input)
    Q_UNUSED(timeLimitMs)
    Q_UNUSED(workspaceDir)
    return false;
#endif

    QDir().mkpath(workspaceDir);
    m_tempDir = std::make_unique<QTemporaryDir>(
        QDir(workspaceDir).filePath(QStringLiteral(".aplus-run-XXXXXX")));
    if (!m_tempDir->isValid()) {
        m_tempDir.reset();
        return false;
    }

    const QString directory = m_tempDir->path();
#ifdef Q_OS_WIN
    const QString scriptPath = directory + QStringLiteral("/run.ps1");
#else
    const QString scriptPath = directory + QStringLiteral("/run.sh");
#endif
    const QString statusPath = directory + QStringLiteral("/status");
    const QString pidPath = directory + QStringLiteral("/pid");
    const QString inputPath = input.isEmpty() ? QString()
                                              : directory + QStringLiteral("/input.txt");

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_tempDir.reset();
        return false;
    }
#ifdef Q_OS_WIN
    // Windows PowerShell 5.1 只有看到 BOM 才按 UTF-8 读脚本，否则中文会乱码
    script.write("\xEF\xBB\xBF", 3);
#endif
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

#ifdef Q_OS_WIN
    const QStringList arguments{
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), scriptPath,
        QStringLiteral("-Exe"), exePath,
        QStringLiteral("-WorkDir"), workingDir,
        QStringLiteral("-LimitMs"), QString::number(timeLimitMs),
        QStringLiteral("-InputFile"), inputPath,
        QStringLiteral("-StatusFile"), statusPath,
        QStringLiteral("-PidFile"), pidPath,
    };

    auto *process = new QProcess(this);
    process->setProgram(shell);
    process->setArguments(arguments);
    process->setWorkingDirectory(workingDir);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    // 自己开一个新控制台窗口，程序在里面运行，可以交互输入
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NEW_CONSOLE;
        args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
    });
    process->start();

    if (!process->waitForStarted(5000)) {
        process->deleteLater();
        m_tempDir.reset();
        return false;
    }

    m_terminal = process;
#else
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
#endif

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

#ifdef Q_OS_WIN
    // 控制台窗口被直接关掉时不会留下 status 文件，别等到 watchdog 才收尾
    if (m_terminal && m_terminal->state() == QProcess::NotRunning) {
        const QFileInfo info(m_statusFile);
        if (!info.exists() || info.size() <= 0) {
            m_running = false;
            m_pollTimer->stop();
            m_watchdog->stop();
            emit finished(-1, false, false, int(m_timer.elapsed()));
            return;
        }
    }
#endif

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

#ifdef Q_OS_WIN
    if (pid > 0) {
        // 只结束程序，控制台窗口留着把收尾信息打完（和 Linux 版一致）
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/F"), QStringLiteral("/T"),
                      QStringLiteral("/PID"), QString::number(pid)});
        killer.waitForFinished(5000);
    }

    if (m_terminal && m_terminal->state() != QProcess::NotRunning) {
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/F"), QStringLiteral("/T"),
                      QStringLiteral("/PID"), QString::number(m_terminal->processId())});
        killer.waitForFinished(5000);
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
#ifdef Q_OS_WIN
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/F"), QStringLiteral("/T"),
                      QStringLiteral("/PID"), QString::number(m_terminal->processId())});
        killer.waitForFinished(5000);
#endif
        if (m_terminal->state() != QProcess::NotRunning) {
            m_terminal->kill();
            m_terminal->waitForFinished(500);
        }
    }
    m_terminal->deleteLater();
    m_terminal = nullptr;
}
