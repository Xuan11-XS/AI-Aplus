#ifndef APLUS_HIGHLIGHTER_H
#define APLUS_HIGHLIGHTER_H

#include <QRegularExpression>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class CppHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit CppHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Normal = 0, InComment = 1 };

    QSet<QString> m_keywords;
    QTextCharFormat m_keywordFormat;
    QTextCharFormat m_typeFormat;
    QTextCharFormat m_preprocessorFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_charFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_numberFormat;
    QTextCharFormat m_functionFormat;
};

#endif // APLUS_HIGHLIGHTER_H
