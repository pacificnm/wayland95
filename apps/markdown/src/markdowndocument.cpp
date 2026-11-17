#include "markdowndocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QTimer>
#include <QUrl>

namespace {

constexpr qint64 kMaxFileSize = 16 * 1024 * 1024;
const QColor kCodeBack(0xee, 0xee, 0xee);
const QColor kQuoteBack(0xf4, 0xf4, 0xf0);
constexpr int kQuoteIndent = 20; // (the pale bar starts at the page's edge)
constexpr int kCodeIndent = 8;
const QColor kLinkColor(0x00, 0x00, 0xc0);
constexpr int kMaxImageWidth = 600;

// ------------------------------------------------------------------ reading

bool isCodeBlock(const QTextBlockFormat &f)
{
    return f.hasProperty(QTextFormat::BlockCodeFence) || f.nonBreakableLines();
}

// an indented code block, as Qt's reader leaves it: only a monospace font
// family on the text (inline code has fixed pitch set too)
bool isIndentedCode(const QTextBlock &b)
{
    if (b.text().isEmpty() || b.textList())
        return false;
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextCharFormat f = it.fragment().charFormat();
        const QStringList families = f.fontFamilies().toStringList();
        if (f.fontFixedPitch() || families.isEmpty() || families.first() != QLatin1String("monospace"))
            return false;
    }
    return true;
}

bool isRule(const QTextBlockFormat &f)
{
    return f.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
}

bool isMono(const QTextCharFormat &f)
{
    if (f.fontFixedPitch())
        return true;
    const QStringList families = f.fontFamilies().toStringList();
    return !families.isEmpty() && (families.first() == QLatin1String("monospace")
                                   || QFontDatabase::isFixedPitch(families.first()));
}

int listKind(const QTextBlock &b)
{
    QTextList *list = b.textList();
    if (!list)
        return MarkdownDocument::NoList;
    const QTextBlockFormat::MarkerType m = b.blockFormat().marker();
    if (m == QTextBlockFormat::MarkerType::Checked || m == QTextBlockFormat::MarkerType::Unchecked)
        return MarkdownDocument::Checks;
    const QTextListFormat::Style s = list->format().style();
    return (s == QTextListFormat::ListDisc || s == QTextListFormat::ListCircle || s == QTextListFormat::ListSquare)
        ? MarkdownDocument::Bullets : MarkdownDocument::Numbers;
}

QTextListFormat::Style listStyleFor(int kind, int level)
{
    if (kind == MarkdownDocument::Numbers)
        return QTextListFormat::ListDecimal;
    switch (level % 3) {
    case 1: return QTextListFormat::ListDisc;
    case 2: return QTextListFormat::ListCircle;
    default: return QTextListFormat::ListSquare;
    }
}

void setCodeFont(QTextCharFormat &f, bool on)
{
    if (on) {
        f.setFontFamilies(QStringList{QStringLiteral("monospace")});
        f.setFontFixedPitch(true);
        f.setBackground(kCodeBack);
    } else {
        f.clearProperty(QTextFormat::FontFamilies);
        f.clearProperty(QTextFormat::FontFamily);
        f.clearProperty(QTextFormat::FontFixedPitch);
        f.clearProperty(QTextFormat::FontStyleHint);
        f.clearProperty(QTextFormat::FontPointSize);
        f.clearBackground();
    }
}

// a block quote's pale bar (see the code block's gray one)
void setQuoteTint(QTextCursor &c, bool on)
{
    QTextCharFormat bcf = c.blockCharFormat();
    if (on)
        bcf.setBackground(kQuoteBack);
    else
        bcf.clearBackground();
    c.setBlockCharFormat(bcf);
}

// the space around a paragraph on the page (Qt's reader gives none when
// the font size is in pixels, as ours is)
void spaceBlock(QTextBlockFormat &bf, bool inList, bool inTable)
{
    qreal top = 4, bottom = 8;
    if (inTable)
        top = bottom = 0;
    else if (isCodeBlock(bf))
        top = bottom = 0;
    if (isCodeBlock(bf) && !inTable)
        bf.setLeftMargin(kCodeIndent); // inside its gray bar
    else if (bf.leftMargin() == kCodeIndent && !bf.hasProperty(QTextFormat::BlockQuoteLevel))
        bf.setLeftMargin(0);
    else if (bf.headingLevel() > 0)
        top = bf.headingLevel() <= 2 ? 14 : 10, bottom = 4;
    else if (inList)
        top = 0, bottom = 3;
    bf.setTopMargin(top);
    bf.setBottomMargin(bottom);
}

// a paragraph's space again, after it went in or out of a list
void respace(const QTextBlock &b)
{
    QTextCursor c(b);
    QTextBlockFormat bf = b.blockFormat();
    spaceBlock(bf, b.textList(), c.currentTable());
    if (bf != b.blockFormat())
        c.setBlockFormat(bf);
}

void setHeadingFont(QTextCharFormat &f, int level)
{
    if (level > 0) {
        f.setProperty(QTextFormat::FontSizeAdjustment, 4 - level);
        f.setFontWeight(QFont::Bold);
    } else {
        f.clearProperty(QTextFormat::FontSizeAdjustment);
        f.clearProperty(QTextFormat::FontWeight);
    }
}

// ------------------------------------------------------------------ writing

struct Style {
    bool bold = false, italic = false, strike = false, underline = false, code = false;
    QString href;
    bool sameEmphasis(const Style &o) const
    {
        return bold == o.bold && italic == o.italic && strike == o.strike && underline == o.underline;
    }
    bool operator==(const Style &o) const { return sameEmphasis(o) && code == o.code && href == o.href; }
};

bool isAlnum(QChar c) { return c.isLetterOrNumber(); }

