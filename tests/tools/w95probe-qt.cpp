// w95probe-qt: the Qt counterpart of w95probe-gtk (same "EVT ..." lines and
// stdin commands), for the tests. Qt is what Wayland 95's own apps use.
//
//   EVT ready | press B X Y | release B X Y | scroll up|down | key NAME
//   EVT focus in|out | size W H | state maximized|... | menu ITEM
//   EVT clipboard TEXT | primary TEXT
// stdin: copy TEXT | select TEXT | paste | paste-primary
//
//   w95probe-qt [--title T] [--color RRGGBB]
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSocketNotifier>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
#include <cstdio>
#include <unistd.h>

static void out(const QString &line)
{
	std::printf("EVT %s\n", qPrintable(line));
	std::fflush(stdout);
}

class Probe : public QWidget
{
public:
	explicit Probe(const QColor &color) : m_color(color)
	{
		setFocusPolicy(Qt::StrongFocus);
		m_menu = new QMenu(this);
		for (const char *name : {"First", "Second", "Third", "Fourth", "Fifth", "Sixth"}) {
			QAction *a = m_menu->addAction(QString::fromLatin1(name));
			connect(a, &QAction::triggered, this, [name] { out(QStringLiteral("menu %1").arg(QLatin1String(name))); });
		}
	}

protected:
	void paintEvent(QPaintEvent *) override { QPainter(this).fillRect(rect(), m_color); }
	void mousePressEvent(QMouseEvent *e) override
	{
		out(QStringLiteral("press %1 %2 %3").arg(button(e)).arg(int(e->position().x())).arg(int(e->position().y())));
		if (e->button() == Qt::RightButton)
			m_menu->popup(e->globalPosition().toPoint());
	}
	void mouseReleaseEvent(QMouseEvent *e) override
	{
		out(QStringLiteral("release %1 %2 %3").arg(button(e)).arg(int(e->position().x())).arg(int(e->position().y())));
	}
	void wheelEvent(QWheelEvent *e) override { out(e->angleDelta().y() > 0 ? QStringLiteral("scroll up") : QStringLiteral("scroll down")); }
	void keyPressEvent(QKeyEvent *e) override
	{
		QString name = e->text().isEmpty() ? QKeySequence(e->key()).toString() : e->text();
		out(QStringLiteral("key %1").arg(name));
	}
	void focusInEvent(QFocusEvent *) override { out(QStringLiteral("focus in")); }
	void focusOutEvent(QFocusEvent *e) override
	{
		// a menu of our own taking the focus isn't the window losing it
		if (e->reason() != Qt::PopupFocusReason)
			out(QStringLiteral("focus out"));
	}
	void resizeEvent(QResizeEvent *e) override { out(QStringLiteral("size %1 %2").arg(e->size().width()).arg(e->size().height())); }
	void changeEvent(QEvent *e) override
	{
		if (e->type() == QEvent::WindowStateChange)
			out(QStringLiteral("state%1%2").arg(isMaximized() ? QStringLiteral(" maximized") : QString(),
			                                    isMinimized() ? QStringLiteral(" minimized") : QString()));
		QWidget::changeEvent(e);
	}

private:
	static int button(QMouseEvent *e)
	{
		return e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2 : e->button() == Qt::RightButton ? 3 : 0;
	}
	QColor m_color;
	QMenu *m_menu;
};

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QString title = QStringLiteral("w95probe-qt");
	QColor color(0x33, 0x99, 0x33);
	const QStringList args = app.arguments();
	for (int i = 1; i + 1 < args.size(); ++i) {
		if (args[i] == QLatin1String("--title")) title = args[++i];
		else if (args[i] == QLatin1String("--color")) color = QColor(QLatin1Char('#') + args[++i]);
	}
	Probe probe(color);
	probe.setWindowTitle(title);
	probe.resize(400, 300);
	probe.show();
	QTimer::singleShot(300, [] { out(QStringLiteral("ready")); });

	// commands on stdin
	auto *notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &app);
	QObject::connect(notifier, &QSocketNotifier::activated, [&app] {
		char buf[512];
		ssize_t n = read(STDIN_FILENO, buf, sizeof buf - 1);
		if (n <= 0) {
			app.quit();
			return;
		}
		buf[n] = 0;
		for (const QString &line : QString::fromUtf8(buf).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
			QClipboard *cb = QGuiApplication::clipboard();
			if (line.startsWith(QLatin1String("copy "))) {
				cb->setText(line.mid(5), QClipboard::Clipboard);
				out(QStringLiteral("copied"));
			} else if (line.startsWith(QLatin1String("select "))) {
				cb->setText(line.mid(7), QClipboard::Selection);
				out(QStringLiteral("selected"));
			} else if (line == QLatin1String("paste")) {
				QString t = cb->text(QClipboard::Clipboard);
				out(QStringLiteral("clipboard %1").arg(t.isEmpty() ? QStringLiteral("(empty)") : t));
			} else if (line == QLatin1String("paste-primary")) {
				QString t = cb->text(QClipboard::Selection);
				out(QStringLiteral("primary %1").arg(t.isEmpty() ? QStringLiteral("(empty)") : t));
			}
		}
	});
	return app.exec();
}
