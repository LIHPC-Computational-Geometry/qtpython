#include "pyconsole/OutputView.h"

#include <QTextCursor>
#include <QTextCharFormat>

OutputView::OutputView(QWidget* parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    QFont f("Monospace");
    f.setStyleHint(QFont::TypeWriter);
    setFont(f);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
}

void OutputView::appendColored(const QString& text, const QColor& color)
{
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(color);
    cursor.setCharFormat(fmt);
    cursor.insertText(text);
    setTextCursor(cursor);
    ensureCursorVisible();
}

void OutputView::appendStdout(const QString& text)
{
    appendColored(text, QColor(0, 0, 0));
}

void OutputView::appendStderr(const QString& text)
{
    appendColored(text, QColor(200, 0, 0));
}

void OutputView::clearOutput()
{
    clear();
}
