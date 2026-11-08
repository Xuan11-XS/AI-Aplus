#ifndef APLUS_CODEEDITOR_H
#define APLUS_CODEEDITOR_H

#include <QPlainTextEdit>
#include <QTextEdit>

class LineNumberArea;

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget *parent = nullptr);

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

    void gotoLine(int line, int column = 1);
    void setErrorLine(int line);
    int errorLine() const { return m_errorLine; }

    int editorFontPointSize() const { return font().pointSize(); }
    void setEditorFontPointSize(int pointSize);

signals:
    void fontPointSizeChanged(int pointSize);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void refreshExtraSelections();
    void indentSelection(bool indent);
    void unindentCurrentLine();

    QWidget *m_lineNumberArea = nullptr;
    int m_errorLine = 0;
    QTextEdit::ExtraSelection m_currentLineSelection;
    QTextEdit::ExtraSelection m_errorLineSelection;
};

QFont aplusMonoFont(int pointSize = 10);

#endif // APLUS_CODEEDITOR_H
