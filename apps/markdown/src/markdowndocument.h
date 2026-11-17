#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QTextDocument;
class QTextBlock;
class QTextTable;

// The document behind the Markdown Editor's page: a TextEdit's QTextDocument,
// read from Markdown (Qt's reader) and written back to it (our own writer:
// Qt's own one loses code blocks and list breaks, and re-wraps paragraphs).
//
// The window keeps cursorPosition/selectionStart/selectionEnd in step with
// its TextEdit; the format properties (bold, blockStyle, listStyle...) say
// what is at the cursor, for the format bar, and the toggle functions change
// the selection, or with nothing selected, what is typed next (as WordPad's
// buttons do).
//
// What a Markdown file can keep: headings, bold, italic, strikethrough,
// underline (as <u>), code, links, images, bulleted/numbered/check lists,
// block quotes, code blocks, tables and horizontal rules. Paragraphs are
// written one line each.
class MarkdownDocument : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorPositionChanged)
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart NOTIFY selectionStartChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionEndChanged)
    Q_PROPERTY(QString baseDir READ baseDir WRITE setBaseDir NOTIFY baseDirChanged)

    Q_PROPERTY(bool bold READ bold NOTIFY formatChanged)
    Q_PROPERTY(bool italic READ italic NOTIFY formatChanged)
    Q_PROPERTY(bool underline READ underline NOTIFY formatChanged)
    Q_PROPERTY(bool strike READ strike NOTIFY formatChanged)
    Q_PROPERTY(bool code READ code NOTIFY formatChanged)
    Q_PROPERTY(QString link READ link NOTIFY formatChanged)
    // 0 normal, 1..6 heading, 7 code block
    Q_PROPERTY(int blockStyle READ blockStyle NOTIFY formatChanged)
    Q_PROPERTY(bool quote READ quote NOTIFY formatChanged)
    // 0 none, 1 bullets, 2 numbers, 3 check boxes
    Q_PROPERTY(int listStyle READ listStyle NOTIFY formatChanged)
    Q_PROPERTY(int listLevel READ listLevel NOTIFY formatChanged)
    Q_PROPERTY(bool inTable READ inTable NOTIFY formatChanged)
    Q_PROPERTY(bool modified READ modified WRITE setModified NOTIFY modifiedChanged)
    // the file has HTML in it (beyond <u>), which the page can't keep
    Q_PROPERTY(bool hasHtml READ hasHtml NOTIFY hasHtmlChanged)