QString escapeInline(const QString &text, bool inTable)
{
    QString out;
    out.reserve(text.size() + 8);
    static const QRegularExpression entity(QStringLiteral("^&(#[0-9]+|#[xX][0-9a-fA-F]+|[A-Za-z][A-Za-z0-9]*);"));
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const QChar prev = i > 0 ? text.at(i - 1) : QChar();
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();
        bool escape = false;
        switch (c.unicode()) {
        case '\\': case '*': case '`': case '[': case ']':
            escape = true;
            break;
        case '_':
            escape = !(isAlnum(prev) && isAlnum(next)); // snake_case reads fine as is
            break;
        case '~':
            escape = prev == QLatin1Char('~') || next == QLatin1Char('~');
            break;
        case '<':
            escape = next.isLetter() || next == QLatin1Char('/') || next == QLatin1Char('!') || next == QLatin1Char('?');
            break;
        case '&':
            escape = entity.match(QStringView(text).mid(i)).hasMatch();
            break;
        case '|':
            escape = inTable;
            break;
        default:
            break;
        }
        if (escape)
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// what would make a line start mean something else: "# ", "- ", "1. ", "> "...
QString escapeLineStart(const QString &line)
{
    static const QRegularExpression marker(QStringLiteral("^(#{1,6}(?=\\s|$)|[-+](?=\\s|$)|>|=+\\s*$|-+\\s*$|~~~)"));
    static const QRegularExpression number(QStringLiteral("^(\\d{1,9})([.)])(?=\\s|$)"));
    if (marker.match(line).hasMatch())
        return QLatin1Char('\\') + line;
    const QRegularExpressionMatch m = number.match(line);
    if (m.hasMatch())
        return m.captured(1) + QLatin1Char('\\') + line.mid(m.captured(1).size());
    return line;
}

QString codeSpan(const QString &text)
{
    int longest = 0, run = 0;
    for (QChar c : text) {
        run = c == QLatin1Char('`') ? run + 1 : 0;
        longest = qMax(longest, run);
    }
    const QString fence(longest + 1, QLatin1Char('`'));
    const bool pad = text.startsWith(QLatin1Char('`')) || text.endsWith(QLatin1Char('`'))
        || (text.startsWith(QLatin1Char(' ')) && text.endsWith(QLatin1Char(' ')) && !text.trimmed().isEmpty());
    return fence + (pad ? QStringLiteral(" ") : QString()) + text + (pad ? QStringLiteral(" ") : QString()) + fence;
}

QString linkTarget(const QString &url)
{
    if (url.contains(QLatin1Char(' ')) || url.contains(QLatin1Char('(')) || url.contains(QLatin1Char(')')))
        return QLatin1Char('<') + url + QLatin1Char('>');
    return url;
}

// A block's text as Markdown inline text: emphasis, code, links and images.
// Lines (hard breaks inside the block) are separated by \n.
QString inlineMarkdown(const QTextBlock &block, bool plainBold, bool inTable)
{
    struct Piece { QString text; Style style; bool image = false; QString imageName, imageAlt; };
    QList<Piece> pieces;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fr = it.fragment();
        const QTextCharFormat f = fr.charFormat();
        if (f.isImageFormat()) {
            const QTextImageFormat img = f.toImageFormat();
            for (int i = 0; i < fr.text().size(); ++i) {
                Piece p;
                p.image = true;
                p.imageName = img.name();
                p.imageAlt = img.property(QTextFormat::ImageAltText).toString();
                p.style.href = f.isAnchor() ? f.anchorHref() : QString();
                pieces << p;
            }
            continue;
        }
        Style s;
        s.code = isMono(f);
        s.bold = !plainBold && f.fontWeight() >= QFont::DemiBold;
        s.italic = f.fontItalic();
        s.strike = f.fontStrikeOut();
        s.href = f.isAnchor() ? f.anchorHref() : QString();
        s.underline = !s.code && f.fontUnderline() && s.href.isEmpty();
        pieces << Piece{fr.text(), s};
    }

    // character by character, so spaces can move outside the emphasis
    // around them ("** bold**" isn't bold in Markdown)
    struct Ch { QChar c; Style s; int image = -1; };
    QList<Ch> chars;
    for (int pi = 0; pi < pieces.size(); ++pi) {
        const Piece &p = pieces.at(pi);
        if (p.image) {
            chars << Ch{QChar::ObjectReplacementCharacter, p.style, pi};
            continue;
        }
        for (QChar c : p.text)
            chars << Ch{c, p.style};
    }
    auto isSpace = [](QChar c) { return c.isSpace() || c == QChar::LineSeparator; };
    for (int i = 0; i < chars.size();) {
        if (!isSpace(chars.at(i).c) || chars.at(i).s.code) {
            ++i;
            continue;
        }
        int j = i;
        while (j < chars.size() && isSpace(chars.at(j).c) && !chars.at(j).s.code)
            ++j;
        const Style before = i > 0 ? chars.at(i - 1).s : Style();
        const Style after = j < chars.size() ? chars.at(j).s : Style();
        for (int k = i; k < j; ++k) {
            Style &s = chars[k].s;
            s.bold = before.bold && after.bold;
            s.italic = before.italic && after.italic;
            s.strike = before.strike && after.strike;
            s.underline = before.underline && after.underline;
            if (chars.at(k).c == QChar::LineSeparator || before.href != after.href)
                s.href = (before.href == after.href) ? before.href : QString();
        }
        i = j;
    }

    // emit, opening and closing markers as the style changes
    enum Mark { MLink, MBold, MItalic, MStrike, MUnder };
    QList<Mark> open;
    QString out;
    QString linkText;
    Style cur;
    auto opener = [](Mark m) -> QString {
        switch (m) {
        case MLink: return QStringLiteral("[");
        case MBold: return QStringLiteral("**");
        case MItalic: return QStringLiteral("*");
        case MStrike: return QStringLiteral("~~");
        case MUnder: return QStringLiteral("<u>");
        }
        return {};
    };
    auto closer = [&cur](Mark m, const QString &href) -> QString {
        Q_UNUSED(cur);
        switch (m) {
        case MLink: return QStringLiteral("](") + linkTarget(href) + QLatin1Char(')');
        case MBold: return QStringLiteral("**");
        case MItalic: return QStringLiteral("*");
        case MStrike: return QStringLiteral("~~");
        case MUnder: return QStringLiteral("</u>");
        }
        return {};
    };
    auto has = [](const Style &s, Mark m) {
        switch (m) {
        case MLink: return !s.href.isEmpty();
        case MBold: return s.bold;
        case MItalic: return s.italic;
        case MStrike: return s.strike;
        case MUnder: return s.underline;
        }
        return false;
    };
    QString openHref;

    int i = 0;
    while (i <= chars.size()) {
        const bool end = i == chars.size();
        const Style next = end ? Style() : chars.at(i).s;
        // close what the next run doesn't have (and everything opened after it)
        int keep = 0;
        while (keep < open.size() && has(next, open.at(keep))
               && !(open.at(keep) == MLink && next.href != openHref))
            ++keep;
        for (int k = open.size() - 1; k >= keep; --k)
            out += closer(open.at(k), openHref);
        open.resize(keep);
        if (end)
            break;
        // open what it has that isn't open
        for (Mark m : {MLink, MBold, MItalic, MStrike, MUnder}) {
            if (has(next, m) && !open.contains(m)) {
                out += opener(m);
                open << m;
                if (m == MLink)
                    openHref = next.href;
            }
        }
        // the run itself
        int j = i;
        while (j < chars.size() && chars.at(j).s == next && chars.at(j).image < 0)
            ++j;
        if (j == i) { // an image
            const Piece &p = pieces.at(chars.at(i).image);
            // (Qt's reader drops a picture with no alt text)
            QString alt = p.imageAlt;
            if (alt.trimmed().isEmpty())
                alt = QFileInfo(QUrl(p.imageName).path()).completeBaseName();
            if (alt.trimmed().isEmpty())
                alt = QStringLiteral("picture");
            out += QStringLiteral("![") + escapeInline(alt, inTable) + QStringLiteral("](")
                + linkTarget(p.imageName) + QLatin1Char(')');
            ++i;
            continue;
        }
        QString run;
        for (int k = i; k < j; ++k)
            run += chars.at(k).c;
        if (next.code) {
            const QStringList lines = run.split(QChar::LineSeparator);
            for (int l = 0; l < lines.size(); ++l) {
                if (l)
                    out += QLatin1Char('\n');
                if (!lines.at(l).isEmpty())
                    out += codeSpan(lines.at(l)); // (Qt's reader takes | in code in a table as it is)
            }
        } else {
            out += escapeInline(run, inTable).replace(QChar::LineSeparator, QLatin1Char('\n'));
        }
        i = j;
    }
    out.replace(QChar::Nbsp, QStringLiteral("&nbsp;"));
    return out;
}

QString cellMarkdown(QTextTableCell cell, bool header)
{
    QStringList parts;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        const QTextBlock b = it.currentBlock();
        if (!b.isValid())
            continue;
        const QString t = inlineMarkdown(b, header, true).trimmed();
        if (!t.isEmpty())
            parts << QString(t).replace(QLatin1Char('\n'), QLatin1Char(' '));
    }
    // (not <br>: Qt's reader drops what follows HTML in a table)
    return parts.join(QLatin1Char(' '));
}

QStringList tableMarkdown(QTextTable *table)
{
    QStringList lines;
    const int rows = table->rows(), cols = table->columns();
    for (int r = 0; r < rows; ++r) {
        QStringList cells;
        for (int c = 0; c < cols; ++c) {
            const QTextTableCell cell = table->cellAt(r, c);
            cells << ((cell.row() == r && cell.column() == c) ? cellMarkdown(cell, r == 0) : QString());
        }
        lines << QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
        if (r == 0) {
            QStringList rule;
            for (int c = 0; c < cols; ++c) {
                const QTextTableCell cell = table->cellAt(rows > 1 ? 1 : 0, c);
                const Qt::Alignment a = cell.firstCursorPosition().blockFormat().alignment() & Qt::AlignHorizontal_Mask;
                rule << (a == Qt::AlignHCenter ? QStringLiteral(":---:") : a == Qt::AlignRight ? QStringLiteral("---:") : QStringLiteral("---"));
            }
            lines << QStringLiteral("| ") + rule.join(QStringLiteral(" | ")) + QStringLiteral(" |");
        }
    }
    return lines;
}

