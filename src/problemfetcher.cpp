#include "problemfetcher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QUrl>

namespace {

// 把洛谷页面里内嵌的 Markdown 转成 QTextBrowser 能看的 HTML。
// 数学公式（$...$）保留原文，不渲染。
QString markdownToHtml(const QString &input)
{
    QString text = input;

    // 1) 围栏代码块先抽出来，避免内容被转义
    QStringList codeBlocks;
    const QRegularExpression fencePattern(QStringLiteral("```[^\\n]*\\n([\\s\\S]*?)```"));
    QList<QRegularExpressionMatch> fenceMatches;
    for (QRegularExpressionMatchIterator it = fencePattern.globalMatch(text); it.hasNext();)
        fenceMatches << it.next();
    for (qsizetype i = fenceMatches.size() - 1; i >= 0; --i) {
        const QRegularExpressionMatch &match = fenceMatches.at(i);
        codeBlocks.prepend(
            QStringLiteral("<pre>%1</pre>").arg(match.captured(1).toHtmlEscaped()));
        text.replace(match.capturedStart(), match.capturedLength(),
                     QStringLiteral("\x01CB%1\x02").arg(i));
    }

    // 2) 转义整段文字
    text = text.toHtmlEscaped();

    // 3) 还原代码块
    for (int i = 0; i < codeBlocks.size(); ++i)
        text.replace(QStringLiteral("\x01CB%1\x02").arg(i), codeBlocks.at(i));

    // 4) 行内代码、粗体、图片、链接
    static const QRegularExpression inlineCodePattern(QStringLiteral("`([^`]+)`"));
    text.replace(inlineCodePattern, QStringLiteral("<code>\\1</code>"));
    static const QRegularExpression boldPattern(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    text.replace(boldPattern, QStringLiteral("<b>\\1</b>"));
    static const QRegularExpression imagePattern(
        QStringLiteral("!\\[([^\\]]*)\\]\\(([^)\\s]+)\\)"));
    text.replace(imagePattern, QStringLiteral("<img src=\"\\2\" alt=\"\\1\">"));
    static const QRegularExpression linkPattern(
        QStringLiteral("\\[([^\\]]+)\\]\\(([^)\\s]+)\\)"));
    text.replace(linkPattern, QStringLiteral("<a href=\"\\2\">\\1</a>"));

    // 5) 段落与换行
    QStringList paragraphs;
    const QStringList raw = text.split(QStringLiteral("\n\n"));
    for (QString paragraph : raw) {
        paragraph = paragraph.trimmed();
        if (paragraph.isEmpty())
            continue;
        paragraph.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        paragraphs << QStringLiteral("<p>%1</p>").arg(paragraph);
    }
    return paragraphs.join(QLatin1Char('\n'));
}

void appendSampleBlocks(QStringList *parts, const QList<ProblemFetcher::Sample> &samples)
{
    for (int i = 0; i < samples.size(); ++i) {
        const ProblemFetcher::Sample &sample = samples.at(i);
        parts->append(QStringLiteral("<h4>样例 #%1 输入</h4><pre>%2</pre>")
                          .arg(i + 1)
                          .arg(sample.input.toHtmlEscaped()));
        parts->append(QStringLiteral("<h4>样例 #%1 输出</h4><pre>%2</pre>")
                          .arg(i + 1)
                          .arg(sample.output.toHtmlEscaped()));
    }
}

} // namespace

ProblemFetcher::ProblemFetcher(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    // 尊重 http_proxy / https_proxy 环境变量（Codeforces 在国内一般需要代理）
    QString proxy = qEnvironmentVariable("https_proxy");
    if (proxy.trimmed().isEmpty())
        proxy = qEnvironmentVariable("http_proxy");
    proxy = proxy.trimmed();

    if (!proxy.isEmpty()) {
        const QUrl url = QUrl::fromUserInput(proxy);
        if (url.isValid() && !url.host().isEmpty()) {
            m_manager->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, url.host(),
                                              static_cast<quint16>(url.port(8080))));
        }
    }
}

