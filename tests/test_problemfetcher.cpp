#include "problemfetcher.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include <cstdio>

static int g_failures = 0;

static void check(bool condition, const char *name)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++g_failures;
}

namespace {

const char kCfHtml[] = R"(<html><body>
<div class="problem-statement">
<div class="header"><div class="title">C. Birthday</div><div class="time-limit">2 seconds</div><div class="memory-limit">256 megabytes</div></div>
<div><p>Find the sum of the given <span class="tex-span">n</span> numbers.</p></div>
<div class="sample-tests"><div class="section-title">Example</div>
<div class="sample-test">
<div class="input"><div class="title">Input</div><pre>4
1 2 3 4</pre></div>
<div class="output"><div class="title">Output</div><pre>10</pre></div>
</div>
<div class="sample-test">
<div class="input"><div class="title">Input</div><pre>2
-1 5</pre></div>
<div class="output"><div class="title">Output</div><pre>4</pre></div>
</div>
</div>
</div>
<script type="text/javascript">MathJax.Hub.Config({tex2jax: {inlineMath: [['$$$','$$$']]}});</script>
</body></html>)";

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // classify
    {
        QString oj, id, error;
        check(ProblemFetcher::classify(QStringLiteral("1001"), &oj, &id, &error)
                  && oj == QLatin1String("luogu") && id == QLatin1String("P1001"),
              "classify 1001 -> luogu P1001");
        check(ProblemFetcher::classify(QStringLiteral("P1001"), &oj, &id, &error)
                  && id == QLatin1String("P1001"),
              "classify P1001");
        check(ProblemFetcher::classify(QStringLiteral("1926C"), &oj, &id, &error)
                  && oj == QLatin1String("codeforces") && id == QLatin1String("1926C"),
              "classify 1926C -> codeforces");
        check(ProblemFetcher::classify(QStringLiteral("cf4a"), &oj, &id, &error)
                  && id == QLatin1String("4A"),
              "classify cf4a -> 4A");
        check(!ProblemFetcher::classify(QStringLiteral("abc"), &oj, &id, &error),
              "classify abc fails");
        check(!ProblemFetcher::classify(QString(), &oj, &id, &error),
              "classify empty fails");
    }

    // luogu parser
    {
        QFile file(QStringLiteral(APLUS_TEST_DIR "/tests/fixtures/luogu_p1001.json"));
        check(file.open(QIODevice::ReadOnly), "luogu fixture opens");
        if (file.isOpen()) {
            ProblemFetcher::Problem problem;
            QString error;
            const bool ok = ProblemFetcher::parseLuogu(file.readAll(), &problem, &error);
            check(ok, "luogu fixture parses");
            check(problem.title == QLatin1String("P1001 A+B Problem"), "luogu title");
            check(problem.samples.size() == 2, "luogu two samples");
            check(!problem.samples.isEmpty() && problem.samples.first().input == QLatin1String("1 2"),
                  "luogu first sample input");
            check(!problem.samples.isEmpty() && problem.samples.first().output == QLatin1String("3"),
                  "luogu first sample output");
            check(problem.html.contains(QStringLiteral("<h4>样例 #1 输入</h4>")),
                  "luogu html has sample block");
            check(problem.html.contains(QStringLiteral("输入格式")), "luogu html has sections");
        }
    }

    // luogu HTML 兜底解析（页面里内嵌的 lentille JSON）
    {
        QFile file(QStringLiteral(APLUS_TEST_DIR "/tests/fixtures/luogu_p1001_lentille.html"));
        check(file.open(QIODevice::ReadOnly), "luogu lentille fixture opens");
        if (file.isOpen()) {
            ProblemFetcher::Problem problem;
            QString error;
            const bool ok = ProblemFetcher::parseLuoguHtml(file.readAll(), &problem, &error);
            check(ok, "luogu html fallback parses");
            check(problem.title == QLatin1String("A+B Problem"), "luogu html title");
            check(problem.samples.size() == 1, "luogu html one sample");
            check(!problem.samples.isEmpty()
                      && problem.samples.first().input == QLatin1String("20 30\n"),
                  "luogu html sample input");
            check(!problem.samples.isEmpty()
                      && problem.samples.first().output == QLatin1String("50\n"),
                  "luogu html sample output");
            check(problem.html.contains(QStringLiteral("输入格式")), "luogu html has sections");
            check(problem.html.contains(QStringLiteral("<pre>")), "luogu html has code blocks");
        }
    }

    // codeforces parser
    {
        ProblemFetcher::Problem problem;
        QString error;
        const bool ok = ProblemFetcher::parseCodeforces(QByteArray(kCfHtml), &problem, &error);
        check(ok, "codeforces fixture parses");
        check(problem.title == QLatin1String("C. Birthday"), "codeforces title");
        check(problem.samples.size() == 2, "codeforces two samples");
        check(!problem.samples.isEmpty() && problem.samples.first().input.startsWith(QLatin1String("4\n1 2 3 4")),
              "codeforces first sample input");
        check(!problem.samples.isEmpty() && problem.samples.first().output == QLatin1String("10"),
              "codeforces first sample output");
        check(problem.html.contains(QStringLiteral("Find the sum")),
              "codeforces statement kept");
        check(!problem.html.contains(QStringLiteral("<script")),
              "codeforces scripts stripped");
    }

    // 可选的真实网络测试：APLUS_LIVE=1 时抓洛谷 P1001
    if (qEnvironmentVariableIsSet("APLUS_LIVE")) {
        ProblemFetcher fetcher;
        ProblemFetcher::Problem problem;
        QString error;
        QEventLoop loop;
        QObject::connect(&fetcher, &ProblemFetcher::fetched, &loop,
                         [&](const ProblemFetcher::Problem &p) {
                             problem = p;
                             loop.quit();
                         });
        QObject::connect(&fetcher, &ProblemFetcher::failed, &loop,
                         [&](const QString &message) {
                             error = message;
                             loop.quit();
                         });
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        fetcher.fetch(QStringLiteral("P1001"));
        loop.exec();

        if (error.isEmpty() && problem.id == QLatin1String("P1001")) {
            std::printf("LIVE ok: %s %s samples=%lld\n", qPrintable(problem.id),
                        qPrintable(problem.title), problem.samples.size());
        } else {
            std::printf("LIVE failed: %s\n", qPrintable(error));
            ++g_failures;
        }
    }

    if (g_failures > 0)
        std::printf("%d test(s) failed\n", g_failures);
    else
        std::printf("all tests passed\n");

    return g_failures == 0 ? 0 : 1;
}