class Writer
{
public:
    QString out;

    void frame(QTextFrame *f)
    {
        for (auto it = f->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(child)) {
                    closeFence();
                    emitChunk(tableMarkdown(table), Kind::Table, 0);
                } else {
                    frame(child);
                }
            } else {
                block(it.currentBlock());
            }
        }
        closeFence();
    }

private:
    enum class Kind { None, Para, List, Code, Table };
    Kind m_prev = Kind::None;
    int m_prevQuote = 0;
    bool m_inFence = false;
    QString m_fenceLang;
    int m_fenceQuote = 0;
    QString m_fence;
    QString m_fenceIndent;
    int m_blankInFence = 0;
    QList<int> m_contentCols; // per list level, where an item's text starts
    QList<int> m_numbers;     // per list level, for numbered items
    QTextList *m_topList = nullptr; // the outermost list the last item was in
    int m_topKind = 0;

    static QString quotePrefix(int level)
    {
        QString p;
        for (int i = 0; i < level; ++i)
            p += QStringLiteral("> ");
        return p;
    }

    void closeFence()
    {
        m_blankInFence = 0;
        if (!m_inFence)
            return;
        out += QLatin1Char('\n') + quotePrefix(m_fenceQuote) + m_fenceIndent + m_fence;
        m_inFence = false;
    }

    void separate(Kind kind, int quote, bool newList)
    {
        if (m_prev == Kind::None)
            return;
        if (kind == Kind::List && m_prev == Kind::List && quote == m_prevQuote && !newList) {
            out += QLatin1Char('\n');
            return;
        }
        const int q = qMin(quote, m_prevQuote);
        out += QLatin1Char('\n') + quotePrefix(q).trimmed() + QLatin1Char('\n');
    }

    void emitChunk(const QStringList &lines, Kind kind, int quote, bool inList = false, bool newList = false)
    {
        // a code block in a list item follows its text directly (Qt's
        // reader takes it out of the list after a blank line)
        if (inList && m_prev == Kind::List)
            out += QLatin1Char('\n');
        else
            separate(kind, quote, newList);
        const QString prefix = quotePrefix(quote);
        for (int i = 0; i < lines.size(); ++i) {
            if (i)
                out += QLatin1Char('\n');
            out += lines.at(i).isEmpty() ? prefix.trimmed() : prefix + lines.at(i);
        }
        m_prev = kind;
        m_prevQuote = quote;
        if (inList) {
            m_prev = Kind::List; // the list goes on after it
        } else if (kind != Kind::List) {
            m_contentCols.clear();
            m_numbers.clear();
            m_topList = nullptr;
        }
    }

    void block(const QTextBlock &b)
    {
        const QTextBlockFormat bf = b.blockFormat();
        const int quote = bf.property(QTextFormat::BlockQuoteLevel).toInt();

        if (isCodeBlock(bf) || isIndentedCode(b)) {
            const QString lang = bf.stringProperty(QTextFormat::BlockCodeLanguage);
            QString text = b.text();
            text.replace(QChar::LineSeparator, QLatin1Char('\n'));
            // in a list item: indented to the item's text
            QString ind;
            if (QTextList *list = b.textList()) {
                const int level = qMax(1, list->format().indent());
                if (!m_contentCols.isEmpty())
                    ind = QString(m_contentCols.at(qMin(level, int(m_contentCols.size())) - 1), QLatin1Char(' '));
            }
            if (m_inFence && m_fenceLang == lang && m_fenceQuote == quote && m_fenceIndent == ind) {
                for (; m_blankInFence > 0; --m_blankInFence)
                    out += QLatin1Char('\n') + quotePrefix(quote).trimmed();
                for (const QString &line : text.split(QLatin1Char('\n')))
                    out += QLatin1Char('\n') + (line.isEmpty() ? quotePrefix(quote).trimmed() : quotePrefix(quote) + ind + line);
                return;
            }
            closeFence();
            m_fence = QStringLiteral("```");
            static const QRegularExpression fenceLine(QStringLiteral("^\\s*(`{3,})"), QRegularExpression::MultilineOption);
            auto it = fenceLine.globalMatch(text);
            while (it.hasNext())
                m_fence = QString(qMax(m_fence.size(), int(it.next().captured(1).size()) + 1), QLatin1Char('`'));
            QStringList lines{ind + m_fence + lang};
            for (const QString &line : text.split(QLatin1Char('\n')))
                lines << (line.isEmpty() ? line : ind + line);
            emitChunk(lines, Kind::Code, quote, !ind.isEmpty());
            m_fenceIndent = ind;
            m_inFence = true;
            m_fenceLang = lang;
            m_fenceQuote = quote;
            return;
        }
        // a blank line in an indented code block: Qt's reader makes it an
        // empty paragraph; it's part of the code if the code goes on
        if (m_inFence && b.text().isEmpty() && !isRule(bf) && !b.textList()) {
            ++m_blankInFence;
            return;
        }
        closeFence();

        if (isRule(bf)) {
            emitChunk({QStringLiteral("---")}, Kind::Para, quote);
            if (b.text().trimmed().isEmpty())
                return;
        }

        const int heading = bf.headingLevel();
        QString text = inlineMarkdown(b, heading > 0, false);
        const QStringList rawLines = text.split(QLatin1Char('\n'));
        QStringList lines;
        for (const QString &l : rawLines)
            lines << QString(l).replace(QRegularExpression(QStringLiteral("^\\s+|\\s+$")), QString());
        while (lines.size() > 1 && lines.last().isEmpty())
            lines.removeLast();
        while (lines.size() > 1 && lines.first().isEmpty())
            lines.removeFirst();

        QTextList *list = b.textList();
        if (list) {
            int level = qMax(1, list->format().indent());
            const int kind = listKind(b);
            if (m_contentCols.isEmpty())
                level = 1; // a sub-list with no item above it (after a code block)
            while (m_contentCols.size() >= level) {
                m_contentCols.removeLast();
                m_numbers.removeLast();
            }
            const int indentCols = m_contentCols.isEmpty() || level == 1 ? 0 : m_contentCols.last();
            QString marker;
            if (list->format().style() == QTextListFormat::ListDecimal || kind == MarkdownDocument::Numbers
                || (list->format().style() != QTextListFormat::ListDisc && list->format().style() != QTextListFormat::ListCircle
                    && list->format().style() != QTextListFormat::ListSquare)) {
                int start = 1;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
                start = list->format().start();
#endif
                marker = QString::number(start + list->itemNumber(b)) + QStringLiteral(". ");
            } else {
                marker = QStringLiteral("- ");
            }
            if (kind == MarkdownDocument::Checks)
                marker += bf.marker() == QTextBlockFormat::MarkerType::Checked ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
            const QString pad(indentCols, QLatin1Char(' '));
            const int contentCol = indentCols + int(marker.size());
            QStringList out;
            for (int i = 0; i < lines.size(); ++i) {
                QString l = i == 0 ? escapeLineStart(lines.at(i)) : escapeLineStart(lines.at(i));
                if (i < lines.size() - 1)
                    l += QLatin1Char('\\');
                out << (i == 0 ? pad + marker + l : QString(contentCol, QLatin1Char(' ')) + l);
            }
            if (lines.size() == 1 && lines.first().isEmpty())
                return; // an empty item (Qt's reader can't read one back)
            // another kind of list right after one: a blank line between
            // (two of the same kind can't be kept apart in Markdown)
            const int markerKind = marker.at(0).isDigit() ? 2 : 1;
            const bool newList = level == 1 && m_topList && list != m_topList && markerKind != m_topKind;
            if (level == 1) {
                m_topList = list;
                m_topKind = markerKind;
            }
            emitChunk(out, Kind::List, quote, false, newList);
            m_contentCols << contentCol;
            m_numbers << 0;
            return;
        }

        if (text.trimmed().isEmpty())
            return; // empty paragraphs: Markdown has no such thing

        QStringList out;
        for (int i = 0; i < lines.size(); ++i) {
            QString l = heading > 0 ? lines.at(i) : escapeLineStart(lines.at(i));
            if (i < lines.size() - 1)
                l += heading > 0 ? QString() : QStringLiteral("\\");
            out << l;
        }
        if (heading > 0)
            out = QStringList{QString(heading, QLatin1Char('#')) + QLatin1Char(' ') + out.join(QLatin1Char(' '))};
        emitChunk(out, Kind::Para, quote);
    }
};

} // namespace

