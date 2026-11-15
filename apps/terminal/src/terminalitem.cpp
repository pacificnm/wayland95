#include "terminalitem.h"

#include <vector>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleHints>
#include <QSocketNotifier>
#include <QWheelEvent>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// The classic 16-color VGA text-mode palette, with "light gray" nudged to
// the Win95 system gray so the default text matches the rest of the UI.
const QRgb kVgaPalette[16] = {
    0x000000, 0xaa0000, 0x00aa00, 0xaa5500, 0x0000aa, 0xaa00aa, 0x00aaaa, 0xc0c0c0,
    0x555555, 0xff5555, 0x55ff55, 0xffff55, 0x5555ff, 0xff55ff, 0x55ffff, 0xffffff,
};

VTermColor rgbColor(QRgb rgb)
{
    VTermColor c;
    vterm_color_rgb(&c, qRed(rgb), qGreen(rgb), qBlue(rgb));
    return c;
}

bool sameColor(const VTermColor &a, const VTermColor &b)
{
    return vterm_color_is_equal(&a, &b);
}

} // namespace

TerminalItem::TerminalItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFlag(ItemHasContents, true);
    setFlag(ItemAcceptsInputMethod, false);
    setAcceptedMouseButtons(Qt::AllButtons);
    setOpaquePainting(true);
    setFillColor(m_defaultBg);

    m_font.setFamily(QStringLiteral("DejaVu Sans Mono"));
    m_font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    m_font.setFixedPitch(true);
    m_font.setPixelSize(14);
    m_font.setKerning(false);
    updateFontMetrics();

    // blink at the desktop's cursor rate (Control Panel > Keyboard)
    m_blinkTimer.setInterval(qMax(100, QGuiApplication::styleHints()->cursorFlashTime() / 2));
    connect(QGuiApplication::styleHints(), &QStyleHints::cursorFlashTimeChanged, this, [this](int ms) {
        m_blinkTimer.setInterval(qMax(100, ms / 2));
        resetBlink();
    });
    connect(&m_blinkTimer, &QTimer::timeout, this, [this] {
        m_blinkOn = !m_blinkOn;
        update();
    });
}

TerminalItem::~TerminalItem()
{
    if (m_ptyFd >= 0) {
        // Closing the master hangs up the shell's session (SIGHUP).
        ::close(m_ptyFd);
        m_ptyFd = -1;
    }
    if (m_childPid > 0) {
        ::kill(m_childPid, SIGHUP);
        ::waitpid(m_childPid, nullptr, WNOHANG);
    }
    if (m_vt)
        vterm_free(m_vt);
}

// ---------------------------------------------------------------------------
// setup

void TerminalItem::componentComplete()
{
    QQuickPaintedItem::componentComplete();

    if (width() > 0 && height() > 0) {
        m_cols = qMax(2, int((width() - 2 * kPadding) / m_cellW));
        m_rows = qMax(2, int((height() - 2 * kPadding) / m_cellH));
    }

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);
    vterm_output_set_callback(m_vt, &TerminalItem::cbOutput, this);

    VTermState *state = vterm_obtain_state(m_vt);
    for (int i = 0; i < 16; ++i) {
        VTermColor c = rgbColor(kVgaPalette[i]);
        vterm_state_set_palette_color(state, i, &c);
    }
    VTermColor fg = rgbColor(m_defaultFg.rgb());
    VTermColor bg = rgbColor(m_defaultBg.rgb());
    vterm_state_set_default_colors(state, &fg, &bg);
    vterm_state_set_bold_highbright(state, 1);

    m_screen = vterm_obtain_screen(m_vt);
    static VTermScreenCallbacks callbacks = [] {
        VTermScreenCallbacks cb;
        std::memset(&cb, 0, sizeof cb);
        cb.damage = &TerminalItem::cbDamage;
        cb.movecursor = &TerminalItem::cbMoveCursor;
        cb.settermprop = &TerminalItem::cbSetTermProp;
        cb.bell = &TerminalItem::cbBell;
        cb.sb_pushline = &TerminalItem::cbPushLine;
        cb.sb_popline = &TerminalItem::cbPopLine;
        cb.sb_clear = &TerminalItem::cbClearScrollback;
        return cb;
    }();
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_reset(m_screen, 1);
    // reset() reports a block cursor; start with the DOS-style underscore
    // instead (programs can still change it with DECSCUSR).
    m_cursorShape = VTERM_PROP_CURSORSHAPE_UNDERLINE;

    emit gridSizeChanged();
    startShell();
}

