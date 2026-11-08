#include "codeeditor.h"

#include "highlighter.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QWheelEvent>

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *m_editor;
};

QFont aplusMonoFont(int pointSize)
{
    QFont font;
    font.setFamilies(QStringList{
        QStringLiteral("Consolas"),
        QStringLiteral("JetBrains Maple Mono"),
        QStringLiteral("JetBrains Mono"),
        QStringLiteral("Cascadia Code"),
        QStringLiteral("DejaVu Sans Mono"),
        QStringLiteral("Noto Sans Mono"),
    });
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(pointSize);
    return font;
}

CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setFont(aplusMonoFont(10));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setCursorWidth(1);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setPlaceholderText(tr("在此输入 C++ 代码"));

    new CppHighlighter(document());

    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        m_currentLineSelection.cursor = textCursor();
        m_currentLineSelection.cursor.clearSelection();
        refreshExtraSelections();
        m_lineNumberArea->update();
    });

    m_currentLineSelection.format.setBackground(QColor(0xEA, 0xF2, 0xFB));
    m_currentLineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    m_currentLineSelection.cursor = textCursor();
    m_currentLineSelection.cursor.clearSelection();

    m_errorLineSelection.format.setBackground(QColor(0xFF, 0xE2, 0xE2));
    m_errorLineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);

    updateLineNumberAreaWidth(0);
    refreshExtraSelections();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    digits = qMax(2, digits);
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    const int width = lineNumberAreaWidth();
    setViewportMargins(width, 0, 0, 0);

    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), width, cr.height()));
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(0xF0, 0xF0, 0xF0));

    painter.setPen(QColor(0xD0, 0xD0, 0xD0));
    painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(),
                     m_lineNumberArea->width() - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int currentLine = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QColor color(0x80, 0x80, 0x80);
            if (blockNumber == currentLine)
                color = QColor(0x00, 0x00, 0x00);
            if (m_errorLine > 0 && blockNumber == m_errorLine - 1)
                color = QColor(0xC0, 0x00, 0x00);

            painter.setPen(color);
            painter.drawText(0, top, m_lineNumberArea->width() - 8, bottom - top,
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(blockNumber + 1));
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::refreshExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    if (!isReadOnly())
        selections.append(m_currentLineSelection);

    if (m_errorLine > 0) {
        const QTextBlock block = document()->findBlockByNumber(m_errorLine - 1);
        if (block.isValid()) {
            m_errorLineSelection.cursor = QTextCursor(block);
            selections.append(m_errorLineSelection);
        }
    }

    setExtraSelections(selections);
}

void CodeEditor::setErrorLine(int line)
{
    m_errorLine = qMax(0, line);
    refreshExtraSelections();
    if (m_lineNumberArea)
        m_lineNumberArea->update();
}

void CodeEditor::setEditorFontPointSize(int pointSize)
{
    pointSize = qBound(6, pointSize, 48);
    if (pointSize == font().pointSize())
        return;

    QFont newFont = font();
    newFont.setPointSize(pointSize);
    setFont(newFont);

    setTabStopDistance(QFontMetricsF(newFont).horizontalAdvance(QLatin1Char(' ')) * 4);
    m_lineNumberArea->setFont(newFont);
    updateLineNumberAreaWidth(0);

    emit fontPointSizeChanged(pointSize);
}

void CodeEditor::gotoLine(int line, int column)
{
    if (line < 1)
        line = 1;

    const QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;

    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::StartOfBlock);
    if (column > 1)
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column - 1);

    setTextCursor(cursor);
    centerCursor();
    setFocus(Qt::OtherFocusReason);
}

void CodeEditor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        int delta = event->angleDelta().y();
        if (delta == 0)
            delta = event->pixelDelta().y();
        if (delta != 0) {
            setEditorFontPointSize(editorFontPointSize() + (delta > 0 ? 1 : -1));
            event->accept();
            return;
        }
    }

    QPlainTextEdit::wheelEvent(event);
}

void CodeEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection())
            cursor.removeSelectedText();

        const QString line = cursor.block().text();
        const int position = cursor.positionInBlock();

        QString indent;
        for (int i = 0; i < position && i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t'))
                indent.append(ch);
            else
                break;
        }

        if (line.left(position).trimmed().endsWith(QLatin1Char('{')))
            indent.append(QString(4, QLatin1Char(' ')));

        cursor.insertText(QLatin1Char('\n') + indent);
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }

    if (event->key() == Qt::Key_Tab) {
        if (textCursor().hasSelection()) {
            indentSelection(true);
        } else {
            QTextCursor cursor = textCursor();
            cursor.insertText(QString(4, QLatin1Char(' ')));
            setTextCursor(cursor);
        }
        return;
    }

    if (event->key() == Qt::Key_Backtab) {
        if (textCursor().hasSelection())
            indentSelection(false);
        else
            unindentCurrentLine();
        return;
    }

    // Typing a closing brace on an otherwise empty indented line removes one indent level.
    if (event->text() == QLatin1String("}")) {
        QTextCursor cursor = textCursor();
        const QString line = cursor.block().text();
        const int position = cursor.positionInBlock();
        bool onlySpaces = true;
        for (int i = 0; i < position && i < line.size(); ++i) {
            if (!line.at(i).isSpace()) {
                onlySpaces = false;
                break;
            }
        }
        if (onlySpaces && position >= 4 && line.mid(position - 4, 4) == QLatin1String("    ")) {
            cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 4);
            cursor.removeSelectedText();
            setTextCursor(cursor);
        }
    }

    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::indentSelection(bool indent)
{
    QTextCursor cursor = textCursor();

    if (!cursor.hasSelection()) {
        if (indent) {
            const int oldPosition = cursor.position();
            QTextCursor edit(document());
            edit.setPosition(cursor.block().position());
            edit.insertText(QString(4, QLatin1Char(' ')));
            QTextCursor moved(document());
            moved.setPosition(oldPosition + 4);
            setTextCursor(moved);
        } else {
            unindentCurrentLine();
        }
        return;
    }

    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();

    QTextBlock first = document()->findBlock(start);
    QTextBlock last = document()->findBlock(end);
    if (last != first && end == last.position())
        last = last.previous();

    QTextCursor edit(document());
    edit.beginEditBlock();

    int delta = 0;
    for (QTextBlock block = first;; block = block.next()) {
        QTextCursor blockCursor(block);
        if (indent) {
            blockCursor.movePosition(QTextCursor::StartOfBlock);
            blockCursor.insertText(QString(4, QLatin1Char(' ')));
            delta += 4;
        } else {
            const QString text = block.text();
            int spaces = 0;
            while (spaces < 4 && spaces < text.size() && text.at(spaces) == QLatin1Char(' '))
                ++spaces;
            if (spaces > 0) {
                blockCursor.movePosition(QTextCursor::StartOfBlock);
                blockCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, spaces);
                blockCursor.removeSelectedText();
                delta -= spaces;
            }
        }

        if (block == last)
            break;
    }

    edit.endEditBlock();

    QTextCursor selection(document());
    selection.setPosition(start);
    selection.setPosition(qMax(start, end + delta), QTextCursor::KeepAnchor);
    setTextCursor(selection);
}

void CodeEditor::unindentCurrentLine()
{
    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const QString text = block.text();

    int spaces = 0;
    while (spaces < 4 && spaces < text.size() && text.at(spaces) == QLatin1Char(' '))
        ++spaces;
    if (spaces == 0)
        return;

    const int oldPosition = cursor.position();
    const int blockStart = block.position();

    QTextCursor edit(document());
    edit.setPosition(blockStart);
    edit.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, spaces);
    edit.removeSelectedText();

    QTextCursor moved(document());
    moved.setPosition(qMax(blockStart, oldPosition - spaces));
    setTextCursor(moved);
}