public:
    enum BlockStyle { Normal = 0, CodeBlock = 7 };
    Q_ENUM(BlockStyle)
    enum ListStyle { NoList = 0, Bullets = 1, Numbers = 2, Checks = 3 };
    Q_ENUM(ListStyle)

    explicit MarkdownDocument(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument *doc);
    int cursorPosition() const { return m_cursorPosition; }
    void setCursorPosition(int pos);
    int selectionStart() const { return m_selectionStart; }
    void setSelectionStart(int pos);
    int selectionEnd() const { return m_selectionEnd; }
    void setSelectionEnd(int pos);
    QString baseDir() const { return m_baseDir; }
    void setBaseDir(const QString &dir);

    bool bold() const { return m_bold; }
    bool italic() const { return m_italic; }
    bool underline() const { return m_underline; }
    bool strike() const { return m_strike; }
    bool code() const { return m_code; }
    QString link() const { return m_link; }
    int blockStyle() const { return m_blockStyle; }
    bool quote() const { return m_quote; }
    int listStyle() const { return m_listStyle; }
    int listLevel() const { return m_listLevel; }
    bool inTable() const { return m_inTable; }
    bool modified() const;
    bool hasHtml() const { return m_hasHtml; }
    static bool containsHtml(const QString &markdown);
    void setModified(bool m);

    // files: { ok, path, error }; save/export: "" or what went wrong
    Q_INVOKABLE QVariantMap load(const QString &path);
    Q_INVOKABLE QString save(const QString &path);
    Q_INVOKABLE QString exportHtml(const QString &path) const;
    // the Markdown view's text, saved as typed (and read back into the page)
    Q_INVOKABLE QString saveText(const QString &path, const QString &text);
    Q_INVOKABLE QString markdown() const;
    // what View > Markdown shows: the file as it is until the page changes
    // (then the page, as Markdown)
    Q_INVOKABLE QString viewText() const;
    // replaces the whole document (New, and back from the Markdown view)
    Q_INVOKABLE void setMarkdown(const QString &text);
    Q_INVOKABLE static QString toMarkdown(QTextDocument *doc);

    // character formats
    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleUnderline();
    Q_INVOKABLE void toggleStrike();
    Q_INVOKABLE void toggleCode();
    Q_INVOKABLE void clearFormatting();
    // the link under the cursor (or the selected text): { text, url, start, end }
    Q_INVOKABLE QVariantMap linkInfo() const;
    // the link of the character at either side of pos ("" if none): what's
    // under the mouse, with TextEdit.positionAt (TextEdit.linkAt misses
    // with padding)
    Q_INVOKABLE QString linkAt(int pos) const;
    // put a link over start..end (its text replaced by `text` when given)
    Q_INVOKABLE void setLink(int start, int end, const QString &text, const QString &url);
    Q_INVOKABLE void removeLink();

    // paragraph formats
    Q_INVOKABLE void setBlockStyle(int style);
    Q_INVOKABLE void toggleQuote();
    Q_INVOKABLE void toggleList(int style);
    Q_INVOKABLE void indent();
    Q_INVOKABLE void outdent();
    Q_INVOKABLE bool isCheckItem(int pos) const;
    Q_INVOKABLE int blockStart(int pos) const;
    Q_INVOKABLE bool isBlockEmpty(int pos) const;
    // where the check boxes are (their paragraphs' starts), for the tests
    Q_INVOKABLE QVariantList checkItems() const;
    // and where each link starts
    Q_INVOKABLE QVariantList linkStarts() const;

    // insert
    Q_INVOKABLE void insertRule();
    Q_INVOKABLE void insertTable(int rows, int columns);
    Q_INVOKABLE void insertImage(const QString &source, const QString &alt);
    Q_INVOKABLE void insertText(const QString &text);
    Q_INVOKABLE void tableInsertRow(bool below);
    Q_INVOKABLE void tableInsertColumn(bool right);
    Q_INVOKABLE void tableRemoveRow();
    Q_INVOKABLE void tableRemoveColumn();
    Q_INVOKABLE void tableRemove();
    // Tab/Shift+Tab in a table: the next (or previous) cell's position, a
    // new row after the last cell; -1 before the first
    Q_INVOKABLE int tableNextCell(bool back);

    // find: [start, end] of the next match, or [] when there's none
    Q_INVOKABLE QVariantList find(const QString &text, int from, bool matchCase, bool backward) const;
    Q_INVOKABLE void replaceRange(int start, int end, const QString &text);
    Q_INVOKABLE int replaceAll(const QString &text, const QString &with, bool matchCase);

    Q_INVOKABLE int wordCount() const;
    // a path relative to the document's folder, when it's inside it
    Q_INVOKABLE QString relativePath(const QString &path) const;

signals:
    void documentChanged();
    void cursorPositionChanged();
    void selectionStartChanged();
    void selectionEndChanged();
    void baseDirChanged();
    void formatChanged();
    void modifiedChanged();
    void hasHtmlChanged();
    // the window should put its cursor/selection here
    void selectRequested(int start, int end);

private:
    QTextDocument *doc() const;
    QTextCursor selectionCursor() const;
    QTextCharFormat formatAtCursor() const;
    void updateFormat();
    void editChars(int start, int end, const std::function<void(QTextCharFormat &)> &change);
    void forEachSelectedBlock(const std::function<void(QTextBlock &)> &f);
    void tidy(); // after reading Markdown: what the page shows that Qt's reader leaves out
    void tidyBlock(QTextBlock block);
    void onContentsChange(int position, int removed, int added);
    void onBlockCountChange(int count);
    QTextTable *currentTable() const;

    QPointer<QQuickTextDocument> m_quickDoc;
    int m_cursorPosition = 0;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    QString m_baseDir;
    int m_blockCount = 1;

    bool m_bold = false, m_italic = false, m_underline = false, m_strike = false, m_code = false;
    QString m_link;
    int m_blockStyle = 0;
    bool m_quote = false;
    int m_listStyle = 0;
    int m_listLevel = 0;
    bool m_inTable = false;
    bool m_hasHtml = false;
    QString m_loadedText; // the Markdown the page was last read from or saved as

    // formats toggled with nothing selected, for what's typed next at
    // m_pendingPos: per Flag, -1 unchanged, 0 off, 1 on
    enum Flag { FBold, FItalic, FUnderline, FStrike, FCode, FlagCount };
    int m_pending[FlagCount] = {-1, -1, -1, -1, -1};
    int m_pendingPos = -1;
    void clearPending();
    bool hasPending() const;
    void toggleFlag(Flag flag);
    static void applyFlag(QTextCharFormat &fmt, Flag flag, bool on);
    static bool flagOf(const QTextCharFormat &fmt, Flag flag);
    bool m_applying = false;
};