void TerminalItem::startShell()
{
    struct winsize ws{};
    ws.ws_row = m_rows;
    ws.ws_col = m_cols;
    ws.ws_xpixel = int(width());
    ws.ws_ypixel = int(height());

    // resolved before fork(): the child should only make async-signal-safe calls
    const QByteArray workdir = QFile::encodeName(m_workingDirectory);
    QList<QByteArray> commandArgs;
    for (const QString &a : std::as_const(m_command))
        commandArgs << a.toLocal8Bit();
    std::vector<char *> commandArgv;
    for (QByteArray &a : commandArgs)
        commandArgv.push_back(a.data());
    commandArgv.push_back(nullptr);

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        const QByteArray msg = QByteArray("win95terminal: forkpty failed: ") + std::strerror(errno);
        vterm_input_write(m_vt, msg.constData(), msg.size());
        return;
    }

    if (pid == 0) {
        // child: becomes the shell. Undo anything Qt changed in our signal
        // state, since exec() keeps the mask and ignored dispositions.
        sigset_t all;
        sigemptyset(&all);
        sigprocmask(SIG_SETMASK, &all, nullptr);
        for (int sig : {SIGCHLD, SIGPIPE, SIGINT, SIGQUIT, SIGTERM, SIGHUP})
            std::signal(sig, SIG_DFL);

        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "win95terminal", 1);

        const char *shell = std::getenv("SHELL");
        if (!shell || !*shell) {
            struct passwd *pw = getpwuid(getuid());
            shell = (pw && pw->pw_shell && *pw->pw_shell) ? pw->pw_shell : "/bin/sh";
        }
        if (!workdir.isEmpty() && chdir(workdir.constData()) == 0) {
            setenv("PWD", workdir.constData(), 1);
        } else if (const char *home = std::getenv("HOME")) {
            if (chdir(home) != 0) { /* stay where we are */ }
        }

        if (commandArgv.size() > 1) {
            execvp(commandArgv[0], commandArgv.data());
            std::fprintf(stderr, "win95terminal: could not run %s: %s\n", commandArgv[0], std::strerror(errno));
            _exit(127);
        }
        execl(shell, shell, static_cast<char *>(nullptr));
        std::fprintf(stderr, "win95terminal: could not run %s: %s\n", shell, std::strerror(errno));
        _exit(127);
    }

    m_childPid = pid;
    m_ptyFd = master;
    ::fcntl(m_ptyFd, F_SETFL, ::fcntl(m_ptyFd, F_GETFL) | O_NONBLOCK);
    ::fcntl(m_ptyFd, F_SETFD, FD_CLOEXEC);

    m_readNotifier = new QSocketNotifier(m_ptyFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &TerminalItem::onPtyReadable);
    m_writeNotifier = new QSocketNotifier(m_ptyFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &TerminalItem::onPtyWritable);
}

// ---------------------------------------------------------------------------
// pty I/O

void TerminalItem::onPtyReadable()
{
    char buf[65536];
    bool gotData = false;
    for (;;) {
        const ssize_t n = ::read(m_ptyFd, buf, sizeof buf);
        if (n > 0) {
            vterm_input_write(m_vt, buf, size_t(n));
            gotData = true;
            continue;
        }
        if (n < 0 && (errno == EINTR))
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        // n == 0, or EIO: the shell (and everything on the pty) is gone.
        childExited();
        break;
    }
    if (gotData) {
        vterm_screen_flush_damage(m_screen);
        update();
    }
}

void TerminalItem::writeToPty(const char *data, qsizetype len)
{
    if (m_ptyFd < 0 || len <= 0)
        return;
    m_pendingWrite.append(data, len);
    onPtyWritable();
}