// =================================================================== object

MarkdownDocument::MarkdownDocument(QObject *parent)
    : QObject(parent)
{
}

QTextDocument *MarkdownDocument::doc() const
{
    return m_quickDoc ? m_quickDoc->textDocument() : nullptr;
}

void MarkdownDocument::setDocument(QQuickTextDocument *d)
{
    if (d == m_quickDoc)
        return;
    if (QTextDocument *old = doc())
        old->disconnect(this);
    m_quickDoc = d;
    if (QTextDocument *nd = doc()) {
        connect(nd, &QTextDocument::modificationChanged, this, &MarkdownDocument::modifiedChanged);
        connect(nd, &QTextDocument::contentsChange, this, &MarkdownDocument::onContentsChange);
        connect(nd, &QTextDocument::blockCountChanged, this, &MarkdownDocument::onBlockCountChange);
        m_blockCount = nd->blockCount();
    }
    emit documentChanged();
    updateFormat();
}

void MarkdownDocument::setCursorPosition(int pos)
{
    if (pos == m_cursorPosition)
        return;
    m_cursorPosition = pos;
    if (pos != m_pendingPos)
        clearPending();
    emit cursorPositionChanged();
    updateFormat();
}

void MarkdownDocument::setSelectionStart(int pos)
{
    if (pos == m_selectionStart)
        return;
    m_selectionStart = pos;
    emit selectionStartChanged();
    updateFormat();
}

void MarkdownDocument::setSelectionEnd(int pos)
{
    if (pos == m_selectionEnd)
        return;
    m_selectionEnd = pos;
    emit selectionEndChanged();
    updateFormat();
}

void MarkdownDocument::setBaseDir(const QString &dir)
{
    if (dir == m_baseDir)
        return;
    m_baseDir = dir;
    emit baseDirChanged();
}

bool MarkdownDocument::modified() const
{
    QTextDocument *d = doc();
    return d && d->isModified();
}

void MarkdownDocument::setModified(bool m)
{
    if (QTextDocument *d = doc())
        d->setModified(m);
}

QTextCursor MarkdownDocument::selectionCursor() const
{
    QTextCursor c(doc());
    const int len = doc()->characterCount() - 1;
    c.setPosition(qBound(0, m_selectionStart, len));
    c.setPosition(qBound(0, m_selectionEnd, len), QTextCursor::KeepAnchor);
    return c;
}

QTextCharFormat MarkdownDocument::formatAtCursor() const
{
    QTextCursor c(doc());
    const int len = doc()->characterCount() - 1;
    if (m_selectionStart != m_selectionEnd) {
        // the first selected character
        c.setPosition(qBound(0, m_selectionStart + 1, len));
        return c.charFormat();
    }
    c.setPosition(qBound(0, m_cursorPosition, len));
    return c.charFormat();
}

QTextTable *MarkdownDocument::currentTable() const
{
    QTextCursor c(doc());
    c.setPosition(qBound(0, m_cursorPosition, doc()->characterCount() - 1));
    return c.currentTable();
}

bool MarkdownDocument::flagOf(const QTextCharFormat &f, Flag flag)
{
    switch (flag) {
    case FBold: return f.fontWeight() >= QFont::DemiBold;
    case FItalic: return f.fontItalic();
    case FUnderline: return f.fontUnderline() && !f.isAnchor();
    case FStrike: return f.fontStrikeOut();
    case FCode: return isMono(f);
    default: return false;
    }
}

void MarkdownDocument::applyFlag(QTextCharFormat &f, Flag flag, bool on)
{
    switch (flag) {
    case FBold: f.setFontWeight(on ? QFont::Bold : QFont::Normal); break;
    case FItalic: f.setFontItalic(on); break;
    case FUnderline: f.setFontUnderline(on); break;
    case FStrike: f.setFontStrikeOut(on); break;
    case FCode: setCodeFont(f, on); break;
    default: break;
    }
}

void MarkdownDocument::updateFormat()
{
    QTextDocument *d = doc();
    if (!d)
        return;
    const QTextCharFormat f = formatAtCursor();
    const bool flags[FlagCount] = {
        m_pending[FBold] >= 0 ? m_pending[FBold] == 1 : flagOf(f, FBold),
        m_pending[FItalic] >= 0 ? m_pending[FItalic] == 1 : flagOf(f, FItalic),
        m_pending[FUnderline] >= 0 ? m_pending[FUnderline] == 1 : flagOf(f, FUnderline),
        m_pending[FStrike] >= 0 ? m_pending[FStrike] == 1 : flagOf(f, FStrike),
        m_pending[FCode] >= 0 ? m_pending[FCode] == 1 : flagOf(f, FCode),
    };
    const QTextBlock b = d->findBlock(m_cursorPosition);
    const QTextBlockFormat bf = b.blockFormat();
    m_bold = flags[FBold];
    m_italic = flags[FItalic];
    m_underline = flags[FUnderline];
    m_strike = flags[FStrike];
    m_code = flags[FCode];
    m_link = f.isAnchor() ? f.anchorHref() : QString();
    m_blockStyle = isCodeBlock(bf) ? CodeBlock : qBound(0, bf.headingLevel(), 6);
    if (m_blockStyle > 0)
        m_bold = m_blockStyle == CodeBlock ? false : m_bold;
    m_quote = bf.property(QTextFormat::BlockQuoteLevel).toInt() > 0;
    m_listStyle = listKind(b);
    m_listLevel = b.textList() ? qMax(1, b.textList()->format().indent()) : 0;
    m_inTable = currentTable() != nullptr;
    emit formatChanged();
}

// ------------------------------------------------------------- the file

QVariantMap MarkdownDocument::load(const QString &path)
{
    QVariantMap result;
    const QFileInfo info(path);
    const QString name = info.fileName();
    result[QStringLiteral("ok")] = false;
    result[QStringLiteral("path")] = info.absoluteFilePath();
    if (!info.exists()) {
        result[QStringLiteral("error")] = QStringLiteral("Cannot find the %1 file.").arg(name);
        return result;
    }
    if (info.size() > kMaxFileSize) {
        result[QStringLiteral("error")] = QStringLiteral("The %1 file is too large to open.").arg(name);
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result[QStringLiteral("error")] = QStringLiteral("Cannot open the %1 file.\n\n%2").arg(name, file.errorString());
        return result;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.left(8192).contains('\0')) {
        result[QStringLiteral("error")] = QStringLiteral("%1 is not a text file, so it can't be opened here.").arg(name);
        return result;
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (utf8.hasError())
        text = QString::fromLatin1(bytes);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    setBaseDir(info.absolutePath());
    setMarkdown(text);
    result[QStringLiteral("ok")] = true;
    return result;
}

QString MarkdownDocument::save(const QString &path)
{
    QSaveFile file(path);
    const QString name = QFileInfo(path).fileName();
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot create the %1 file.\n\n%2").arg(name, file.errorString());
    // unchanged: the file as it was read (never re-written for nothing)
    const QString text = modified() ? markdown() : m_loadedText;
    file.write(text.toUtf8());
    if (!file.commit())
        return QStringLiteral("Cannot save the %1 file.\n\n%2").arg(name, file.errorString());
    m_loadedText = text;
    setBaseDir(QFileInfo(path).absolutePath());
    setModified(false);
    if (m_hasHtml != containsHtml(text)) { // (what was saved from the page has none)
        m_hasHtml = !m_hasHtml;
        emit hasHtmlChanged();
    }
    return {};
}

QString MarkdownDocument::saveText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QString name = QFileInfo(path).fileName();
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot create the %1 file.\n\n%2").arg(name, file.errorString());
    file.write(text.toUtf8());
    if (!file.commit())
        return QStringLiteral("Cannot save the %1 file.\n\n%2").arg(name, file.errorString());
    setBaseDir(QFileInfo(path).absolutePath());
    setMarkdown(text);
    return {};
}

