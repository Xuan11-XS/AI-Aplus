#include "compilerunner.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

struct RunResult
{
    bool compileDone = false;
    bool compileOk = false;
    QString diagnostics;
    QString out;
    QString err;
    int exitCode = -999;
    bool crashed = false;
    bool timedOut = false;
    bool stopped = false;
    bool runDone = false;
    int elapsedMs = 0;
};

RunResult execute(const QString &directory, const QString &fileName,
                  const QString &code, const QString &input, int timeLimitMs,
                  int stopAfterMs = -1)
{
    RunResult result;

    QFile source(directory + QLatin1Char('/') + fileName);
    if (!source.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "cannot write source file %s\n", qPrintable(source.fileName()));
        return result;
    }
    source.write(code.toUtf8());
    source.close();

    CompileRunner::Options options;
    options.timeLimitMs = timeLimitMs;

    CompileRunner runner;
    QEventLoop loop;

    QObject::connect(&runner, &CompileRunner::compileFinished, &loop,
                     [&](bool ok, const QString &diagnostics, int,
                         const QString &exePath, bool stopped) {
                         result.compileDone = true;
                         result.compileOk = ok;
                         result.diagnostics = diagnostics;
                         if (!ok || stopped) {
                             loop.quit();
                             return;
                         }

                         QObject::connect(&runner, &CompileRunner::runOutput, &loop,
                                          [&](const QString &text, bool error) {
                                              if (error)
                                                  result.err += text;
                                              else
                                                  result.out += text;
                                          });

                         QObject::connect(&runner, &CompileRunner::runFinished, &loop,
                                          [&](int exitCode, bool crashed, bool timedOut,
                                              bool stopped, int elapsedMs) {
                                              result.exitCode = exitCode;
                                              result.crashed = crashed;
                                              result.timedOut = timedOut;
                                              result.stopped = stopped;
                                              result.elapsedMs = elapsedMs;
                                              result.runDone = true;
                                              loop.quit();
                                          });

                         if (stopAfterMs > 0) {
                             QTimer::singleShot(stopAfterMs, &loop, [&runner]() {
                                 runner.stop();
                             });
                         }

                         runner.run(exePath, directory, input, timeLimitMs);
                     });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(30000);

    runner.compile(directory + QLatin1Char('/') + fileName, options);
    loop.exec();

    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::fprintf(stderr, "cannot create temporary directory\n");
        return 2;
    }

    int failures = 0;
    auto check = [&failures](bool condition, const QString &name) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", qPrintable(name));
        if (!condition)
            ++failures;
    };

    {
        const QString code =
            "#include <iostream>\n"
            "int main() { int a, b; std::cin >> a >> b; std::cout << a + b << '\\n'; return 0; }\n";
        const RunResult result = execute(directory.path(), QStringLiteral("sum.cpp"),
                                         code, QStringLiteral("1 2\n"), 2000);
        check(result.compileOk, QStringLiteral("A+B compiles"));
        check(result.runDone, QStringLiteral("A+B runs"));
        check(result.out.trimmed() == QLatin1String("3"), QStringLiteral("A+B prints 3"));
        check(!result.crashed && !result.timedOut && result.exitCode == 0,
              QStringLiteral("A+B exits normally"));
    }

    {
        const QString code =
            "#include <iostream>\n"
            "int main() { cout << 1; return 0; }\n";
        const RunResult result = execute(directory.path(), QStringLiteral("broken.cpp"),
                                         code, QString(), 2000);
        check(result.compileDone, QStringLiteral("compile error is reported"));
        check(!result.compileOk, QStringLiteral("compile error fails compile"));
        check(result.diagnostics.contains(QLatin1String("error")),
              QStringLiteral("compile error contains diagnostics"));
        check(!result.runDone, QStringLiteral("compile error does not run"));
    }

    {
        const QString code =
            "#include <bits/stdc++.h>\n"
            "int main() { volatile long long x = 0; while (true) x++; }\n";
        const RunResult result = execute(directory.path(), QStringLiteral("loop.cpp"),
                                         code, QString(), 600);
        check(result.compileOk, QStringLiteral("TLE program compiles"));
        check(result.runDone, QStringLiteral("TLE program finishes"));
        check(result.timedOut, QStringLiteral("TLE is detected"));
        check(result.elapsedMs >= 500 && result.elapsedMs < 10000,
              QStringLiteral("TLE time limit is enforced"));
    }

    {
        const QString code =
            "#include <bits/stdc++.h>\n"
            "int main() { volatile long long x = 0; while (true) x++; }\n";
        const RunResult result = execute(directory.path(), QStringLiteral("stop.cpp"),
                                         code, QString(), 5000, 300);
        check(result.runDone, QStringLiteral("stop finishes the run"));
        check(result.stopped, QStringLiteral("stop is reported"));
        check(!result.timedOut, QStringLiteral("stop is not a timeout"));
    }

    {
        const QString code =
            "#include <iostream>\n"
            "int main() { std::cout << \"out\\n\"; std::cerr << \"err\\n\"; return 3; }\n";
        const RunResult result = execute(directory.path(), QStringLiteral("exit.cpp"),
                                         code, QString(), 2000);
        check(result.compileOk, QStringLiteral("exit-code program compiles"));
        check(result.runDone, QStringLiteral("exit-code program runs"));
        check(result.exitCode == 3, QStringLiteral("exit code is passed through"));
        check(result.out.contains(QLatin1String("out")), QStringLiteral("stdout is captured"));
        check(result.err.contains(QLatin1String("err")), QStringLiteral("stderr is captured"));
        check(!result.crashed, QStringLiteral("non-zero exit is not a crash"));
    }

    if (failures > 0)
        std::printf("%d test(s) failed\n", failures);
    else
        std::printf("all tests passed\n");

    return failures == 0 ? 0 : 1;
}