void TerminalItem::onPtyWritable()
{
    while (!m_pendingWrite.isEmpty()) {
        const ssize_t n = ::write(m_ptyFd, m_pendingWrite.constData(), size_t(m_pendingWrite.size()));
        if (n > 0) {
            m_pendingWrite.remove(0, n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break; // EAGAIN (pty buffer full) or an error; retry when writable
        }
    }
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(!m_pendingWrite.isEmpty());
}

void TerminalItem::childExited()
{
    int exitCode = 0;
    if (m_childPid > 0) {
        int status = 0;
        if (::waitpid(m_childPid, &status, 0) == m_childPid)
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        m_childPid = -1;
    }
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
    ::close(m_ptyFd);
    m_ptyFd = -1;
    emit finished(exitCode);
}

// ---------------------------------------------------------------------------
// libvterm callbacks

int TerminalItem::cbDamage(VTermRect, void *user)
{
    static_cast<TerminalItem *>(user)->update();
    return 1;
}

int TerminalItem::cbMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *self = static_cast<TerminalItem *>(user);
    self->m_cursor = pos;
    self->m_cursorVisible = visible;
    self->update();
    return 1;
}

int TerminalItem::cbSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    auto *self = static_cast<TerminalItem *>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = val->boolean;
        break;
    case VTERM_PROP_CURSORBLINK:
        self->m_cursorBlinkEnabled = val->boolean;
        self->resetBlink();
        break;
    case VTERM_PROP_CURSORSHAPE:
        self->m_cursorShape = val->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        self->m_altScreen = val->boolean;
        self->setScrollOffset(0);
        break;
    case VTERM_PROP_REVERSE:
        self->m_reverseVideo = val->boolean;
        break;
    case VTERM_PROP_MOUSE:
        self->m_mouseMode = val->number;
        break;
    case VTERM_PROP_TITLE:
        // Titles can arrive split over several fragments.
        if (val->string.initial)
            self->m_titleBuf.clear();
        self->m_titleBuf.append(val->string.str, qsizetype(val->string.len));
        if (val->string.final) {
            self->m_title = QString::fromUtf8(self->m_titleBuf);
            emit self->titleChanged();
        }
        break;
    default:
        break;
    }
    self->update();
    return 1;
}

int TerminalItem::cbBell(void *user)
{
    emit static_cast<TerminalItem *>(user)->bell();
    return 1;
}

int TerminalItem::cbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalItem *>(user);
    QVector<VTermScreenCell> line(cells, cells + cols);
    // trim trailing blank cells to keep the buffer small
    while (!line.isEmpty() && line.last().chars[0] == 0
           && VTERM_COLOR_IS_DEFAULT_BG(&line.last().bg) && !line.last().attrs.reverse)
        line.removeLast();
    self->m_scrollback.append(std::move(line));
    if (self->m_scrollback.size() > kMaxScrollback) {
        self->m_scrollback.removeFirst();
        ++self->m_droppedLines;
    }
    // keep the view still if the user has scrolled back
    if (self->m_scrollOffset > 0)
        self->m_scrollOffset = qMin(self->m_scrollOffset + 1, int(self->m_scrollback.size()));
    return 1;
}

int TerminalItem::cbPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalItem *>(user);
    if (self->m_scrollback.isEmpty())
        return 0;
    const QVector<VTermScreenCell> line = self->m_scrollback.takeLast();
    VTermScreenCell blank;
    self->fetchCell(-1, 0, &blank); // line -1 always yields a blank cell
    for (int c = 0; c < cols; ++c)
        cells[c] = c < line.size() ? line.at(c) : blank;
    self->m_scrollOffset = qMin(self->m_scrollOffset, int(self->m_scrollback.size()));
    return 1;
}

int TerminalItem::cbClearScrollback(void *user)
{
    auto *self = static_cast<TerminalItem *>(user);
    self->m_droppedLines += int(self->m_scrollback.size());
    self->m_scrollback.clear();
    self->m_scrollOffset = 0;
    self->clearSelection();
    return 1;
}

void TerminalItem::cbOutput(const char *s, size_t len, void *user)
{
    static_cast<TerminalItem *>(user)->writeToPty(s, qsizetype(len));
}

// ---------------------------------------------------------------------------
// fonts & geometry

void TerminalItem::setFontFamily(const QString &family)
{
    if (family == m_font.family())
        return;
    m_font.setFamily(family);
    updateFontMetrics();
}

void TerminalItem::setFontPixelSize(int px)
{
    px = qBound(6, px, 72);
    if (px == m_font.pixelSize())
        return;
    m_font.setPixelSize(px);
    updateFontMetrics();
}

void TerminalItem::updateFontMetrics()
{
    const QFontMetricsF fm(m_font);
    m_cellW = fm.horizontalAdvance(QLatin1Char('M'));
    m_cellH = qCeil(fm.height());
    m_ascent = fm.ascent();
    emit fontChanged();
    updateGridSize();
    update();
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        updateGridSize();
}