QString MarkdownDocument::exportHtml(const QString &path) const
{
    QSaveFile file(path);
    const QString name = QFileInfo(path).fileName();
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot create the %1 file.\n\n%2").arg(name, file.errorString());
    file.write(doc()->toHtml().toUtf8());
    if (!file.commit())
        return QStringLiteral("Cannot save the %1 file.\n\n%2").arg(name, file.errorString());
    return {};
}

QString MarkdownDocument::toMarkdown(QTextDocument *d)
{
    Writer w;
    w.frame(d->rootFrame());
    QString out = w.out;
    while (out.endsWith(QLatin1Char('\n')))
        out.chop(1);
    return out.isEmpty() ? out : out + QLatin1Char('\n');
}

QString MarkdownDocument::markdown() const
{
    return doc() ? toMarkdown(doc()) : QString();
}

QString MarkdownDocument::viewText() const
{
    return modified() ? markdown() : m_loadedText;
}

bool MarkdownDocument::containsHtml(const QString &markdown)
{
    static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})[^\\n]*\\n.*?(^ {0,3}\\1[ \\t]*$|\\z)"),
                                          QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression span(QStringLiteral("(`+)[^`].*?\\1"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tag(QStringLiteral("<(/?)([A-Za-z][A-Za-z0-9-]*)(?=[\\s/>])|<!--"));
    QString text = markdown;
    text.remove(fence);
    text.remove(span);
    auto it = tag.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.captured(2).compare(QLatin1String("u"), Qt::CaseInsensitive) != 0)
            return true;
    }
    return false;
}

void MarkdownDocument::setMarkdown(const QString &text)
{
    QTextDocument *d = doc();
    if (!d)
        return;
    clearPending();
    m_loadedText = text;
    d->setUndoRedoEnabled(false);
    m_applying = true;
    d->setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    tidy();
    m_applying = false;
    d->setUndoRedoEnabled(true);
    d->setModified(false);
    m_blockCount = d->blockCount();
    const bool html = containsHtml(text);
    if (html != m_hasHtml) {
        m_hasHtml = html;
        emit hasHtmlChanged();
    }
    emit selectRequested(0, 0);
    updateFormat();
}

void MarkdownDocument::tidyBlock(QTextBlock b)
{
    QTextCursor c(b);
    QTextBlockFormat bf = b.blockFormat();
    bool changed = false;
    if (isIndentedCode(b)) { // a code block like any other
        bf.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
        bf.setNonBreakableLines(true);
        changed = true;
    }
    // Qt's reader carries a check box on to the paragraphs after a task list
    if (!b.textList() && bf.marker() != QTextBlockFormat::MarkerType::NoMarker) {
        bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
        changed = true;
    }
    const bool code = isCodeBlock(bf);
    {
        QTextBlockFormat spaced = bf;
        spaceBlock(spaced, b.textList(), c.currentTable());
        if (spaced.topMargin() != bf.topMargin() || spaced.bottomMargin() != bf.bottomMargin()
            || spaced.leftMargin() != bf.leftMargin()) {
            bf = spaced;
            changed = true;
        }
    }
    if (bf.property(QTextFormat::BlockQuoteLevel).toInt() > 0
        && bf.leftMargin() != kQuoteIndent * bf.property(QTextFormat::BlockQuoteLevel).toInt()) {
        bf.setLeftMargin(kQuoteIndent * bf.property(QTextFormat::BlockQuoteLevel).toInt());
        bf.setRightMargin(0);
        changed = true;
    }
    if (changed)
        c.setBlockFormat(bf);
    if (code) {
        // the gray bar behind a code block's line: Qt Quick draws a block's
        // background from its paragraph mark's format (not the block's)
        QTextCharFormat bcf = c.blockCharFormat();
        setCodeFont(bcf, true);
        c.setBlockCharFormat(bcf);
    } else if (bf.property(QTextFormat::BlockQuoteLevel).toInt() > 0) {
        QTextCharFormat bcf = c.blockCharFormat();
        bcf.setBackground(kQuoteBack);
        c.setBlockCharFormat(bcf);
    }

    // inline code gets a gray background, links blue and underlined, and
    // pictures wider than the page are shown smaller
    struct Fix { int s, e; QTextCharFormat f; };
    QList<Fix> fixes;
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment fr = it.fragment();
        QTextCharFormat f = fr.charFormat();
        bool fix = false;
        if (f.isImageFormat()) {
            QTextImageFormat img = f.toImageFormat();
            if (img.width() <= 0 && !m_baseDir.isEmpty()) {
                const QString file = QDir(m_baseDir).absoluteFilePath(img.name());
                const QSize size = QImageReader(file).size();
                if (size.width() > kMaxImageWidth) {
                    img.setWidth(kMaxImageWidth);
                    img.setHeight(double(size.height()) * kMaxImageWidth / size.width());
                    fixes << Fix{fr.position(), fr.position() + fr.length(), img};
                }
            }
            continue;
        }
        if (isMono(f) && !isCodeBlock(bf) && f.background().style() == Qt::NoBrush) {
            f.setBackground(kCodeBack);
            fix = true;
        }
        if (f.isAnchor() && !f.fontUnderline()) {
            f.setFontUnderline(true);
            f.setForeground(kLinkColor);
            fix = true;
        }
        if (fix)
            fixes << Fix{fr.position(), fr.position() + fr.length(), f};
    }
    for (const Fix &fx : std::as_const(fixes)) {
        c.setPosition(fx.s);
        c.setPosition(fx.e, QTextCursor::KeepAnchor);
        c.setCharFormat(fx.f);
    }
}

void MarkdownDocument::tidy()
{
    QTextDocument *d = doc();
    QTextCursor all(d);
    all.beginEditBlock();
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        tidyBlock(b);
    // table header rows in bold
    QList<QTextTable *> tables;
    for (QTextFrame *f : d->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f))
            tables << t;
    for (QTextTable *t : std::as_const(tables)) {
        // Qt's reader can leave an empty paragraph at the top of the first
        // cell (after a code block)
        for (int r = 0; r < t->rows(); ++r) {
            for (int col = 0; col < t->columns(); ++col) {
                const QTextTableCell cell = t->cellAt(r, col);
                QTextCursor first = cell.firstCursorPosition();
                if (first.block().text().isEmpty() && first.block().position() + first.block().length() <= cell.lastPosition()) {
                    first.setPosition(first.block().position() + first.block().length(), QTextCursor::KeepAnchor);
                    first.removeSelectedText();
                }
            }
        }
        QTextTableFormat tf = t->format();
        tf.setTopMargin(4);
        tf.setBottomMargin(8);
        t->setFormat(tf);
        for (int c = 0; c < t->columns(); ++c) {
            QTextTableCell cell = t->cellAt(0, c);
            QTextCursor cc = cell.firstCursorPosition();
            cc.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
            QTextCharFormat bold;
            bold.setFontWeight(QFont::Bold);
            cc.mergeCharFormat(bold);
        }
    }
    all.endEditBlock();
}

// --------------------------------------------------- typing (and its fixes)

void MarkdownDocument::clearPending()
{
    for (int &p : m_pending)
        p = -1;
    m_pendingPos = -1;
}

bool MarkdownDocument::hasPending() const
{
    for (int p : m_pending)
        if (p >= 0)
            return true;
    return false;
}

void MarkdownDocument::onContentsChange(int position, int removed, int added)
{
    Q_UNUSED(removed);
    if (m_applying || added <= 0 || !hasPending() || position != m_pendingPos)
        return;
    int pending[FlagCount];
    std::copy(std::begin(m_pending), std::end(m_pending), std::begin(pending));
    clearPending();
    // after the TextEdit has finished with the change
    QTimer::singleShot(0, this, [this, position, added, pending]() {
        QTextDocument *d = doc();
        if (!d || position + added > d->characterCount() - 1)
            return;
        QTextCursor c(d);
        c.joinPreviousEditBlock();
        m_applying = true;
        editChars(position, position + added, [pending](QTextCharFormat &f) {
            for (int i = 0; i < FlagCount; ++i)
                if (pending[i] >= 0)
                    applyFlag(f, Flag(i), pending[i] == 1);
        });
        m_applying = false;
        c.endEditBlock();
        updateFormat();
    });
}

