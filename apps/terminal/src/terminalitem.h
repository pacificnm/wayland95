#pragma once

#include <QStringList>

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

extern "C" {
#include <vterm.h>
}

class QSocketNotifier;

// A terminal emulator as a single Qt Quick item.
//
// libvterm does all the escape-sequence parsing and keeps the screen grid;
// this item owns the pty + shell process, feeds bytes between the two,
// keeps a scrollback buffer, and paints the cell grid with QPainter.
//
// The shell is started when the item is completed, and `finished` is
// emitted when it exits.
class TerminalItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontChanged)
    Q_PROPERTY(qreal cellWidth READ cellWidth NOTIFY fontChanged)
    Q_PROPERTY(qreal cellHeight READ cellHeight NOTIFY fontChanged)
    Q_PROPERTY(int padding READ padding CONSTANT)
    Q_PROPERTY(int columns READ columns NOTIFY gridSizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY gridSizeChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // where the shell starts; empty means $HOME. Only read at startup.
    Q_PROPERTY(QString workingDirectory MEMBER m_workingDirectory NOTIFY workingDirectoryChanged)
    // a program to run instead of the shell (win95terminal -e ...)
    Q_PROPERTY(QStringList command MEMBER m_command NOTIFY commandChanged)

public:
    explicit TerminalItem(QQuickItem *parent = nullptr);
    ~TerminalItem() override;

    QString title() const { return m_title; }
    QString fontFamily() const { return m_font.family(); }
    void setFontFamily(const QString &family);
    int fontPixelSize() const { return m_font.pixelSize(); }
    void setFontPixelSize(int px);
    qreal cellWidth() const { return m_cellW; }
    qreal cellHeight() const { return m_cellH; }
    int padding() const { return kPadding; }
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }
    bool hasSelection() const { return m_hasSelection; }

    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();

    void paint(QPainter *painter) override;

signals:
    void titleChanged();
    void fontChanged();
    void gridSizeChanged();
    void selectionChanged();
    void bell();
    void finished(int exitCode);
    void workingDirectoryChanged();
    void commandChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    static constexpr int kPadding = 3;
    static constexpr int kMaxScrollback = 5000;

    // libvterm callbacks (trampolines to the member functions below)
    static int cbDamage(VTermRect rect, void *user);
    static int cbMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int cbSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int cbBell(void *user);
    static int cbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int cbPopLine(int cols, VTermScreenCell *cells, void *user);
    static int cbClearScrollback(void *user);
    static void cbOutput(const char *s, size_t len, void *user);

    void startShell();
    void onPtyReadable();
    void onPtyWritable();
    void writeToPty(const char *data, qsizetype len);
    void childExited();

    void updateFontMetrics();
    void updateGridSize();
    void setScrollOffset(int offset);
    void resetBlink();

    // Lines are indexed 0..scrollback+rows-1: scrollback first, then the
    // live screen. "Absolute" lines add m_droppedLines so a selection stays
    // put while output scrolls by.
    int lineCount() const { return int(m_scrollback.size()) + m_rows; }
    int topLine() const { return int(m_scrollback.size()) - m_scrollOffset; }
    void fetchCell(int line, int col, VTermScreenCell *cell) const;
    QColor resolveColor(VTermColor color) const;
    QPoint cellAt(const QPointF &pos, bool clampToGrid = true) const; // (col, absLine)
    bool isSelected(int absLine, int col) const;
    QString lineText(int line, int fromCol, int toCol) const;
    QString selectedText() const;
    void clearSelection();
    VTermModifier vtermMods(Qt::KeyboardModifiers mods) const;
    bool forwardMouseToApp(Qt::KeyboardModifiers mods) const;

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    int m_rows = 25;
    int m_cols = 80;

    int m_ptyFd = -1;
    int m_childPid = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QByteArray m_pendingWrite;

    QFont m_font;
    qreal m_cellW = 8;
    qreal m_cellH = 16;
    qreal m_ascent = 12;
    QColor m_defaultFg = QColor(0xc0, 0xc0, 0xc0);
    QColor m_defaultBg = QColor(0, 0, 0);

    QVector<QVector<VTermScreenCell>> m_scrollback;
    int m_droppedLines = 0;
    int m_scrollOffset = 0;

    VTermPos m_cursor{0, 0};
    bool m_cursorVisible = true;
    bool m_cursorBlinkEnabled = true;
    bool m_blinkOn = true;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_UNDERLINE; // DOS-style underscore
    QTimer m_blinkTimer;

    int m_mouseMode = 0; // VTERM_PROP_MOUSE_*
    bool m_altScreen = false;
    bool m_reverseVideo = false;

    QString m_workingDirectory;
    QStringList m_command;
    QByteArray m_titleBuf;
    QString m_title;

    bool m_selecting = false;
    bool m_hasSelection = false;
    QPoint m_selAnchor; // (col, absLine)
    QPoint m_selEnd;
};