void TerminalItem::updateGridSize()
{
    if (!m_vt || width() <= 0 || height() <= 0)
        return;
    const int cols = qMax(2, int((width() - 2 * kPadding) / m_cellW));
    const int rows = qMax(2, int((height() - 2 * kPadding) / m_cellH));
    if (cols == m_cols && rows == m_rows)
        return;

    m_cols = cols;
    m_rows = rows;
    vterm_set_size(m_vt, m_rows, m_cols);
    vterm_screen_flush_damage(m_screen);
    if (m_ptyFd >= 0) {
        struct winsize ws{};
        ws.ws_row = m_rows;
        ws.ws_col = m_cols;
        ws.ws_xpixel = int(width());
        ws.ws_ypixel = int(height());
        ::ioctl(m_ptyFd, TIOCSWINSZ, &ws); // kernel sends SIGWINCH to the job
    }
    setScrollOffset(qMin(m_scrollOffset, int(m_scrollback.size())));
    emit gridSizeChanged();
    update();
}

// ---------------------------------------------------------------------------
// painting

void TerminalItem::fetchCell(int line, int col, VTermScreenCell *cell) const
{
    const int sb = int(m_scrollback.size());
    if (line >= sb && line < sb + m_rows && m_screen) {
        vterm_screen_get_cell(m_screen, VTermPos{line - sb, col}, cell);
        return;
    }
    if (line >= 0 && line < sb && col < m_scrollback.at(line).size()) {
        *cell = m_scrollback.at(line).at(col);
        return;
    }
    std::memset(cell, 0, sizeof *cell);
    cell->width = 1;
    cell->fg.type = VTERM_COLOR_RGB | VTERM_COLOR_DEFAULT_FG;
    cell->bg.type = VTERM_COLOR_RGB | VTERM_COLOR_DEFAULT_BG;
}

QColor TerminalItem::resolveColor(VTermColor color) const
{
    if (VTERM_COLOR_IS_DEFAULT_FG(&color))
        return m_defaultFg;
    if (VTERM_COLOR_IS_DEFAULT_BG(&color))
        return m_defaultBg;
    vterm_screen_convert_color_to_rgb(m_screen, &color);
    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}

