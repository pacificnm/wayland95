// win95notify: send a desktop notification.
//
//   win95notify "Backup finished" "All 1,204 files were copied."
//   win95notify -u critical -i dialog-warning "Disk almost full"
//   win95notify -A open=Open -A later="Remind me" --wait "Update ready"
//       (prints the chosen action's key, or "closed")

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>

#include <cstdio>

namespace {

const QString Service = QStringLiteral("org.freedesktop.Notifications");
const QString Path = QStringLiteral("/org/freedesktop/Notifications");

// receives the server's signals for --wait
class Waiter : public QObject
{
    Q_OBJECT
public:
    uint id = 0;
    bool answered = false;
public slots:
    void actionInvoked(uint which, const QString &key)
    {
        if (which != id || answered)
            return;
        answered = true;
        std::printf("%s\n", qPrintable(key));
        std::fflush(stdout);
        QCoreApplication::exit(0);
    }
    void closed(uint which, uint reason)
    {
        // after an action the notification closes too; that's not news
        if (which != id || answered)
            return;
        answered = true;
        std::printf("closed\n");
        std::fflush(stdout);
        QCoreApplication::exit(reason == 1 ? 1 : 0);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95notify"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Shows a desktop notification."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("summary"), QStringLiteral("The notification's title."));
    parser.addPositionalArgument(QStringLiteral("body"), QStringLiteral("More text (optional)."), QStringLiteral("[body]"));
    const QCommandLineOption appOpt({ QStringLiteral("a"), QStringLiteral("app-name") },
                                    QStringLiteral("Who it's from."), QStringLiteral("name"), QStringLiteral("win95notify"));
    const QCommandLineOption iconOpt({ QStringLiteral("i"), QStringLiteral("icon") },
                                     QStringLiteral("An icon name or image file."), QStringLiteral("icon"));
    const QCommandLineOption urgencyOpt({ QStringLiteral("u"), QStringLiteral("urgency") },
                                        QStringLiteral("low, normal or critical."), QStringLiteral("level"),
                                        QStringLiteral("normal"));
    const QCommandLineOption timeoutOpt({ QStringLiteral("t"), QStringLiteral("expire-time") },
                                        QStringLiteral("Milliseconds on screen (0 = until closed)."),
                                        QStringLiteral("ms"), QStringLiteral("-1"));
    const QCommandLineOption actionOpt({ QStringLiteral("A"), QStringLiteral("action") },
                                       QStringLiteral("A button, as key=Label (repeatable)."), QStringLiteral("action"));
    const QCommandLineOption replaceOpt({ QStringLiteral("r"), QStringLiteral("replace-id") },
                                        QStringLiteral("Replace an earlier notification."), QStringLiteral("id"),
                                        QStringLiteral("0"));
    const QCommandLineOption printOpt({ QStringLiteral("p"), QStringLiteral("print-id") },
                                      QStringLiteral("Print the notification's id."));
    const QCommandLineOption waitOpt({ QStringLiteral("w"), QStringLiteral("wait") },
                                     QStringLiteral("Wait until it's closed; print the action chosen."));
    parser.addOptions({ appOpt, iconOpt, urgencyOpt, timeoutOpt, actionOpt, replaceOpt, printOpt, waitOpt });
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        std::fprintf(stderr, "win95notify: no summary given (see --help)\n");
        return 2;
    }

    const QString urgencyName = parser.value(urgencyOpt);
    const uchar urgency = urgencyName == QLatin1String("low") ? 0 : urgencyName == QLatin1String("critical") ? 2 : 1;
    QStringList actions;
    for (const QString &a : parser.values(actionOpt)) {
        const qsizetype eq = a.indexOf(QLatin1Char('='));
        actions << (eq > 0 ? a.left(eq) : a) << (eq > 0 ? a.mid(eq + 1) : a);
    }
    QVariantMap hints{ { QStringLiteral("urgency"), QVariant::fromValue(urgency) } };

    QDBusConnection bus = QDBusConnection::sessionBus();
    Waiter waiter;
    if (parser.isSet(waitOpt)) {
        bus.connect(Service, Path, Service, QStringLiteral("ActionInvoked"), &waiter, SLOT(actionInvoked(uint,QString)));
        bus.connect(Service, Path, Service, QStringLiteral("NotificationClosed"), &waiter, SLOT(closed(uint,uint)));
    }

    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Service, QStringLiteral("Notify"));
    call << parser.value(appOpt) << parser.value(replaceOpt).toUInt() << parser.value(iconOpt) << args.at(0)
         << args.value(1) << actions << hints << parser.value(timeoutOpt).toInt();
    const QDBusReply<uint> reply = bus.call(call, QDBus::Block, 5000);
    if (!reply.isValid()) {
        std::fprintf(stderr, "win95notify: couldn't show the notification: %s\n", qPrintable(reply.error().message()));
        return 1;
    }
    waiter.id = reply.value();
    if (parser.isSet(printOpt))
        std::printf("%u\n", waiter.id);
    std::fflush(stdout);
    return parser.isSet(waitOpt) ? app.exec() : 0;
}

#include "main.moc"