bool ProblemFetcher::classify(const QString &query, QString *oj, QString *id, QString *error)
{
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        if (error)
            *error = QStringLiteral("请输入题号，比如 P1001、1001、1926C、CF4A");
        return false;
    }

    // 洛谷：P1001 / p1001 / 纯数字 1001
    static const QRegularExpression luoguPattern(
        QStringLiteral("^p?(\\d{2,6})$"), QRegularExpression::CaseInsensitiveOption);
    // Codeforces：1926C / cf1926c / 4A
    static const QRegularExpression codeforcesPattern(
        QStringLiteral("^(?:cf)?(\\d{1,4})([a-z]\\d?)$"), QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch luoguMatch = luoguPattern.match(q);
    if (luoguMatch.hasMatch()) {
        *oj = QStringLiteral("luogu");
        *id = QStringLiteral("P") + luoguMatch.captured(1);
        return true;
    }

    const QRegularExpressionMatch codeforcesMatch = codeforcesPattern.match(q);
    if (codeforcesMatch.hasMatch()) {
        *oj = QStringLiteral("codeforces");
        *id = codeforcesMatch.captured(1) + codeforcesMatch.captured(2).toUpper();
        return true;
    }

    if (error)
        *error = QStringLiteral("认不出题号 %1（洛谷用 P1001，Codeforces 用 1926C）").arg(q);
    return false;
}

bool ProblemFetcher::parseLuogu(const QByteArray &data, Problem *problem, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("洛谷返回的不是 JSON");
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("code")).toInt() != 200) {
        const QString message = root.value(QStringLiteral("currentData"))
                                    .toObject()
                                    .value(QStringLiteral("errorMessage"))
                                    .toString();
        if (error)
            *error = message.isEmpty() ? QStringLiteral("洛谷接口返回错误") : message;
        return false;
    }

    const QJsonObject current = root.value(QStringLiteral("currentData")).toObject();
    const QJsonObject prob = current.value(QStringLiteral("problem")).toObject();

    problem->title = root.value(QStringLiteral("currentTitle")).toString();
    if (problem->title.isEmpty())
        problem->title = prob.value(QStringLiteral("title")).toString();

    QStringList parts;
    parts << QStringLiteral("<h2>%1</h2>").arg(problem->title.toHtmlEscaped());

    const QStringList fields{
        QStringLiteral("background"), QStringLiteral("description"),
        QStringLiteral("inputFormat"), QStringLiteral("outputFormat"),
        QStringLiteral("hint"), QStringLiteral("translation")};
    const QStringList headings{
        QStringLiteral("题目背景"), QStringLiteral("题目描述"),
        QStringLiteral("输入格式"), QStringLiteral("输出格式"),
        QStringLiteral("说明/提示"), QStringLiteral("题目翻译")};

    for (int i = 0; i < fields.size(); ++i) {
        const QString text = prob.value(fields.at(i)).toString().trimmed();
        if (text.isEmpty())
            continue;
        if (text.startsWith(QLatin1Char('<')))
            parts << QStringLiteral("<h4>%1</h4>%2").arg(headings.at(i), text);
        else
            parts << QStringLiteral("<h4>%1</h4><p>%2</p>").arg(headings.at(i), text.toHtmlEscaped());
    }

    const QJsonArray samples = prob.value(QStringLiteral("samples")).toArray();
    for (int i = 0; i < samples.size(); ++i) {
        const QJsonObject sample = samples.at(i).toObject();
        Sample s;
        s.input = sample.value(QStringLiteral("input")).toString();
        s.output = sample.value(QStringLiteral("output")).toString();
        s.input.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        s.output.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        problem->samples.append(s);
    }

    appendSampleBlocks(&parts, problem->samples);
    problem->html = parts.join(QLatin1Char('\n'));
    return true;
}