void MarkdownDocument::onBlockCountChange(int count)
{
    const bool grew = count > m_blockCount;
    m_blockCount = count;
    if (!grew || m_applying)
        return;
    // Enter at the end of a heading or rule: the next paragraph is a normal one
    QTimer::singleShot(0, this, [this]() {
        QTextDocument *d = doc();
        if (!d)
            return;
        QTextBlock b = d->findBlock(m_cursorPosition);
        const QTextBlock prev = b.previous();
        if (!b.isValid() || !prev.isValid() || !b.text().isEmpty())
            return;
        QTextBlockFormat bf = b.blockFormat();
        const bool heading = bf.headingLevel() > 0 && prev.blockFormat().headingLevel() > 0;
        const bool rule = isRule(bf);
        if (!heading && !rule)
            return;
        QTextCursor c(b);
        c.joinPreviousEditBlock();
        m_applying = true;
        bf.setHeadingLevel(0);
        bf.clearProperty(QTextFormat::BlockTrailingHorizontalRulerWidth);
        spaceBlock(bf, b.textList(), c.currentTable());
        c.setBlockFormat(bf);
        QTextCharFormat cf = c.blockCharFormat();
        setHeadingFont(cf, 0);
        c.setBlockCharFormat(cf);
        c.setCharFormat(cf);
        m_applying = false;
        c.endEditBlock();
        updateFormat();
    });
}

// ----------------------------------------------------------- characters

void MarkdownDocument::editChars(int start, int end, const std::function<void(QTextCharFormat &)> &change)
{
    QTextDocument *d = doc();
    struct R { int s, e; QTextCharFormat f; };
    QList<R> ranges;
    for (QTextBlock b = d->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fr = it.fragment();
            const int s = qMax(start, fr.position());
            const int e = qMin(end, fr.position() + fr.length());
            if (s >= e || fr.charFormat().isImageFormat())
                continue;
            QTextCharFormat f = fr.charFormat();
            change(f);
            ranges << R{s, e, f};
        }
    }
    QTextCursor c(d);
    c.beginEditBlock();
    for (const R &r : std::as_const(ranges)) {
        c.setPosition(r.s);
        c.setPosition(r.e, QTextCursor::KeepAnchor);
        c.setCharFormat(r.f);
    }
    c.endEditBlock();
}

void MarkdownDocument::toggleFlag(Flag flag)
{
    if (!doc())
        return;
    const bool now = flag == FBold ? m_bold : flag == FItalic ? m_italic : flag == FUnderline ? m_underline
                   : flag == FStrike ? m_strike : m_code;
    const bool on = !now;
    if (m_selectionStart == m_selectionEnd) {
        // for what's typed next, as WordPad does
        m_pending[flag] = on ? 1 : 0;
        m_pendingPos = m_cursorPosition;
        updateFormat();
        return;
    }
    editChars(m_selectionStart, m_selectionEnd, [flag, on](QTextCharFormat &f) { applyFlag(f, flag, on); });
    updateFormat();
}

void MarkdownDocument::toggleBold() { toggleFlag(FBold); }
void MarkdownDocument::toggleItalic() { toggleFlag(FItalic); }
void MarkdownDocument::toggleUnderline() { toggleFlag(FUnderline); }
void MarkdownDocument::toggleStrike() { toggleFlag(FStrike); }
void MarkdownDocument::toggleCode() { toggleFlag(FCode); }

void MarkdownDocument::clearFormatting()
{
    if (!doc() || m_selectionStart == m_selectionEnd) {
        for (int i = 0; i < FlagCount; ++i)
            m_pending[i] = 0;
        m_pendingPos = m_cursorPosition;
        updateFormat();
        return;
    }
    editChars(m_selectionStart, m_selectionEnd, [](QTextCharFormat &f) {
        for (int i = 0; i < FlagCount; ++i)
            applyFlag(f, Flag(i), false);
    });
    updateFormat();
}

QVariantMap MarkdownDocument::linkInfo() const
{
    QVariantMap info;
    QTextDocument *d = doc();
    if (!d)
        return info;
    int start = m_selectionStart, end = m_selectionEnd;
    QString url;
    // the whole link the cursor is in
    const QTextBlock b = d->findBlock(m_cursorPosition);
    for (auto it = b.begin(); !it.atEnd(); ++it) {
        const QTextFragment fr = it.fragment();
        const QTextCharFormat f = fr.charFormat();
        if (!f.isAnchor() || fr.position() > m_cursorPosition || fr.position() + fr.length() < m_cursorPosition)
            continue;
        if (start == end || (start >= fr.position() && end <= fr.position() + fr.length())) {
            url = f.anchorHref();
            start = fr.position();
            end = fr.position() + fr.length();
            // the link may go on in the next fragments (a bold word in it)
            auto jt = it;
            for (++jt; !jt.atEnd() && jt.fragment().charFormat().anchorHref() == url; ++jt)
                end = jt.fragment().position() + jt.fragment().length();
            break;
        }
    }
    QTextCursor c(d);
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    info[QStringLiteral("text")] = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char(' '));
    info[QStringLiteral("url")] = url;
    info[QStringLiteral("start")] = start;
    info[QStringLiteral("end")] = end;
    return info;
}

QString MarkdownDocument::linkAt(int pos) const
{
    QTextDocument *d = doc();
    if (!d)
        return {};
    const int len = d->characterCount() - 1;
    QTextCursor c(d);
    for (int p : {pos + 1, pos}) { // the character after pos, then the one before
        if (p < 1 || p > len)
            continue;
        c.setPosition(p);
        if (c.atBlockStart())
            continue;
        const QTextCharFormat f = c.charFormat();
        if (f.isAnchor() && !f.anchorHref().isEmpty())
            return f.anchorHref();
    }
    return {};
}

void MarkdownDocument::setLink(int start, int end, const QString &text, const QString &url)
{
    QTextDocument *d = doc();
    if (!d)
        return;
    QTextCursor c(d);
    c.beginEditBlock();
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    const QString current = c.selectedText();
    if (!text.isEmpty() && text != current) {
        QTextCharFormat f = c.charFormat();
        if (start == end) {
            // inserting: start from a plain format
            f.clearProperty(QTextFormat::FontUnderline);
            f.setAnchor(false);
        }
        c.insertText(text, f);
        end = start + int(text.size());
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
    }
    c.endEditBlock();
    editChars(start, end, [url](QTextCharFormat &f) {
        if (url.isEmpty()) {
            f.setAnchor(false);
            f.clearProperty(QTextFormat::AnchorHref);
            f.setFontUnderline(false);
            f.clearForeground();
        } else {
            f.setAnchor(true);
            f.setAnchorHref(url);
            f.setFontUnderline(true);
            f.setForeground(kLinkColor);
        }
    });
    // what's typed after the link isn't part of it
    emit selectRequested(end, end);
    updateFormat();
}

void MarkdownDocument::removeLink()
{
    const QVariantMap info = linkInfo();
    if (info.value(QStringLiteral("url")).toString().isEmpty())
        return;
    setLink(info.value(QStringLiteral("start")).toInt(), info.value(QStringLiteral("end")).toInt(), QString(), QString());
}

// ----------------------------------------------------------- paragraphs

void MarkdownDocument::forEachSelectedBlock(const std::function<void(QTextBlock &)> &f)
{
    QTextDocument *d = doc();
    const int start = qMin(m_selectionStart, m_selectionEnd);
    const int end = qMax(m_selectionStart, m_selectionEnd);
    QTextBlock last = d->findBlock(end);
    for (QTextBlock b = d->findBlock(start); b.isValid(); b = b.next()) {
        QTextBlock copy = b;
        f(copy);
        if (b == last)
            break;
    }
}