void TerminalItem::paint(QPainter *p)
{
    const QColor screenBg = m_reverseVideo ? m_defaultFg : m_defaultBg;
    p->fillRect(QRectF(0, 0, width(), height()), screenBg);
    if (!m_screen)
        return;

    QFont boldItalicFont = m_font;
    const int top = topLine();
    const int cursorLine = int(m_scrollback.size()) + m_cursor.row;

    struct Resolved {
        QColor fg, bg;
    };
    auto resolve = [&](const VTermScreenCell &cell, int absLine, int col) {
        Resolved r{resolveColor(cell.fg), resolveColor(cell.bg)};
        if (VTERM_COLOR_IS_DEFAULT_BG(&cell.bg) && m_reverseVideo)
            r.bg = m_defaultFg;
        if (VTERM_COLOR_IS_DEFAULT_FG(&cell.fg) && m_reverseVideo)
            r.fg = m_defaultBg;
        if (cell.attrs.reverse)
            std::swap(r.fg, r.bg);
        if (isSelected(absLine, col)) {
            r.bg = QColor(0x00, 0x00, 0x80); // Win95 selection navy
            r.fg = Qt::white;
        }
        return r;
    };

    VTermScreenCell cell;
    for (int row = 0; row < m_rows; ++row) {
        const int line = top + row;
        const int absLine = m_droppedLines + line;
        const qreal y = kPadding + row * m_cellH;

        // pass 1: backgrounds
        for (int col = 0; col < m_cols;) {
            fetchCell(line, col, &cell);
            const int w = qMax(1, int(cell.width));
            const Resolved r = resolve(cell, absLine, col);
            if (r.bg != screenBg)
                p->fillRect(QRectF(kPadding + col * m_cellW, y, w * m_cellW, m_cellH), r.bg);
            col += w;
        }

        // pass 2: text, batching runs of plain ASCII with identical style so
        // we don't issue one drawText per cell
        QString run;
        int runStart = 0;
        QColor runColor;
        bool runBold = false, runItalic = false;
        int runUnderline = 0;
        bool runStrike = false;

        auto flush = [&]() {
            if (run.isEmpty())
                return;
            QFont f = m_font;
            f.setBold(runBold);
            f.setItalic(runItalic);
            p->setFont(f);
            p->setPen(runColor);
            const qreal x = kPadding + runStart * m_cellW;
            p->drawText(QPointF(x, y + m_ascent), run);
            const qreal runW = run.size() * m_cellW;
            if (runUnderline)
                p->fillRect(QRectF(x, y + m_ascent + 1, runW, 1), runColor);
            if (runStrike)
                p->fillRect(QRectF(x, y + m_cellH / 2, runW, 1), runColor);
            run.clear();
        };

        for (int col = 0; col < m_cols;) {
            fetchCell(line, col, &cell);
            const int w = qMax(1, int(cell.width));
            const Resolved r = resolve(cell, absLine, col);
            const uint32_t ch = cell.chars[0];
            const bool blank = ch == 0 || ch == ' ' || cell.attrs.conceal;

            const bool ascii = ch >= 0x21 && ch < 0x7f && cell.chars[1] == 0 && w == 1;
            const bool sameStyle = !run.isEmpty() && r.fg == runColor && cell.attrs.bold == runBold
                && cell.attrs.italic == runItalic && int(cell.attrs.underline) == runUnderline
                && cell.attrs.strike == runStrike && runStart + run.size() == col;

            if (blank && !cell.attrs.underline && !cell.attrs.strike) {
                flush();
            } else if (ascii || blank) {
                if (!sameStyle) {
                    flush();
                    runStart = col;
                    runColor = r.fg;
                    runBold = cell.attrs.bold;
                    runItalic = cell.attrs.italic;
                    runUnderline = cell.attrs.underline;
                    runStrike = cell.attrs.strike;
                }
                run.append(blank ? QChar(' ') : QChar(char16_t(ch)));
            } else {
                // non-ASCII, combining or double-width: draw on its own so a
                // fallback font's glyph width can't push later cells out of line
                flush();
                int n = 0;
                while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n])
                    ++n;
                const QString text = QString::fromUcs4(reinterpret_cast<const char32_t *>(cell.chars), n);
                boldItalicFont = m_font;
                boldItalicFont.setBold(cell.attrs.bold);
                boldItalicFont.setItalic(cell.attrs.italic);
                p->setFont(boldItalicFont);
                p->setPen(r.fg);
                p->drawText(QRectF(kPadding + col * m_cellW, y, w * m_cellW, m_cellH),
                            Qt::AlignCenter | Qt::TextDontClip, text);
            }
            col += w;
        }
        flush();

        // cursor
        if (line == cursorLine && m_cursorVisible && m_scrollOffset <= m_rows) {
            const bool focused = hasActiveFocus();
            if (!focused || !m_cursorBlinkEnabled || m_blinkOn) {
                const QRectF cellRect(kPadding + m_cursor.col * m_cellW, y, m_cellW, m_cellH);
                p->setPen(Qt::NoPen);
                if (!focused) {
                    p->setPen(m_defaultFg);
                    p->setBrush(Qt::NoBrush);
                    p->drawRect(cellRect.adjusted(0.5, 0.5, -0.5, -0.5));
                } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
                    p->fillRect(QRectF(cellRect.left(), cellRect.bottom() - 2, m_cellW, 2), m_defaultFg);
                } else if (m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT) {
                    p->fillRect(QRectF(cellRect.left(), cellRect.top(), 2, m_cellH), m_defaultFg);
                } else {
                    // block: invert the cell under it
                    p->fillRect(cellRect, m_defaultFg);
                    fetchCell(line, m_cursor.col, &cell);
                    if (cell.chars[0] && cell.chars[0] != ' ') {
                        int n = 0;
                        while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n])
                            ++n;
                        p->setFont(m_font);
                        p->setPen(m_defaultBg);
                        p->drawText(cellRect, Qt::AlignCenter | Qt::TextDontClip,
                                    QString::fromUcs4(reinterpret_cast<const char32_t *>(cell.chars), n));
                    }
                }
            }
        }
    }

    // scrollback indicator, like a tiny scrollbar thumb on the right edge
    if (m_scrollOffset > 0 && lineCount() > m_rows) {
        const qreal trackH = height() - 2 * kPadding;
        const qreal thumbH = qMax<qreal>(12, trackH * m_rows / lineCount());
        const qreal pos = qreal(top) / (lineCount() - m_rows);
        p->fillRect(QRectF(width() - 4, kPadding + pos * (trackH - thumbH), 3, thumbH),
                    QColor(0x80, 0x80, 0x80));
    }
}

