#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QtQmlIntegration/qqmlintegration.h>

// The code window's colours: JavaScript keywords, strings, comments and
// numbers, and the names the code can reach (form, App, Db, the form's
// elements) in a colour of their own. On a TextEdit's textDocument:
//
//   CodeColors { document: editor.textDocument; names: ["nameBox", ...]; dark: false }
class CodeHighlighter;

class CodeColors : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QStringList names READ names WRITE setNames NOTIFY namesChanged)
    // a dark background (a colour scheme's): lighter colours
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)

public:
    explicit CodeColors(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QStringList names() const { return m_names; }
    void setNames(const QStringList &names);
    bool dark() const { return m_dark; }
    void setDark(bool dark);

signals:
    void documentChanged();
    void namesChanged();
    void darkChanged();

private:
    QPointer<QQuickTextDocument> m_document;
    CodeHighlighter *m_highlighter = nullptr;
    QStringList m_names;
    bool m_dark = false;
};

class CodeHighlighter : public QSyntaxHighlighter
{
public:
    explicit CodeHighlighter(QTextDocument *document);
    void setNames(const QStringList &names);
    void setDark(bool dark);

protected:
    void highlightBlock(const QString &text) override;

private:
    enum State { Code = 0, BlockComment = 1, Template = 2 };
    void makeFormats();

    QStringList m_names;
    bool m_dark = false;
    QTextCharFormat m_keyword, m_literal, m_string, m_comment, m_number, m_name;
};
