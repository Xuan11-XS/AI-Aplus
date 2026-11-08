#include "highlighter.h"

#include <QFont>
#include <QTextDocument>

CppHighlighter::CppHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    const QStringList keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype",
        "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "requires", "return",
        "short", "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw", "true",
        "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
    };
    m_keywords = QSet<QString>(keywords.begin(), keywords.end());

    m_keywordFormat.setForeground(QColor(0x00, 0x00, 0xCC));
    m_keywordFormat.setFontWeight(QFont::Bold);

    m_typeFormat.setForeground(QColor(0x00, 0x00, 0xCC));

    m_preprocessorFormat.setForeground(QColor(0x80, 0x40, 0x00));

    m_stringFormat.setForeground(QColor(0xA3, 0x15, 0x15));
    m_charFormat.setForeground(QColor(0xA3, 0x15, 0x15));

    m_commentFormat.setForeground(QColor(0x00, 0x80, 0x00));
    m_commentFormat.setFontItalic(true);

    m_numberFormat.setForeground(QColor(0x09, 0x86, 0x58));

    m_functionFormat.setForeground(QColor(0x79, 0x5E, 0x26));
}

void CppHighlighter::highlightBlock(const QString &text)
{
    const int length = text.size();
    int index = 0;

    if (previousBlockState() == InComment) {
        const int end = text.indexOf(QLatin1String("*/"));
        if (end < 0) {
            setFormat(0, length, m_commentFormat);
            setCurrentBlockState(InComment);
            return;
        }
        setFormat(0, end + 2, m_commentFormat);
        index = end + 2;
    }
    setCurrentBlockState(Normal);

    // Preprocessor directives are coloured up to the first comment on the line.
    int first = 0;
    while (first < length && text.at(first).isSpace())
        ++first;

    if (first < length && text.at(first) == QLatin1Char('#')) {
        int end = length;
        const int lineComment = text.indexOf(QLatin1String("//"), first);
        const int blockComment = text.indexOf(QLatin1String("/*"), first);
        if (lineComment >= 0)
            end = lineComment;
        if (blockComment >= 0 && blockComment < end)
            end = blockComment;
        setFormat(first, end - first, m_preprocessorFormat);
        if (end >= length)
            return;
        index = end;
    }

    while (index < length) {
        const QChar ch = text.at(index);

        if (ch == QLatin1Char('/') && index + 1 < length) {
            if (text.at(index + 1) == QLatin1Char('/')) {
                setFormat(index, length - index, m_commentFormat);
                return;
            }
            if (text.at(index + 1) == QLatin1Char('*')) {
                const int end = text.indexOf(QLatin1String("*/"), index + 2);
                if (end < 0) {
                    setFormat(index, length - index, m_commentFormat);
                    setCurrentBlockState(InComment);
                    return;
                }
                setFormat(index, end + 2 - index, m_commentFormat);
                index = end + 2;
                continue;
            }
        }

        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            const QChar quote = ch;
            int end = index + 1;
            while (end < length) {
                if (text.at(end) == QLatin1Char('\\') && end + 1 < length) {
                    end += 2;
                    continue;
                }
                if (text.at(end) == quote) {
                    ++end;
                    break;
                }
                ++end;
            }
            setFormat(index, qMin(end, length) - index,
                      quote == QLatin1Char('"') ? m_stringFormat : m_charFormat);
            index = end;
            continue;
        }

        if (ch.isDigit()) {
            int end = index;
            while (end < length) {
                const QChar c = text.at(end);
                if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')
                    || c == QLatin1Char('\'')) {
                    ++end;
                } else {
                    break;
                }
            }
            setFormat(index, end - index, m_numberFormat);
            index = end;
            continue;
        }

        if (ch.isLetter() || ch == QLatin1Char('_')) {
            int end = index + 1;
            while (end < length && (text.at(end).isLetterOrNumber() || text.at(end) == QLatin1Char('_')))
                ++end;

            const QString word = text.mid(index, end - index);
            if (m_keywords.contains(word)) {
                setFormat(index, end - index, m_keywordFormat);
            } else {
                int next = end;
                while (next < length && text.at(next).isSpace())
                    ++next;
                if (next < length && text.at(next) == QLatin1Char('('))
                    setFormat(index, end - index, m_functionFormat);
            }
            index = end;
            continue;
        }

        ++index;
    }
}