// ---------------------------------------------------------------------------
// scrolling, blinking, selection

void TerminalItem::setScrollOffset(int offset)
{
    offset = qBound(0, offset, int(m_scrollback.size()));
    if (offset == m_scrollOffset)
        return;
    m_scrollOffset = offset;
    update();
}

void TerminalItem::resetBlink()
{
    m_blinkOn = true;
    // a flash time of 0 means "don't blink"
    if (hasActiveFocus() && m_cursorBlinkEnabled && QGuiApplication::styleHints()->cursorFlashTime() > 0)
        m_blinkTimer.start();
    else
        m_blinkTimer.stop();
    update();
}

void TerminalItem::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    resetBlink();
    if (m_vt)
        vterm_state_focus_in(vterm_obtain_state(m_vt));
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    resetBlink();
    if (m_vt)
        vterm_state_focus_out(vterm_obtain_state(m_vt));
}

QPoint TerminalItem::cellAt(const QPointF &pos, bool clampToGrid) const
{
    int col = int((pos.x() - kPadding) / m_cellW);
    int row = int(std::floor((pos.y() - kPadding) / m_cellH));
    if (clampToGrid) {
        col = qBound(0, col, m_cols - 1);
        row = qBound(0, row, m_rows - 1);
    }
    return QPoint(col, m_droppedLines + topLine() + row);
}

bool TerminalItem::isSelected(int absLine, int col) const
{
    if (!m_hasSelection)
        return false;
    QPoint a = m_selAnchor, b = m_selEnd;
    if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x()))
        std::swap(a, b);
    if (absLine < a.y() || absLine > b.y())
        return false;
    if (absLine == a.y() && col < a.x())
        return false;
    if (absLine == b.y() && col > b.x())
        return false;
    return true;
}

QString TerminalItem::lineText(int line, int fromCol, int toCol) const
{
    QString out;
    VTermScreenCell cell;
    for (int col = fromCol; col <= toCol && col < m_cols;) {
        fetchCell(line, col, &cell);
        if (cell.chars[0] == 0) {
            out.append(QLatin1Char(' '));
        } else {
            int n = 0;
            while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n])
                ++n;
            out.append(QString::fromUcs4(reinterpret_cast<const char32_t *>(cell.chars), n));
        }
        col += qMax(1, int(cell.width));
    }
    // drop trailing padding
    qsizetype end = out.size();
    while (end > 0 && out.at(end - 1) == QLatin1Char(' '))
        --end;
    out.truncate(end);
    return out;
}