bool ProblemFetcher::parseLuoguHtml(const QByteArray &data, Problem *problem, QString *error)
{
    // _contentOnly=1 的接口被风控时，退到普通页面里内嵌的题目数据
    const QString html = QString::fromUtf8(data);
    const int scriptStart = html.indexOf(QStringLiteral("<script id=\"lentille-context\""));
    if (scriptStart < 0) {
        if (error)
            *error = QStringLiteral("页面里没有题目数据（lentille-context）");
        return false;
    }

    const int jsonStart = html.indexOf(QLatin1Char('>'), scriptStart) + 1;
    const int jsonEnd = html.indexOf(QStringLiteral("</script>"), jsonStart);
    if (jsonEnd < 0) {
        if (error)
            *error = QStringLiteral("题目数据不完整");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(html.mid(jsonStart, jsonEnd - jsonStart).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("题目数据不是合法 JSON");
        return false;
    }

    const QJsonObject prob = doc.object()
                                 .value(QStringLiteral("data"))
                                 .toObject()
                                 .value(QStringLiteral("problem"))
                                 .toObject();
    if (prob.isEmpty()) {
        if (error)
            *error = QStringLiteral("题目数据里没有 problem 字段");
        return false;
    }

    const QJsonObject content = prob.value(QStringLiteral("content")).toObject();

    problem->title = content.value(QStringLiteral("name")).toString();
    if (problem->title.isEmpty())
        problem->title = prob.value(QStringLiteral("name")).toString();
    if (problem->title.isEmpty())
        problem->title = prob.value(QStringLiteral("pid")).toString();

    QStringList parts;
    parts << QStringLiteral("<h2>%1</h2>").arg(problem->title.toHtmlEscaped());

    const struct {
        const char *key;
        QString heading;
    } fields[] = {
        {"background", QStringLiteral("题目背景")},
        {"description", QStringLiteral("题目描述")},
        {"formatI", QStringLiteral("输入格式")},
        {"formatO", QStringLiteral("输出格式")},
        {"hint", QStringLiteral("说明/提示")},
    };
    for (const auto &field : fields) {
        const QString text = content.value(QLatin1String(field.key)).toString().trimmed();
        if (text.isEmpty())
            continue;
        parts << QStringLiteral("<h4>%1</h4>%2").arg(field.heading, markdownToHtml(text));
    }

    const QJsonArray samples = prob.value(QStringLiteral("samples")).toArray();
    for (int i = 0; i < samples.size(); ++i) {
        const QJsonArray pair = samples.at(i).toArray();
        if (pair.size() < 2)
            continue;
        Sample s;
        s.input = pair.at(0).toString();
        s.output = pair.at(1).toString();
        s.input.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        s.output.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        problem->samples.append(s);
    }

    appendSampleBlocks(&parts, problem->samples);
    problem->html = parts.join(QLatin1Char('\n'));
    return true;
}

bool ProblemFetcher::parseCodeforces(const QByteArray &data, Problem *problem, QString *error)
{
    const QString html = QString::fromUtf8(data);

    static const QRegularExpression titlePattern(
        QStringLiteral("<div class=\"title\">\\s*([^<]+?)\\s*</div>"));
    const QRegularExpressionMatch titleMatch = titlePattern.match(html);
    if (!titleMatch.hasMatch()) {
        if (error)
            *error = QStringLiteral("没找到题面（页面结构变了，或被 Cloudflare 挡了）");
        return false;
    }
    problem->title = titleMatch.captured(1).trimmed();

    const int statementStart = html.indexOf(QStringLiteral("<div class=\"problem-statement\">"));
    if (statementStart < 0) {
        if (error)
            *error = QStringLiteral("没找到题面（页面结构变了，或被 Cloudflare 挡了）");
        return false;
    }

    const int sampleStart = html.indexOf(QStringLiteral("<div class=\"sample-tests\">"));

    // 语句主体：跳过 header（按 div 嵌套深度找结尾），取到 sample-tests 为止
    int bodyStart = statementStart;
    const int headerStart = html.indexOf(QStringLiteral("<div class=\"header\">"), statementStart);
    if (headerStart >= 0 && (sampleStart < 0 || headerStart < sampleStart)) {
        int depth = 0;
        int i = headerStart;
        while (i >= 0 && i < html.size()) {
            const int open = html.indexOf(QStringLiteral("<div"), i);
            const int close = html.indexOf(QStringLiteral("</div>"), i);
            if (close >= 0 && (open < 0 || close < open)) {
                --depth;
                i = close + 6;
                if (depth == 0)
                    break;
            } else if (open >= 0) {
                ++depth;
                i = open + 4;
            } else {
                break;
            }
        }
        if (depth == 0)
            bodyStart = i;
    }

    int bodyEnd = sampleStart;
    if (bodyEnd < bodyStart)
        bodyEnd = qMin(html.size(), bodyStart + 80000);

    QString statement = html.mid(bodyStart, bodyEnd - bodyStart);
    static const QRegularExpression scriptPattern(QStringLiteral("<script[\\s\\S]*?</script>"));
    statement.remove(scriptPattern);

    QStringList parts;
    parts << QStringLiteral("<h2>%1</h2>").arg(problem->title.toHtmlEscaped());
    parts << statement;

    // 样例：input/output 的 <pre> 按出现顺序配对
    if (sampleStart >= 0) {
        const QString sampleHtml = html.mid(sampleStart);
        static const QRegularExpression prePattern(
            QStringLiteral("<div class=\"(input|output)\">[\\s\\S]*?<pre>([\\s\\S]*?)</pre>"));
        QRegularExpressionMatchIterator it = prePattern.globalMatch(sampleHtml);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const bool isInput = match.captured(1) == QLatin1String("input");

            QString text = match.captured(2);
            // 解 HTML 实体（<pre> 里的 &lt; &gt; &amp; 等）
            text.replace(QLatin1String("&lt;"), QLatin1String("<"));
            text.replace(QLatin1String("&gt;"), QLatin1String(">"));
            text.replace(QLatin1String("&quot;"), QLatin1String("\""));
            text.replace(QLatin1String("&#39;"), QLatin1String("'"));
            text.replace(QLatin1String("&apos;"), QLatin1String("'"));
            text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
            text.replace(QLatin1String("&amp;"), QLatin1String("&"));

            if (isInput) {
                Sample s;
                s.input = text;
                problem->samples.append(s);
            } else if (!problem->samples.isEmpty()) {
                problem->samples.last().output = text;
            }
        }
    }

    appendSampleBlocks(&parts, problem->samples);
    problem->html = parts.join(QLatin1Char('\n'));
    return true;
}

