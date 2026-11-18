#include "codehighlighter.h"

#include <QSet>
#include <QTextDocument>

namespace {

const QSet<QString> &keywords()
{
    static const QSet<QString> words{
        QStringLiteral("var"), QStringLiteral("let"), QStringLiteral("const"), QStringLiteral("function"),
        QStringLiteral("return"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("break"), QStringLiteral("continue"),
        QStringLiteral("switch"), QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("new"),
        QStringLiteral("delete"), QStringLiteral("typeof"), QStringLiteral("instanceof"), QStringLiteral("in"),
        QStringLiteral("of"), QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("finally"),
        QStringLiteral("throw"), QStringLiteral("this"), QStringLiteral("void"), QStringLiteral("property"),
        QStringLiteral("readonly"), QStringLiteral("signal"), QStringLiteral("class"), QStringLiteral("async"),
        QStringLiteral("await"), QStringLiteral("yield"),
    };
    return words;
}

const QSet<QString> &literals()
{
    static const QSet<QString> words{
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("null"), QStringLiteral("undefined"),
        QStringLiteral("NaN"), QStringLiteral("Infinity"),
    };
    return words;
}

bool isIdentStart(QChar c) { return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$'); }
bool isIdent(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$'); }

} // namespace

CodeColors::CodeColors(QObject *parent)
    : QObject(parent)
{
}

void CodeColors::setDocument(QQuickTextDocument *document)
{
    if (document == m_document)
        return;
    m_document = document;
    delete m_highlighter;
    m_highlighter = nullptr;
    if (m_document && m_document->textDocument()) {
        m_highlighter = new CodeHighlighter(m_document->textDocument());
        m_highlighter->setDark(m_dark);
        m_highlighter->setNames(m_names);
    }
    emit documentChanged();
}

void CodeColors::setNames(const QStringList &names)
{
    if (names == m_names)
        return;
    m_names = names;
    if (m_highlighter)
        m_highlighter->setNames(names);
    emit namesChanged();
}

void CodeColors::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    if (m_highlighter)
        m_highlighter->setDark(dark);
    emit darkChanged();
}

CodeHighlighter::CodeHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    makeFormats();
}

void CodeHighlighter::setNames(const QStringList &names)
{
    if (names == m_names)
        return;
    m_names = names;
    rehighlight();
}

void CodeHighlighter::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    makeFormats();
    rehighlight();
}

void CodeHighlighter::makeFormats()
{
    // Visual Basic's: keywords blue, comments green; and strings red,
    // numbers teal, the form's names purple
    auto format = [](const char *color, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(QColor(QLatin1String(color)));
        if (bold)
            f.setFontWeight(QFont::Bold);
        f.setFontItalic(italic);
        return f;
    };
    m_keyword = format(m_dark ? "#7fb2ff" : "#00007f", true);
    m_literal = format(m_dark ? "#7fb2ff" : "#0000c0");
    m_string = format(m_dark ? "#ff9a8a" : "#a31515");
    m_comment = format(m_dark ? "#6fdc6f" : "#008000", false, true);
    m_number = format(m_dark ? "#6fe0e0" : "#008080");
    m_name = format(m_dark ? "#e09fff" : "#800080");
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    int i = 0;
    const int n = int(text.size());
    int state = previousBlockState() < 0 ? Code : previousBlockState();

    // carried over from the line before: a /* comment or a `template`
    if (state == BlockComment) {
        const int close = int(text.indexOf(QLatin1String("*/")));
        if (close < 0) {
            setFormat(0, n, m_comment);
            setCurrentBlockState(BlockComment);
            return;
        }
        setFormat(0, close + 2, m_comment);
        i = close + 2;
        state = Code;
    } else if (state == Template) {
        int j = 0;
        for (; j < n; ++j) {
            if (text.at(j) == QLatin1Char('\\'))
                ++j;
            else if (text.at(j) == QLatin1Char('`'))
                break;
        }
        if (j >= n) {
            setFormat(0, n, m_string);
            setCurrentBlockState(Template);
            return;
        }
        setFormat(0, j + 1, m_string);
        i = j + 1;
        state = Code;
    }

    QChar before; // the last thing that wasn't a space (a . before a name: a member)
    while (i < n) {
        const QChar c = text.at(i);
        const QChar next = i + 1 < n ? text.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            setFormat(i, n - i, m_comment);
            break;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            const int close = int(text.indexOf(QLatin1String("*/"), i + 2));
            if (close < 0) {
                setFormat(i, n - i, m_comment);
                setCurrentBlockState(BlockComment);
                return;
            }
            setFormat(i, close + 2 - i, m_comment);
            i = close + 2;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            int j = i + 1;
            for (; j < n; ++j) {
                if (text.at(j) == QLatin1Char('\\'))
                    ++j;
                else if (text.at(j) == c)
                    break;
            }
            if (j >= n && c == QLatin1Char('`')) {
                setFormat(i, n - i, m_string);
                setCurrentBlockState(Template);
                return;
            }
            setFormat(i, qMin(j + 1, n) - i, m_string);
            i = j + 1;
            before = c;
            continue;
        }
        if (c.isDigit() || (c == QLatin1Char('.') && next.isDigit())) {
            int j = i;
            if (c == QLatin1Char('0') && (next == QLatin1Char('x') || next == QLatin1Char('X'))) {
                j += 2;
                while (j < n && (text.at(j).isDigit() || QStringLiteral("abcdefABCDEF").contains(text.at(j))))
                    ++j;
            } else {
                while (j < n && (text.at(j).isDigit() || text.at(j) == QLatin1Char('.') || text.at(j) == QLatin1Char('_')))
                    ++j;
                if (j < n && (text.at(j) == QLatin1Char('e') || text.at(j) == QLatin1Char('E'))) {
                    ++j;
                    if (j < n && (text.at(j) == QLatin1Char('+') || text.at(j) == QLatin1Char('-')))
                        ++j;
                    while (j < n && text.at(j).isDigit())
                        ++j;
                }
            }
            setFormat(i, j - i, m_number);
            i = j;
            before = QLatin1Char('0');
            continue;
        }
        if (isIdentStart(c)) {
            int j = i + 1;
            while (j < n && isIdent(text.at(j)))
                ++j;
            const QString word = text.mid(i, j - i);
            const bool member = before == QLatin1Char('.');
            if (!member && keywords().contains(word))
                setFormat(i, j - i, m_keyword);
            else if (!member && literals().contains(word))
                setFormat(i, j - i, m_literal);
            else if (!member && m_names.contains(word))
                setFormat(i, j - i, m_name);
            i = j;
            before = QLatin1Char('a');
            continue;
        }
        if (!c.isSpace())
            before = c;
        ++i;
    }
    setCurrentBlockState(Code);
}