QString TerminalItem::selectedText() const
{
    if (!m_hasSelection)
        return {};
    QPoint a = m_selAnchor, b = m_selEnd;
    if (b.y() < a.y() || (b.y() == a.y() && b.x() < a.x()))
        std::swap(a, b);
    QStringList lines;
    for (int abs = a.y(); abs <= b.y(); ++abs) {
        const int line = abs - m_droppedLines;
        if (line < 0 || line >= lineCount())
            continue;
        const int from = abs == a.y() ? a.x() : 0;
        const int to = abs == b.y() ? b.x() : m_cols - 1;
        lines << lineText(line, from, to);
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalItem::clearSelection()
{
    if (!m_hasSelection && !m_selecting)
        return;
    m_hasSelection = false;
    m_selecting = false;
    emit selectionChanged();
    update();
}

void TerminalItem::selectAll()
{
    m_selAnchor = QPoint(0, m_droppedLines);
    m_selEnd = QPoint(m_cols - 1, m_droppedLines + lineCount() - 1);
    m_hasSelection = true;
    emit selectionChanged();
    update();
}

void TerminalItem::copy()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void TerminalItem::paste()
{
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty() || !m_vt)
        return;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    const QByteArray bytes = text.toUtf8();
    setScrollOffset(0);
    vterm_keyboard_start_paste(m_vt); // no-op unless the app enabled bracketed paste
    writeToPty(bytes.constData(), bytes.size());
    vterm_keyboard_end_paste(m_vt);
}

// ---------------------------------------------------------------------------
// keyboard

VTermModifier TerminalItem::vtermMods(Qt::KeyboardModifiers mods) const
{
    int m = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier)
        m |= VTERM_MOD_SHIFT;
    if (mods & Qt::ControlModifier)
        m |= VTERM_MOD_CTRL;
    if (mods & Qt::AltModifier)
        m |= VTERM_MOD_ALT;
    return VTermModifier(m);
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    if (!m_vt || m_ptyFd < 0) {
        event->ignore();
        return;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int key = event->key();
    const VTermModifier vmod = vtermMods(mods);
    event->accept();

    // terminal-local shortcuts
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
        if (key == Qt::Key_C) { copy(); return; }
        if (key == Qt::Key_V) { paste(); return; }
    }
    if ((mods & Qt::ShiftModifier) && !(mods & Qt::ControlModifier) && !m_altScreen) {
        if (key == Qt::Key_PageUp) { setScrollOffset(m_scrollOffset + m_rows / 2); return; }
        if (key == Qt::Key_PageDown) { setScrollOffset(m_scrollOffset - m_rows / 2); return; }
    }
    if ((mods & Qt::ShiftModifier) && key == Qt::Key_Insert) { paste(); return; }

    // anything that sends input snaps the view back to the live screen
    auto typed = [this] {
        setScrollOffset(0);
        resetBlink();
    };

    VTermKey vkey = VTERM_KEY_NONE;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:     vkey = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:   vkey = VTERM_KEY_TAB; break;
    case Qt::Key_Backspace: vkey = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape:    vkey = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up:        vkey = VTERM_KEY_UP; break;
    case Qt::Key_Down:      vkey = VTERM_KEY_DOWN; break;
    case Qt::Key_Left:      vkey = VTERM_KEY_LEFT; break;
    case Qt::Key_Right:     vkey = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert:    vkey = VTERM_KEY_INS; break;
    case Qt::Key_Delete:    vkey = VTERM_KEY_DEL; break;
    case Qt::Key_Home:      vkey = VTERM_KEY_HOME; break;
    case Qt::Key_End:       vkey = VTERM_KEY_END; break;
    case Qt::Key_PageUp:    vkey = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown:  vkey = VTERM_KEY_PAGEDOWN; break;
    default:
        if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
            vkey = VTermKey(VTERM_KEY_FUNCTION(1 + key - Qt::Key_F1));
        break;
    }
    if (vkey != VTERM_KEY_NONE) {
        typed();
        VTermModifier m = vmod;
        if (key == Qt::Key_Backtab)
            m = VTermModifier(m | VTERM_MOD_SHIFT);
        vterm_keyboard_key(m_vt, vkey, m);
        return;
    }

    // Ctrl+<key> -> C0 control byte, the way xterm does it. (libvterm would
    // turn Ctrl+I/J/M/[ into CSI-u sequences that most shells don't read.)
    if (mods & Qt::ControlModifier) {
        int c = -1;
        if (key >= Qt::Key_A && key <= Qt::Key_Z)
            c = key - Qt::Key_A + 1;
        else if (key == Qt::Key_At || key == Qt::Key_Space || key == Qt::Key_2)
            c = 0;
        else if (key == Qt::Key_BracketLeft || key == Qt::Key_3)
            c = 0x1b;
        else if (key == Qt::Key_Backslash || key == Qt::Key_4)
            c = 0x1c;
        else if (key == Qt::Key_BracketRight || key == Qt::Key_5)
            c = 0x1d;
        else if (key == Qt::Key_AsciiCircum || key == Qt::Key_6)
            c = 0x1e;
        else if (key == Qt::Key_Underscore || key == Qt::Key_Minus || key == Qt::Key_7)
            c = 0x1f;
        else if (key == Qt::Key_Question || key == Qt::Key_8)
            c = 0x7f;
        if (c >= 0) {
            typed();
            char bytes[2];
            int n = 0;
            if (mods & Qt::AltModifier)
                bytes[n++] = 0x1b;
            bytes[n++] = char(c);
            writeToPty(bytes, n);
            return;
        }
    }

    const QString text = event->text();
    bool sent = false;
    for (char32_t c : text.toUcs4()) {
        if (c < 0x20 || c == 0x7f)
            continue; // control chars were handled above
        vterm_keyboard_unichar(m_vt, c, VTermModifier(vmod & VTERM_MOD_ALT));
        sent = true;
    }
    if (sent)
        typed();
    else
        event->ignore();
}

// ---------------------------------------------------------------------------
// mouse