void ProblemFetcher::fetch(const QString &query)
{
    QString oj;
    QString id;
    QString error;
    if (!classify(query, &oj, &id, &error)) {
        emit failed(error);
        return;
    }

    m_oj = oj;
    m_id = id;
    m_usingHtmlFallback = false;

    QString urlString;
    if (oj == QLatin1String("luogu"))
        urlString = QStringLiteral("https://www.luogu.com.cn/problem/%1?_contentOnly=1").arg(id);
    else {
        static const QRegularExpression splitPattern(QStringLiteral("^(\\d+)([A-Z]\\d?)$"));
        const QRegularExpressionMatch match = splitPattern.match(id);
        urlString = QStringLiteral("https://codeforces.com/problemset/problem/%1/%2")
                        .arg(match.captured(1), match.captured(2));
    }

    startRequest(QUrl(urlString));
}

void ProblemFetcher::startRequest(const QUrl &url)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",
                         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Chrome/124.0 Safari/537.36");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    request.setTransferTimeout(15000);

    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &ProblemFetcher::onReplyFinished);
}

void ProblemFetcher::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;

    const QByteArray data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    const auto fallbackToHtml = [this]() {
        // JSON 接口被风控/非 200 时，退到普通页面里内嵌的题目数据
        m_usingHtmlFallback = true;
        startRequest(QUrl(QStringLiteral("https://www.luogu.com.cn/problem/%1").arg(m_id)));
    };

    if (status != 0 && status != 200) {
        if (m_oj == QLatin1String("luogu") && !m_usingHtmlFallback) {
            fallbackToHtml();
            return;
        }
        emit failed(QStringLiteral("请求失败：HTTP %1").arg(status));
        return;
    }

    Problem problem;
    QString error;
    bool ok = false;
    if (m_oj == QLatin1String("luogu"))
        ok = m_usingHtmlFallback ? parseLuoguHtml(data, &problem, &error)
                                 : parseLuogu(data, &problem, &error);
    else
        ok = parseCodeforces(data, &problem, &error);

    if (!ok) {
        if (m_oj == QLatin1String("luogu") && !m_usingHtmlFallback) {
            fallbackToHtml();
            return;
        }
        emit failed(error.isEmpty() ? QStringLiteral("解析失败") : error);
        return;
    }

    problem.oj = m_oj;
    problem.id = m_id;
    emit fetched(problem);
}
