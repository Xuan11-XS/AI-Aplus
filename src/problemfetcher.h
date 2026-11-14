#ifndef APLUS_PROBLEMFETCHER_H
#define APLUS_PROBLEMFETCHER_H

#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

class ProblemFetcher : public QObject
{
    Q_OBJECT

public:
    struct Sample
    {
        QString input;
        QString output;
    };

    struct Problem
    {
        QString oj;   // "luogu" / "codeforces"
        QString id;   // "P1001" / "1926C"
        QString title;
        QString html;
        QList<Sample> samples;
    };

    explicit ProblemFetcher(QObject *parent = nullptr);

    static bool classify(const QString &query, QString *oj, QString *id, QString *error);
    static bool parseLuogu(const QByteArray &data, Problem *problem, QString *error);
    static bool parseLuoguHtml(const QByteArray &data, Problem *problem, QString *error);
    static bool parseCodeforces(const QByteArray &data, Problem *problem, QString *error);

    void fetch(const QString &query);

signals:
    void fetched(const ProblemFetcher::Problem &problem);
    void failed(const QString &message);

private:
    void onReplyFinished();

    void startRequest(const QUrl &url);

    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_oj;
    QString m_id;
    bool m_usingHtmlFallback = false;
};

#endif // APLUS_PROBLEMFETCHER_H