bool TerminalItem::forwardMouseToApp(Qt::KeyboardModifiers mods) const
{
    // Shift always gives you local selection, as in xterm.
    return m_mouseMode != VTERM_PROP_MOUSE_NONE && !(mods & Qt::ShiftModifier) && m_scrollOffset == 0;
}

static int vtermButton(Qt::MouseButton b)
{
    switch (b) {
    case Qt::LeftButton:   return 1;
    case Qt::MiddleButton: return 2;
    case Qt::RightButton:  return 3;
    default:               return 0;
    }
}

void TerminalItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    event->accept();

    if (forwardMouseToApp(event->modifiers())) {
        const QPoint c = cellAt(event->position());
        vterm_mouse_move(m_vt, c.y() - m_droppedLines - topLine(), c.x(), vtermMods(event->modifiers()));
        if (int b = vtermButton(event->button()))
            vterm_mouse_button(m_vt, b, true, vtermMods(event->modifiers()));
        return;
    }

    if (event->button() == Qt::LeftButton) {
        clearSelection();
        m_selAnchor = m_selEnd = cellAt(event->position());
        m_selecting = true;
    } else if (event->button() == Qt::MiddleButton) {
        paste();
    }
}

void TerminalItem::mouseMoveEvent(QMouseEvent *event)
{
    if (forwardMouseToApp(event->modifiers())) {
        const QPoint c = cellAt(event->position());
        vterm_mouse_move(m_vt, c.y() - m_droppedLines - topLine(), c.x(), vtermMods(event->modifiers()));
        return;
    }
    if (!m_selecting)
        return;

    // dragging past the top/bottom edge scrolls through the history
    if (event->position().y() < 0)
        setScrollOffset(m_scrollOffset + 1);
    else if (event->position().y() > height())
        setScrollOffset(m_scrollOffset - 1);

    const QPoint c = cellAt(event->position());
    if (c != m_selEnd || !m_hasSelection) {
        m_selEnd = c;
        const bool had = m_hasSelection;
        m_hasSelection = m_selEnd != m_selAnchor;
        if (had != m_hasSelection)
            emit selectionChanged();
        update();
    }
}

void TerminalItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (forwardMouseToApp(event->modifiers())) {
        if (int b = vtermButton(event->button()))
            vterm_mouse_button(m_vt, b, false, vtermMods(event->modifiers()));
        return;
    }
    m_selecting = false;
}

void TerminalItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (forwardMouseToApp(event->modifiers()) || event->button() != Qt::LeftButton) {
        mousePressEvent(event);
        return;
    }
    // select the word under the pointer
    const QPoint c = cellAt(event->position());
    const int line = c.y() - m_droppedLines;
    auto isWordChar = [&](int col) {
        VTermScreenCell cell;
        fetchCell(line, col, &cell);
        const uint32_t ch = cell.chars[0];
        return ch && ch != ' ' && !std::strchr("\"'`()[]{}<>|;,", int(ch < 128 ? ch : 'a'));
    };
    if (!isWordChar(c.x()))
        return;
    int from = c.x(), to = c.x();
    while (from > 0 && isWordChar(from - 1))
        --from;
    while (to < m_cols - 1 && isWordChar(to + 1))
        ++to;
    m_selAnchor = QPoint(from, c.y());
    m_selEnd = QPoint(to, c.y());
    m_hasSelection = true;
    m_selecting = false;
    emit selectionChanged();
    update();
}

void TerminalItem::wheelEvent(QWheelEvent *event)
{
    event->accept();
    const int steps = event->angleDelta().y() / 40; // ~3 lines per notch
    if (steps == 0)
        return;

    if (forwardMouseToApp(event->modifiers())) {
        const QPoint c = cellAt(event->position());
        vterm_mouse_move(m_vt, c.y() - m_droppedLines - topLine(), c.x(), vtermMods(event->modifiers()));
        const int button = steps > 0 ? 4 : 5;
        for (int i = 0; i < qAbs(steps); ++i) {
            vterm_mouse_button(m_vt, button, true, VTERM_MOD_NONE);
            vterm_mouse_button(m_vt, button, false, VTERM_MOD_NONE);
        }
        return;
    }
    if (m_altScreen) {
        // full-screen apps without mouse support (less, man): send arrows
        for (int i = 0; i < qAbs(steps); ++i)
            vterm_keyboard_key(m_vt, steps > 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
        return;
    }
    setScrollOffset(m_scrollOffset + steps);
}