void MarkdownDocument::setBlockStyle(int style)
{
    QTextDocument *d = doc();
    if (!d)
        return;
    QTextCursor edit(d);
    edit.beginEditBlock();
    forEachSelectedBlock([&](QTextBlock &b) {
        QTextCursor c(b);
        QTextBlockFormat bf = b.blockFormat();
        const bool wasCode = isCodeBlock(bf);
        if (QTextList *list = b.textList()) {
            if (style != Normal) {
                list->remove(b);
                bf = b.blockFormat();
                bf.setIndent(0);
                bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
            }
        }
        bf.setHeadingLevel(style >= 1 && style <= 6 ? style : 0);
        if (style == CodeBlock) {
            bf.setProperty(QTextFormat::BlockCodeFence, QStringLiteral("`"));
            bf.setNonBreakableLines(true);
        } else if (wasCode) {
            bf.clearProperty(QTextFormat::BlockCodeFence);
            bf.clearProperty(QTextFormat::BlockCodeLanguage);
            bf.clearProperty(QTextFormat::BlockNonBreakableLines);
        }
        bf.clearBackground();
        spaceBlock(bf, b.textList(), c.currentTable());
        c.setBlockFormat(bf);
        const int level = bf.headingLevel();
        const bool code = style == CodeBlock;
        const int bstart = b.position(), bend = b.position() + b.length() - 1;
        auto fix = [level, code, wasCode](QTextCharFormat &f) {
            setHeadingFont(f, level);
            if (code)
                setCodeFont(f, true), f.clearBackground();
            else if (wasCode)
                setCodeFont(f, false);
        };
        if (bend > bstart)
            editChars(bstart, bend, fix);
        QTextCharFormat bcf = c.blockCharFormat();
        fix(bcf);
        if (code)
            bcf.setBackground(kCodeBack); // (the gray bar)
        c.setBlockCharFormat(bcf);
    });
    edit.endEditBlock();
    clearPending();
    updateFormat();
}

void MarkdownDocument::toggleQuote()
{
    QTextDocument *d = doc();
    if (!d)
        return;
    const bool on = !m_quote;
    QTextCursor edit(d);
    edit.beginEditBlock();
    forEachSelectedBlock([&](QTextBlock &b) {
        QTextCursor c(b);
        QTextBlockFormat bf = b.blockFormat();
        if (on) {
            bf.setProperty(QTextFormat::BlockQuoteLevel, 1);
            bf.setLeftMargin(kQuoteIndent);
            bf.setRightMargin(0);
        } else {
            bf.clearProperty(QTextFormat::BlockQuoteLevel);
            bf.setLeftMargin(0);
        }
        c.setBlockFormat(bf);
        setQuoteTint(c, on);
    });
    edit.endEditBlock();
    updateFormat();
}

void MarkdownDocument::toggleList(int style)
{
    QTextDocument *d = doc();
    if (!d)
        return;
    const bool off = m_listStyle == style;
    QTextCursor edit(d);
    edit.beginEditBlock();
    QTextList *made = nullptr;
    forEachSelectedBlock([&](QTextBlock &b) {
        QTextCursor c(b);
        QTextBlockFormat bf = b.blockFormat();
        if (isCodeBlock(bf) || bf.headingLevel() > 0)
            return;
        if (off) {
            if (QTextList *list = b.textList())
                list->remove(b);
            bf = b.blockFormat();
            bf.setIndent(0);
            bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
            c.setBlockFormat(bf);
            return;
        }
        bf.setMarker(style == Checks ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::NoMarker);
        c.setBlockFormat(bf);
        if (QTextList *list = b.textList()) {
            // change the kind of the list it's in (the whole list, as Word does)
            QTextListFormat lf = list->format();
            lf.setStyle(listStyleFor(style, qMax(1, lf.indent())));
            list->setFormat(lf);
            if (style != Checks) {
                for (int i = 0; i < list->count(); ++i) {
                    QTextBlock item = list->item(i);
                    QTextBlockFormat ibf = item.blockFormat();
                    if (ibf.marker() != QTextBlockFormat::MarkerType::NoMarker) {
                        ibf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
                        QTextCursor(item).setBlockFormat(ibf);
                    }
                }
            }
            return;
        }
        if (made) {
            made->add(b);
            return;
        }
        // join the list just above when it's the same kind
        const QTextBlock prev = b.previous();
        if (prev.isValid() && prev.textList() && listKind(prev) == style
            && prev.textList()->format().indent() <= 1) {
            prev.textList()->add(b);
            made = prev.textList();
            return;
        }
        QTextListFormat lf;
        lf.setStyle(listStyleFor(style, 1));
        lf.setIndent(1);
        made = c.createList(lf);
    });
    forEachSelectedBlock([](QTextBlock &b) { respace(b); });
    edit.endEditBlock();
    updateFormat();
}

void MarkdownDocument::indent()
{
    QTextDocument *d = doc();
    if (!d)
        return;
    QTextCursor edit(d);
    edit.beginEditBlock();
    forEachSelectedBlock([&](QTextBlock &b) {
        QTextList *list = b.textList();
        if (!list) {
            // a paragraph: into a block quote
            QTextBlockFormat bf = b.blockFormat();
            const int q = bf.property(QTextFormat::BlockQuoteLevel).toInt() + 1;
            bf.setProperty(QTextFormat::BlockQuoteLevel, q);
            bf.setLeftMargin(kQuoteIndent * q);
            QTextCursor bc(b);
            bc.setBlockFormat(bf);
            setQuoteTint(bc, true);
            return;
        }
        if (list->itemNumber(b) == 0 && b.previous().isValid() && !b.previous().textList())
            return; // the first item has nothing to go under
        const int level = qMax(1, list->format().indent()) + 1;
        const int kind = listKind(b);
        // join a sub-list just above at that level, or start one
        QTextBlock prev = b.previous();
        while (prev.isValid() && prev.textList() && prev.textList()->format().indent() > level)
            prev = prev.previous();
        if (prev.isValid() && prev.textList() && prev.textList()->format().indent() == level) {
            list->remove(b);
            prev.textList()->add(b);
        } else {
            QTextListFormat lf = list->format();
            lf.setIndent(level);
            lf.setStyle(listStyleFor(kind, level));
            list->remove(b);
            QTextCursor(b).createList(lf);
        }
    });
    forEachSelectedBlock([](QTextBlock &b) { respace(b); });
    edit.endEditBlock();
    updateFormat();
}

void MarkdownDocument::outdent()
{
    QTextDocument *d = doc();
    if (!d)
        return;
    QTextCursor edit(d);
    edit.beginEditBlock();
    forEachSelectedBlock([&](QTextBlock &b) {
        QTextList *list = b.textList();
        if (!list) {
            QTextBlockFormat bf = b.blockFormat();
            const int q = bf.property(QTextFormat::BlockQuoteLevel).toInt() - 1;
            if (q < 0)
                return;
            if (q == 0)
                bf.clearProperty(QTextFormat::BlockQuoteLevel);
            else
                bf.setProperty(QTextFormat::BlockQuoteLevel, q);
            bf.setLeftMargin(kQuoteIndent * q);
            QTextCursor bc(b);
            bc.setBlockFormat(bf);
            setQuoteTint(bc, q > 0);
            return;
        }
        const int level = qMax(1, list->format().indent()) - 1;
        list->remove(b);
        QTextBlockFormat bf = b.blockFormat();
        if (level == 0) {
            bf.setIndent(0);
            bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
            QTextCursor(b).setBlockFormat(bf);
            return;
        }
        QTextBlock prev = b.previous();
        while (prev.isValid() && prev.textList() && prev.textList()->format().indent() > level)
            prev = prev.previous();
        if (prev.isValid() && prev.textList() && prev.textList()->format().indent() == level) {
            prev.textList()->add(b);
        } else {
            QTextListFormat lf = list->format();
            lf.setIndent(level);
            QTextCursor(b).createList(lf);
        }
    });
    forEachSelectedBlock([](QTextBlock &b) { respace(b); });
    edit.endEditBlock();
    updateFormat();
}

bool MarkdownDocument::isCheckItem(int pos) const
{
    QTextDocument *d = doc();
    return d && listKind(d->findBlock(pos)) == Checks;
}

int MarkdownDocument::blockStart(int pos) const
{
    QTextDocument *d = doc();
    return d ? d->findBlock(pos).position() : 0;
}

bool MarkdownDocument::isBlockEmpty(int pos) const
{
    QTextDocument *d = doc();
    return d && d->findBlock(pos).text().isEmpty();
}

QVariantList MarkdownDocument::checkItems() const
{
    QVariantList out;
    QTextDocument *d = doc();
    if (!d)
        return out;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        if (listKind(b) == Checks)
            out << b.position();
    return out;
}

QVariantList MarkdownDocument::linkStarts() const
{
    QVariantList out;
    QTextDocument *d = doc();
    if (!d)
        return out;
    QString last;
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            const QString href = f.isAnchor() ? f.anchorHref() : QString();
            if (!href.isEmpty() && href != last)
                out << it.fragment().position();
            last = href;
        }
        last.clear();
    }
    return out;
}

// --------------------------------------------------------------- insert

void MarkdownDocument::insertRule()
{
    QTextDocument *d = doc();
    if (!d)
        return;
    QTextCursor c = selectionCursor();
    c.beginEditBlock();
    c.removeSelectedText();
    if (!c.block().text().isEmpty() || c.block().textList() || c.block().blockFormat().headingLevel() > 0)
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextBlockFormat rule;
    rule.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth, QTextLength(QTextLength::PercentageLength, 100));
    if (QTextList *list = c.block().textList())
        list->remove(c.block());
    c.setBlockFormat(rule);
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    c.endEditBlock();
    emit selectRequested(c.position(), c.position());
    updateFormat();
}

void MarkdownDocument::insertTable(int rows, int columns)
{
    QTextDocument *d = doc();
    if (!d || rows < 1 || columns < 1)
        return;
    QTextCursor c = selectionCursor();
    c.beginEditBlock();
    c.removeSelectedText();
    if (!c.block().text().isEmpty())
        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    QTextTableFormat tf;
    tf.setBorder(1);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setCellPadding(4);
    tf.setCellSpacing(0);
    tf.setBorderCollapse(true);
    tf.setBorderBrush(QColor(0x80, 0x80, 0x80));
    tf.setTopMargin(4);
    tf.setBottomMargin(8);
    QTextTable *table = c.insertTable(rows, columns, tf);
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    for (int col = 0; col < columns; ++col) {
        QTextCursor cc = table->cellAt(0, col).firstCursorPosition();
        cc.setBlockCharFormat(bold);
        cc.setCharFormat(bold);
    }
    const int first = table->cellAt(0, 0).firstPosition();
    c.endEditBlock();
    emit selectRequested(first, first);
    updateFormat();
}

void MarkdownDocument::insertImage(const QString &source, const QString &alt)
{
    QTextDocument *d = doc();
    if (!d || source.isEmpty())
        return;
    QTextImageFormat img;
    img.setName(source);
    img.setProperty(QTextFormat::ImageAltText, alt);
    const QString file = QDir(m_baseDir.isEmpty() ? QDir::homePath() : m_baseDir).absoluteFilePath(source);
    const QSize size = QImageReader(file).size();
    if (size.width() > kMaxImageWidth) {
        img.setWidth(kMaxImageWidth);
        img.setHeight(double(size.height()) * kMaxImageWidth / size.width());
    }
    QTextCursor c = selectionCursor();
    c.beginEditBlock();
    c.removeSelectedText();
    c.insertImage(img);
    c.endEditBlock();
    emit selectRequested(c.position(), c.position());
    updateFormat();
}

void MarkdownDocument::insertText(const QString &text)
{
    QTextCursor c = selectionCursor();
    c.insertText(text);
    emit selectRequested(c.position(), c.position());
}

void MarkdownDocument::tableInsertRow(bool below)
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    const QTextTableCell cell = t->cellAt(m_cursorPosition);
    const int row = cell.row() + (below ? 1 : 0);
    if (row == 0)
        t->insertRows(0, 1);
    else
        t->insertRows(row, 1);
    const int pos = t->cellAt(row, cell.column()).firstPosition();
    emit selectRequested(pos, pos);
    updateFormat();
}

void MarkdownDocument::tableInsertColumn(bool right)
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    const QTextTableCell cell = t->cellAt(m_cursorPosition);
    const int col = cell.column() + (right ? 1 : 0);
    t->insertColumns(col, 1);
    const int pos = t->cellAt(cell.row(), col).firstPosition();
    emit selectRequested(pos, pos);
    updateFormat();
}

void MarkdownDocument::tableRemoveRow()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    if (t->rows() <= 1) {
        tableRemove();
        return;
    }
    const QTextTableCell cell = t->cellAt(m_cursorPosition);
    const int row = cell.row();
    t->removeRows(row, 1);
    const int pos = t->cellAt(qMin(row, t->rows() - 1), cell.column()).firstPosition();
    emit selectRequested(pos, pos);
    updateFormat();
}

void MarkdownDocument::tableRemoveColumn()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    if (t->columns() <= 1) {
        tableRemove();
        return;
    }
    const QTextTableCell cell = t->cellAt(m_cursorPosition);
    const int col = cell.column();
    t->removeColumns(col, 1);
    const int pos = t->cellAt(cell.row(), qMin(col, t->columns() - 1)).firstPosition();
    emit selectRequested(pos, pos);
    updateFormat();
}

void MarkdownDocument::tableRemove()
{
    QTextTable *t = currentTable();
    if (!t)
        return;
    QTextCursor c(doc());
    c.setPosition(t->firstPosition() - 1);
    c.setPosition(t->lastPosition() + 1, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    emit selectRequested(c.position(), c.position());
    updateFormat();
}

int MarkdownDocument::tableNextCell(bool back)
{
    QTextTable *t = currentTable();
    if (!t)
        return -1;
    const QTextTableCell cell = t->cellAt(m_cursorPosition);
    const int cols = t->columns();
    int idx = cell.row() * cols + cell.column() + (back ? -1 : 1);
    if (idx < 0)
        return -1;
    if (idx >= t->rows() * cols)
        t->appendRows(1);
    return t->cellAt(idx / cols, idx % cols).firstPosition();
}

// ----------------------------------------------------------------- find

QVariantList MarkdownDocument::find(const QString &text, int from, bool matchCase, bool backward) const
{
    QTextDocument *d = doc();
    if (!d || text.isEmpty())
        return {};
    QTextDocument::FindFlags flags;
    if (matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    if (backward)
        flags |= QTextDocument::FindBackward;
    const QTextCursor c = d->find(text, from, flags);
    if (c.isNull())
        return {};
    return {c.selectionStart(), c.selectionEnd()};
}

void MarkdownDocument::replaceRange(int start, int end, const QString &text)
{
    QTextCursor c(doc());
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    c.insertText(text);
    emit selectRequested(start, start + int(text.size()));
}

int MarkdownDocument::replaceAll(const QString &text, const QString &with, bool matchCase)
{
    QTextDocument *d = doc();
    if (!d || text.isEmpty())
        return 0;
    QTextDocument::FindFlags flags;
    if (matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    int n = 0;
    QTextCursor edit(d);
    edit.beginEditBlock();
    QTextCursor c = d->find(text, 0, flags);
    while (!c.isNull()) {
        c.insertText(with);
        ++n;
        c = d->find(text, c.position(), flags);
    }
    edit.endEditBlock();
    return n;
}

int MarkdownDocument::wordCount() const
{
    QTextDocument *d = doc();
    if (!d)
        return 0;
    static const QRegularExpression word(QStringLiteral("[\\w'’-]+"));
    int n = 0;
    auto it = word.globalMatch(d->toPlainText());
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

QString MarkdownDocument::relativePath(const QString &path) const
{
    if (m_baseDir.isEmpty())
        return path;
    const QString rel = QDir(m_baseDir).relativeFilePath(path);
    return rel.startsWith(QLatin1String("../../")) ? path : rel;
}
