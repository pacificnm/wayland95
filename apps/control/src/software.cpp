#include "software.h"
#include "desktopentry.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QXmlStreamReader>

#include <algorithm>
#include <zlib.h>

namespace {

const QString PkService = QStringLiteral("org.freedesktop.PackageKit");
const QString PkPath = QStringLiteral("/org/freedesktop/PackageKit");
const QString PkTransactionIface = QStringLiteral("org.freedesktop.PackageKit.Transaction");

// PackageKit's enums (pk-enum.h)
enum : quint64 {
    FilterNone = 1 << 1,
    FilterInstalled = 1 << 2,
    FilterNotInstalled = 1 << 3,
};
enum : quint64 {
    FlagOnlyTrusted = 1 << 1,
    FlagSimulate = 1 << 2,
};
enum : uint {
    InfoInstalled = 1, InfoAvailable = 2, InfoLow = 3, InfoEnhancement = 4, InfoNormal = 5,
    InfoBugfix = 6, InfoImportant = 7, InfoSecurity = 8, InfoBlocked = 9, InfoUpdating = 11,
    InfoInstalling = 12, InfoRemoving = 13, InfoObsoleting = 15, InfoReinstalling = 19,
    InfoDowngrading = 20,
};
enum : uint { ExitSuccess = 1, ExitCancelled = 3 };

QDBusConnection bus() { return QDBusConnection::systemBus(); }

QString statusText(uint status)
{
    switch (status) {
    case 1: return QStringLiteral("Waiting...");
    case 2: case 3: return QStringLiteral("Starting...");
    case 4: case 5: case 17: return QStringLiteral("Getting information...");
    case 6: return QStringLiteral("Removing...");
    case 7: case 20: case 21: case 22: case 24: case 25: return QStringLiteral("Downloading the list of software...");
    case 8: return QStringLiteral("Downloading...");
    case 9: return QStringLiteral("Installing...");
    case 10: return QStringLiteral("Updating...");
    case 11: return QStringLiteral("Cleaning up...");
    case 13: return QStringLiteral("Finding what else is needed...");
    case 14: return QStringLiteral("Checking signatures...");
    case 15: return QStringLiteral("Testing changes...");
    case 16: return QStringLiteral("Making changes...");
    case 19: return QStringLiteral("Cancelling...");
    case 27: return QStringLiteral("Loading the list of software...");
    case 30: return QStringLiteral("Waiting for another program that's installing software...");
    case 31: return QStringLiteral("Waiting for permission...");
    case 35: return QStringLiteral("Copying files...");
    case 36: return QStringLiteral("Running setup scripts...");
    default: return QString();
    }
}

// "name;version;arch;data"
QStringList splitId(const QString &id) { return id.split(QLatin1Char(';')); }
QString idName(const QString &id) { return splitId(id).value(0); }
QString idVersion(const QString &id) { return splitId(id).value(1); }
QString idArch(const QString &id) { return splitId(id).value(2); }

bool nativeArch(const QString &arch)
{
    static const QStringList native = [] {
        const QString cpu = QSysInfo::currentCpuArchitecture();
        if (cpu == QLatin1String("x86_64"))
            return QStringList{ QStringLiteral("amd64"), QStringLiteral("x86_64") };
        if (cpu == QLatin1String("arm64"))
            return QStringList{ QStringLiteral("arm64"), QStringLiteral("aarch64") };
        if (cpu == QLatin1String("i386"))
            return QStringList{ QStringLiteral("i386"), QStringLiteral("i686") };
        return QStringList{ cpu };
    }();
    return arch.isEmpty() || native.contains(arch) || arch == QLatin1String("all") || arch == QLatin1String("noarch");
}

QByteArray readMaybeGzip(const QString &path)
{
    QByteArray data;
    gzFile f = gzopen(QFile::encodeName(path).constData(), "rb");
    if (!f)
        return data;
    char buffer[1 << 16];
    int n;
    while ((n = gzread(f, buffer, sizeof buffer)) > 0)
        data.append(buffer, n);
    gzclose(f);
    return data;
}

// freedesktop categories, most specific first, to the ones we show
QString categoryFor(const QStringList &cats)
{
    static const QList<std::pair<const char *, const char *>> map = {
        { "Game", "Games" }, { "Development", "Programming" }, { "Office", "Office" },
        { "Graphics", "Graphics" }, { "AudioVideo", "Multimedia" }, { "Audio", "Multimedia" },
        { "Video", "Multimedia" }, { "Network", "Internet" }, { "Education", "Education" },
        { "Science", "Science" }, { "Utility", "Accessories" }, { "System", "System" },
        { "Settings", "System" },
    };
    for (const auto &[from, to] : map)
        if (cats.contains(QLatin1String(from)))
            return QLatin1String(to);
    return QStringLiteral("Other");
}

// AppStream descriptions are a little HTML: paragraphs and lists
QString plainText(QString html)
{
    html.replace(QLatin1String("</p>"), QLatin1String("\n\n"));
    html.replace(QLatin1String("<li>"), QStringLiteral("• "));
    html.replace(QLatin1String("</li>"), QLatin1String("\n"));
    html.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    html.replace(QLatin1String("&lt;"), QLatin1String("<"));
    html.replace(QLatin1String("&gt;"), QLatin1String(">"));
    html.replace(QLatin1String("&quot;"), QLatin1String("\""));
    html.replace(QLatin1String("&apos;"), QLatin1String("'"));
    html.replace(QLatin1String("&amp;"), QLatin1String("&"));
    html.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
    html.replace(QRegularExpression(QStringLiteral(" *\\n *")), QStringLiteral("\n"));
    html.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    return html.trimmed();
}

QString yamlScalar(QByteArray v)
{
    v = v.trimmed();
    if (v.size() >= 2 && v.startsWith('\'') && v.endsWith('\''))
        return QString::fromUtf8(v.mid(1, v.size() - 2)).replace(QLatin1String("''"), QLatin1String("'"));
    if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"')) {
        QString s = QString::fromUtf8(v.mid(1, v.size() - 2));
        s.replace(QLatin1String("\\\""), QLatin1String("\"")).replace(QLatin1String("\\\\"), QLatin1String("\\"));
        return s;
    }
    return QString::fromUtf8(v);
}

int indentOf(const QByteArray &line)
{
    int i = 0;
    while (i < line.size() && line.at(i) == ' ')
        ++i;
    return i;
}

QString iconUrl(const QString &icon) { return appIconUrl(icon); }

QStringList dataDirs()
{
    QStringList dirs;
    const QString env = qEnvironmentVariable("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    for (const QString &d : env.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        if (!d.contains(QLatin1String("flatpak")) && !dirs.contains(d))
            dirs << d;
    return dirs;
}

QString flatpakUserDir() { return QDir::homePath() + QStringLiteral("/.local/share/flatpak"); }
const QString FlatpakSystemDir = QStringLiteral("/var/lib/flatpak");

} // namespace

// ---------------------------------------------------------------------------
// PkTransaction

PkTransaction::PkTransaction(QObject *parent)
    : QObject(parent)
{
}

void PkTransaction::start(const QString &method, const QVariantList &args, bool interactive)
{
    const QDBusReply<QDBusObjectPath> created = bus().call(
        QDBusMessage::createMethodCall(PkService, PkPath, QStringLiteral("org.freedesktop.PackageKit"),
                                       QStringLiteral("CreateTransaction")), QDBus::Block, 15000);
    if (!created.isValid()) {
        // fail after the caller has connected to our signals
        QMetaObject::invokeMethod(this, [this, e = created.error().message()] {
            fail(QStringLiteral("The package manager isn't available (%1).").arg(e));
        }, Qt::QueuedConnection);
        return;
    }
    m_path = created.value().path();

    auto sub = [this](const char *name, const char *slot) {
        bus().connect(PkService, m_path, PkTransactionIface, QLatin1String(name), this, slot);
    };
    sub("Package", SLOT(onPackage(uint,QString,QString)));
    sub("Files", SLOT(onFiles(QString,QStringList)));
    sub("Details", SLOT(onDetails(QVariantMap)));
    sub("ErrorCode", SLOT(onErrorCode(uint,QString)));
    sub("Finished", SLOT(onFinished(uint,uint)));
    bus().connect(PkService, m_path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                  this, SLOT(onProperties(QString,QVariantMap,QStringList)));

    QStringList hints{ QStringLiteral("interactive=") + (interactive ? QStringLiteral("true") : QStringLiteral("false")) };
    const QString lang = qEnvironmentVariable("LANG");
    if (!lang.isEmpty())
        hints << QStringLiteral("locale=") + lang;
    QDBusMessage setHints = QDBusMessage::createMethodCall(PkService, m_path, PkTransactionIface, QStringLiteral("SetHints"));
    setHints << hints;
    bus().asyncCall(setHints);

    QDBusMessage call = QDBusMessage::createMethodCall(PkService, m_path, PkTransactionIface, method);
    call.setArguments(args);
    // long timeout: interactive calls wait for the password dialog
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, 10 * 60 * 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher->isError())
            fail(watcher->error().message());
    });
}

void PkTransaction::cancel()
{
    if (!m_path.isEmpty() && !m_done)
        bus().asyncCall(QDBusMessage::createMethodCall(PkService, m_path, PkTransactionIface, QStringLiteral("Cancel")));
}

void PkTransaction::fail(const QString &error)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(false, error);
    deleteLater();
}

void PkTransaction::onPackage(uint info, const QString &id, const QString &summary) { emit package(info, id, summary); }
void PkTransaction::onFiles(const QString &id, const QStringList &list) { emit files(id, list); }
void PkTransaction::onDetails(const QVariantMap &details) { emit this->details(details); }

void PkTransaction::onErrorCode(uint code, const QString &details)
{
    if (m_error.isEmpty())
        m_error = details.trimmed();
    emit errorCode(code, details);
}

void PkTransaction::onFinished(uint exit, uint)
{
    if (m_done)
        return;
    m_done = true;
    if (exit == ExitSuccess)
        emit finished(true, QString());
    else if (exit == ExitCancelled)
        emit finished(false, QStringLiteral("Cancelled."));
    else
        emit finished(false, m_error.isEmpty() ? QStringLiteral("The package manager couldn't finish.") : m_error);
    deleteLater();
}

void PkTransaction::onProperties(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != PkTransactionIface)
        return;
    bool any = false;
    if (changed.contains(QStringLiteral("Percentage"))) {
        const uint p = changed.value(QStringLiteral("Percentage")).toUInt();
        m_percent = p > 100 ? -1 : int(p);
        any = true;
    }
    if (changed.contains(QStringLiteral("Status"))) {
        m_status = changed.value(QStringLiteral("Status")).toUInt();
        any = true;
    }
    if (any)
        emit progress(m_percent, m_status);
}

// ---------------------------------------------------------------------------
// Catalogs (run on a worker thread)

void SoftwareCenter::loadDebCatalog(QList<Program> &out)
{
    // Debian/Ubuntu: DEP-11 YAML, downloaded by apt with the package lists
    QString yamlDir, iconRoot;
    for (const QString &base : { QStringLiteral("/var/lib/swcatalog"), QStringLiteral("/var/lib/app-info") }) {
        if (QDir(base + QStringLiteral("/yaml")).exists()) {
            yamlDir = base + QStringLiteral("/yaml");
            iconRoot = base + QStringLiteral("/icons");
            break;
        }
    }
    if (yamlDir.isEmpty())
        return;

    const QFileInfoList files = QDir(yamlDir).entryInfoList({ QStringLiteral("*.yml.gz"), QStringLiteral("*.yml") }, QDir::Files);
    for (const QFileInfo &file : files) {
        const QByteArray data = readMaybeGzip(file.absoluteFilePath());
        QString origin;
        // the document being read
        QByteArray type, topKey;
        Program p;
        QStringList cats;
        QString iconFile, pendingIcon, stockIcon;
        int iconRank = 0;
        bool inDesc = false, inKeywords = false;
        QByteArray desc;

        auto flush = [&] {
            if (type == "desktop-application" && !p.id.isEmpty() && !p.name.isEmpty()) {
                p.source = QStringLiteral("deb");
                p.category = categoryFor(cats);
                p.description = plainText(QString::fromUtf8(desc));
                p.icon = !iconFile.isEmpty() ? QUrl::fromLocalFile(iconFile).toString() : iconUrl(stockIcon);
                out << p;
            }
            type.clear();
            topKey.clear();
            p = Program();
            cats.clear();
            iconFile.clear();
            pendingIcon.clear();
            stockIcon.clear();
            iconRank = 0;
            inDesc = inKeywords = false;
            desc.clear();
        };

        qsizetype pos = 0;
        while (pos < data.size()) {
            qsizetype end = data.indexOf('\n', pos);
            if (end < 0)
                end = data.size();
            const QByteArray line = data.mid(pos, end - pos);
            pos = end + 1;

            if (line == "---") {
                flush();
                continue;
            }
            const int indent = indentOf(line);
            if (inDesc) {
                if (line.trimmed().isEmpty() || indent >= 4) {
                    desc += line.trimmed() + ' ';
                    continue;
                }
                inDesc = false;
            }
            if (indent == 0 && !line.startsWith("- ")) {
                inKeywords = false;
                const int colon = line.indexOf(':');
                if (colon < 0)
                    continue;
                topKey = line.left(colon);
                const QByteArray value = line.mid(colon + 1).trimmed();
                if (topKey == "Type")
                    type = value;
                else if (topKey == "Package")
                    p.id = yamlScalar(value);
                else if (topKey == "Origin")
                    origin = yamlScalar(value);
                continue;
            }
            if (topKey == "Categories" && line.startsWith("- ")) {
                cats << yamlScalar(line.mid(2));
            } else if ((topKey == "Name" || topKey == "Summary") && line.startsWith("  C: ")) {
                (topKey == "Name" ? p.name : p.summary) = yamlScalar(line.mid(5));
            } else if (topKey == "Description" && line.startsWith("  C:")) {
                inDesc = true;
                const QByteArray rest = line.mid(4).trimmed();
                if (rest != ">-" && rest != "|-" && rest != ">" && rest != "|")
                    desc = yamlScalar(rest).toUtf8();
            } else if (topKey == "Keywords") {
                if (indent == 2 && !line.startsWith("  - "))
                    inKeywords = line.startsWith("  C:");
                else if (inKeywords && line.trimmed().startsWith("- "))
                    p.keywords << yamlScalar(line.trimmed().mid(2)).toLower();
            } else if (topKey == "Icon") {
                const QByteArray t = line.trimmed();
                if (t.startsWith("- name: ")) {
                    pendingIcon = yamlScalar(t.mid(8));
                } else if (t.startsWith("width: ") && !pendingIcon.isEmpty() && indent >= 4) {
                    const int w = t.mid(7).toInt();
                    const int rank = w == 64 ? 3 : w == 48 ? 2 : w == 128 ? 1 : 0;
                    if (rank > iconRank) {
                        const QString path = QStringLiteral("%1/%2/%3x%3/%4").arg(iconRoot, origin).arg(w).arg(pendingIcon);
                        if (QFileInfo::exists(path)) {
                            iconRank = rank;
                            iconFile = path;
                        }
                    }
                } else if (t.startsWith("stock: ")) {
                    stockIcon = yamlScalar(t.mid(7));
                }
            }
        }
        flush();
    }
}

void SoftwareCenter::loadFlatpakCatalog(QList<Program> &out, QStringList &remotes)
{
    QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QLatin1String("arm64"))
        arch = QStringLiteral("aarch64");

    for (const auto &[dir, installation] : { std::pair{ flatpakUserDir(), QStringLiteral("user") },
                                              std::pair{ FlatpakSystemDir, QStringLiteral("system") } }) {
        // remotes, from the installation's repo config
        QFile config(dir + QStringLiteral("/repo/config"));
        if (config.open(QIODevice::ReadOnly)) {
            static const QRegularExpression re(QStringLiteral("^\\[remote \"([^\"]+)\"\\]"), QRegularExpression::MultilineOption);
            auto it = re.globalMatch(QString::fromUtf8(config.readAll()));
            while (it.hasNext()) {
                const QString remote = it.next().captured(1);
                if (!remotes.contains(remote))
                    remotes << remote;
            }
        }

        const QString appstream = dir + QStringLiteral("/appstream");
        for (const QString &remote : QDir(appstream).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString active = QStringLiteral("%1/%2/%3/active").arg(appstream, remote, arch);
            QString xmlPath = active + QStringLiteral("/appstream.xml.gz");
            if (!QFileInfo::exists(xmlPath))
                xmlPath = active + QStringLiteral("/appstream.xml");
            const QByteArray data = readMaybeGzip(xmlPath);
            if (data.isEmpty())
                continue;

            QXmlStreamReader xml(data);
            Program p;
            QStringList cats;
            bool inComponent = false, inDescription = false;
            QString desc;
            QString icon;
            int iconRank = 0;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    const auto name = xml.name();
                    const bool untranslated = !xml.attributes().hasAttribute(QLatin1String("xml:lang"));
                    if (name == QLatin1String("component")) {
                        const auto t = xml.attributes().value(QLatin1String("type"));
                        inComponent = t == QLatin1String("desktop-application") || t == QLatin1String("desktop");
                        p = Program();
                        cats.clear();
                        desc.clear();
                        icon.clear();
                        iconRank = 0;
                    } else if (!inComponent) {
                        continue;
                    } else if (name == QLatin1String("id") && p.id.isEmpty()) {
                        p.id = xml.readElementText().trimmed();
                        if (p.id.endsWith(QLatin1String(".desktop")))
                            p.id.chop(8);
                    } else if (name == QLatin1String("bundle") && xml.attributes().value(QLatin1String("type")) == QLatin1String("flatpak")) {
                        const QStringList ref = xml.readElementText().trimmed().split(QLatin1Char('/'));
                        if (ref.size() >= 2)
                            p.id = ref.at(1);
                    } else if (name == QLatin1String("name") && untranslated && !inDescription) {
                        p.name = xml.readElementText().trimmed();
                    } else if (name == QLatin1String("summary") && untranslated) {
                        p.summary = xml.readElementText().trimmed();
                    } else if (name == QLatin1String("description")) {
                        inDescription = true;
                    } else if (inDescription && (name == QLatin1String("p") || name == QLatin1String("li")) && untranslated) {
                        const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
                        desc += name == QLatin1String("li") ? QStringLiteral("• ") + text + QLatin1Char('\n') : text + QStringLiteral("\n\n");
                    } else if (name == QLatin1String("category")) {
                        cats << xml.readElementText().trimmed();
                    } else if (name == QLatin1String("keyword") && untranslated) {
                        p.keywords << xml.readElementText().trimmed().toLower();
                    } else if (name == QLatin1String("icon") && xml.attributes().value(QLatin1String("type")) == QLatin1String("cached")) {
                        const int w = xml.attributes().value(QLatin1String("width")).toInt();
                        const QString file = xml.readElementText().trimmed();
                        const int rank = w == 64 ? 3 : w == 128 ? 2 : w > 0 ? 1 : 0;
                        const QString path = QStringLiteral("%1/icons/%2x%2/%3").arg(active).arg(w).arg(file);
                        if (rank > iconRank && QFileInfo::exists(path)) {
                            iconRank = rank;
                            icon = path;
                        }
                    }
                } else if (xml.isEndElement()) {
                    if (xml.name() == QLatin1String("description")) {
                        inDescription = false;
                    } else if (xml.name() == QLatin1String("component") && inComponent) {
                        inComponent = false;
                        if (!p.id.isEmpty() && !p.name.isEmpty()) {
                            p.source = QStringLiteral("flatpak");
                            p.remote = remote;
                            p.installation = installation;
                            p.category = categoryFor(cats);
                            p.description = desc.trimmed();
                            p.icon = icon.isEmpty() ? QString() : QUrl::fromLocalFile(icon).toString();
                            out << p;
                        }
                    }
                }
            }
        }
    }
}

SoftwareCenter::Catalog SoftwareCenter::loadCatalog(const QString &flatpakBin)
{
    Catalog c;
    loadDebCatalog(c.programs);
    if (!flatpakBin.isEmpty())
        loadFlatpakCatalog(c.programs, c.remotes);
    return c;
}

QVariantMap SoftwareCenter::toVariant(const Program &p)
{
    return {
        { QStringLiteral("key"), p.source + QLatin1Char(':') + p.id },
        { QStringLiteral("source"), p.source },
        { QStringLiteral("id"), p.id },
        { QStringLiteral("remote"), p.remote },
        { QStringLiteral("installation"), p.installation },
        { QStringLiteral("name"), p.name },
        { QStringLiteral("summary"), p.summary },
        { QStringLiteral("description"), p.description },
        { QStringLiteral("icon"), p.icon },
        { QStringLiteral("category"), p.category },
    };
}

// ---------------------------------------------------------------------------
// SoftwareCenter

#ifndef WAYLAND95_UPDATES_URL
#define WAYLAND95_UPDATES_URL "https://pacificnm.github.io/wayland95"
#endif

QString SoftwareCenter::desktopUpdates() const
{
    if (!QFileInfo::exists(QStringLiteral("/var/lib/dpkg/info/wayland95.list")))
        return QStringLiteral("source");
    // any apt source naming the update repository
    QStringList files{ QStringLiteral("/etc/apt/sources.list") };
    const QDir dir(QStringLiteral("/etc/apt/sources.list.d"));
    for (const QString &f : dir.entryList({ QStringLiteral("*.list"), QStringLiteral("*.sources") }, QDir::Files))
        files << dir.filePath(f);
    for (const QString &path : std::as_const(files)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly) && f.readAll().contains(WAYLAND95_UPDATES_URL))
            return QStringLiteral("package");
    }
    return QStringLiteral("no-source");
}

void SoftwareCenter::setUpDesktopUpdates()
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QString helper = QStringLiteral(WAYLAND95_SERVICES_HELPER);
    if (!QFileInfo::exists(helper))
        helper = QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC);
    if (pkexec.isEmpty()) {
        emit desktopUpdatesSetUp(false, QStringLiteral("pkexec isn't installed (sudo apt install pkexec)."));
        return;
    }
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAll()).trimmed();
        p->deleteLater();
        emit desktopUpdatesChanged();
        if (st == QProcess::NormalExit && code == 0) {
            emit desktopUpdatesSetUp(true, QString());
            checkUpdates(true);
        } else {
            emit desktopUpdatesSetUp(false, code == 126 || code == 127
                                     ? QStringLiteral("You need an administrator's password to change this.") : out);
        }
    });
    p->start(pkexec, { helper, QStringLiteral("update-source") });
}

SoftwareCenter::SoftwareCenter(QObject *parent)
    : QObject(parent)
{
    QDBusConnectionInterface *iface = bus().isConnected() ? bus().interface() : nullptr;
    m_packageKit = iface && (iface->isServiceRegistered(PkService).value()
                             || iface->activatableServiceNames().value().contains(PkService));
    m_flatpakBin = QStandardPaths::findExecutable(QStringLiteral("flatpak"));

    // what to call the distribution's software: "Debian", "Fedora"...
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : osRelease.readAll().split('\n')) {
            if (line.startsWith("NAME="))
                m_packageSource = yamlScalar(line.mid(5)).section(QLatin1Char(' '), 0, 0);
        }
    }
    if (m_packageSource.isEmpty())
        m_packageSource = QStringLiteral("System");

    loadCatalogAsync();
    reload();
}

SoftwareCenter::~SoftwareCenter()
{
    if (m_catalogThread)
        m_catalogThread->wait();
    if (m_process)
        m_process->kill();
}

QStringList SoftwareCenter::categories() const
{
    return { QStringLiteral("Accessories"), QStringLiteral("Education"), QStringLiteral("Games"),
             QStringLiteral("Graphics"), QStringLiteral("Internet"), QStringLiteral("Multimedia"),
             QStringLiteral("Office"), QStringLiteral("Programming"), QStringLiteral("Science"),
             QStringLiteral("System"), QStringLiteral("Other") };
}

void SoftwareCenter::loadCatalogAsync()
{
    if (m_catalogThread) {
        m_catalogAgain = true; // flathub was just set up: read it once this load is done
        return;
    }
    m_catalogLoading = true;
    emit catalogChanged();
    const QString bin = m_flatpakBin;
    m_catalogThread = QThread::create([this, bin] {
        Catalog c = loadCatalog(bin);
        QMetaObject::invokeMethod(this, [this, c = std::move(c)]() mutable {
            m_catalog = std::move(c.programs);
            m_remotes = c.remotes;
            m_catalogThread->wait();
            delete m_catalogThread;
            m_catalogThread = nullptr;
            m_catalogLoading = false;
            emit catalogChanged();
            if (m_catalogAgain) {
                m_catalogAgain = false;
                loadCatalogAsync();
            }
        }, Qt::QueuedConnection);
    });
    m_catalogThread->start(QThread::LowPriority);
}

void SoftwareCenter::reload()
{
    if (m_loading > 0)
        return;
    if (m_packageKit)
        loadDebPrograms();
    if (flatpak())
        loadFlatpakPrograms();
}

void SoftwareCenter::loadDebPrograms()
{
    ++m_loading;
    emit loadingChanged();

    // the installed packages
    auto *all = pk(QStringLiteral("GetPackages"), { QVariant::fromValue(quint64(FilterInstalled)) }, false);
    auto names = std::make_shared<QSet<QString>>();
    connect(all, &PkTransaction::package, this, [names](uint, const QString &id, const QString &) { names->insert(idName(id)); });
    connect(all, &PkTransaction::finished, this, [this, names](bool ok) {
        if (ok)
            m_installedDeb = *names;
        emit programsChanged(); // search results' "installed" may change
    });

    // programs: the packages that own the visible .desktop files
    auto entries = std::make_shared<QHash<QString, DesktopEntry>>();
    for (const QString &dir : dataDirs()) {
        const QDir apps(dir + QStringLiteral("/applications"));
        QDirIterator it(apps.path(), { QStringLiteral("*.desktop") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QFileInfo(it.next()).canonicalFilePath();
            if (path.isEmpty() || entries->contains(path))
                continue;
            // every program a package brings, even ones only for another
            // desktop's menus: they're still removable
            const DesktopEntry e = DesktopEntry::read(path);
            if (e.application && !e.noDisplay && !e.hidden && !e.name.isEmpty())
                entries->insert(path, e);
        }
    }
    if (entries->isEmpty()) {
        m_debPrograms.clear();
        finishLoad();
        return;
    }

    auto owners = std::make_shared<QSet<QString>>();
    auto *search = pk(QStringLiteral("SearchFiles"), { QVariant::fromValue(quint64(FilterInstalled)), QStringList(entries->keys()) }, false);
    connect(search, &PkTransaction::package, this, [owners](uint, const QString &id, const QString &) { owners->insert(id); });
    connect(search, &PkTransaction::finished, this, [this, entries, owners](bool ok) {
        if (!ok || owners->isEmpty()) {
            m_debPrograms.clear();
            finishLoad();
            return;
        }
        // which of those files each package owns
        auto summaries = std::make_shared<QHash<QString, QStringList>>();
        auto *files = pk(QStringLiteral("GetFiles"), { QStringList(owners->values()) }, false);
        connect(files, &PkTransaction::files, this, [entries, summaries](const QString &id, const QStringList &list) {
            for (const QString &f : list)
                if (f.endsWith(QLatin1String(".desktop")) && entries->contains(f))
                    (*summaries)[id] << f;
        });
        connect(files, &PkTransaction::finished, this, [this, entries, summaries](bool) {
            QHash<QString, const Program *> catalog;
            for (const Program &p : std::as_const(m_catalog))
                if (p.source == QLatin1String("deb"))
                    catalog.insert(p.id, &p);

            QVariantList list;
            for (auto it = summaries->cbegin(); it != summaries->cend(); ++it) {
                const QString name = idName(it.key());
                QStringList paths = it.value();
                std::sort(paths.begin(), paths.end());
                // a package with several programs: the catalog's name for
                // it, or the one named after the package
                DesktopEntry main = entries->value(paths.first());
                for (const QString &p : std::as_const(paths))
                    if (QFileInfo(p).completeBaseName().endsWith(name))
                        main = entries->value(p);
                const Program *known = paths.size() > 1 ? catalog.value(name) : nullptr;
                list << QVariantMap{
                    { QStringLiteral("key"), QStringLiteral("deb:") + name },
                    { QStringLiteral("source"), QStringLiteral("deb") },
                    { QStringLiteral("id"), name },
                    { QStringLiteral("packageId"), it.key() },
                    { QStringLiteral("name"), known ? known->name : main.name },
                    { QStringLiteral("summary"), known ? known->summary : main.comment },
                    { QStringLiteral("icon"), known && !known->icon.isEmpty() ? known->icon : iconUrl(main.icon) },
                    { QStringLiteral("version"), idVersion(it.key()) },
                    { QStringLiteral("programCount"), paths.size() },
                };
            }
            m_debPrograms = list;
            finishLoad();
        });
    });
}

void SoftwareCenter::loadFlatpakPrograms()
{
    ++m_loading;
    emit loadingChanged();
    runFlatpak({ QStringLiteral("list"), QStringLiteral("--app"), QStringLiteral("--columns=application,name,version,installation,description") },
               false, [this](int code, const QString &out) {
        QVariantList list;
        QHash<QString, QString> installed;
        if (code == 0) {
            for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QStringList f = line.split(QLatin1Char('\t'));
                if (f.size() < 4)
                    continue;
                const QString id = f.at(0).trimmed();
                installed.insert(id, f.at(3).trimmed());
                list << QVariantMap{
                    { QStringLiteral("key"), QStringLiteral("flatpak:") + id },
                    { QStringLiteral("source"), QStringLiteral("flatpak") },
                    { QStringLiteral("id"), id },
                    { QStringLiteral("installation"), f.at(3).trimmed() },
                    { QStringLiteral("name"), f.at(1).trimmed().isEmpty() ? id : f.at(1).trimmed() },
                    { QStringLiteral("summary"), f.value(4).trimmed() },
                    { QStringLiteral("icon"), iconUrl(id) },
                    { QStringLiteral("version"), f.at(2).trimmed() },
                };
            }
        }
        m_installedFlatpak = installed;
        m_flatpakPrograms = list;
        finishLoad();
    });
}

void SoftwareCenter::finishLoad()
{
    if (--m_loading > 0)
        return;
    QVariantList all = m_debPrograms + m_flatpakPrograms;
    std::sort(all.begin(), all.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    m_programs = all;
    emit programsChanged();
    emit loadingChanged();
}

QVariantList SoftwareCenter::search(const QString &text, const QString &category, const QString &source, int limit) const
{
    const QStringList words = text.toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QList<std::pair<int, const Program *>> hits;
    for (const Program &p : m_catalog) {
        if (!source.isEmpty() && p.source != source)
            continue;
        if (!category.isEmpty() && p.category != category)
            continue;
        int score = 0;
        const QString name = p.name.toLower();
        bool all = true;
        for (const QString &w : words) {
            int s = 0;
            if (name == w) s = 100;
            else if (name.startsWith(w)) s = 60;
            else if (name.contains(w)) s = 40;
            else if (std::any_of(p.keywords.cbegin(), p.keywords.cend(), [&](const QString &k) { return k.startsWith(w); })) s = 20;
            else if (p.id.toLower().contains(w) || p.summary.toLower().contains(w)) s = 10;
            if (s == 0) {
                all = false;
                break;
            }
            score += s;
        }
        if (all)
            hits << std::pair{ score, &p };
    }
    std::sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second->name.compare(b.second->name, Qt::CaseInsensitive) < 0;
    });

    QVariantList result;
    for (const auto &[score, p] : std::as_const(hits)) {
        if (result.size() >= limit)
            break;
        QVariantMap m = toVariant(*p);
        m.insert(QStringLiteral("installed"), p->source == QLatin1String("deb") ? m_installedDeb.contains(p->id)
                                                                               : m_installedFlatpak.contains(p->id));
        result << m;
    }
    return result;
}

void SoftwareCenter::searchPackages(const QString &text)
{
    if (!m_packageKit || text.trimmed().isEmpty()) {
        emit packagesFound({});
        return;
    }
    auto found = std::make_shared<QMap<QString, QVariantMap>>();
    auto *t = pk(QStringLiteral("SearchDetails"), { QVariant::fromValue(quint64(FilterNone)), QStringList{ text.trimmed() } }, false);
    connect(t, &PkTransaction::package, this, [found](uint info, const QString &id, const QString &summary) {
        const QString name = idName(id);
        if (!nativeArch(idArch(id)) || found->size() >= 500)
            return;
        QVariantMap &m = (*found)[name];
        const bool installed = info == InfoInstalled;
        if (!m.isEmpty() && !installed)
            return;
        m = QVariantMap{
            { QStringLiteral("key"), QStringLiteral("deb:") + name },
            { QStringLiteral("source"), QStringLiteral("deb") },
            { QStringLiteral("id"), name },
            { QStringLiteral("name"), name },
            { QStringLiteral("summary"), summary },
            { QStringLiteral("description"), summary },
            { QStringLiteral("icon"), QString() },
            { QStringLiteral("installed"), installed },
            { QStringLiteral("version"), idVersion(id) },
        };
    });
    connect(t, &PkTransaction::finished, this, [this, found](bool) {
        QVariantList list;
        for (const QVariantMap &m : std::as_const(*found))
            list << m;
        emit packagesFound(list);
    });
}

// ---------------------------------------------------------------------------
// Installing, removing, updating

void SoftwareCenter::prepareInstall(const QVariantMap &item)
{
    const QString name = item.value(QStringLiteral("name")).toString();
    const QString id = item.value(QStringLiteral("id")).toString();
    m_planSteps.clear();
    m_planTitle = QStringLiteral("Installing ") + name;

    if (item.value(QStringLiteral("source")) == QLatin1String("flatpak")) {
        const QString installation = item.value(QStringLiteral("installation")).toString() == QLatin1String("system")
                                         ? QStringLiteral("--system") : QStringLiteral("--user");
        const QString remote = item.value(QStringLiteral("remote")).toString();
        m_planSteps << [this, installation, remote, id] {
            runFlatpak({ QStringLiteral("install"), installation, QStringLiteral("--noninteractive"), QStringLiteral("-y"), remote, id },
                       true, [this](int code, const QString &out) {
                code == 0 ? nextStep() : taskFailed(out.trimmed().section(QLatin1Char('\n'), -1));
            });
        };
        emit plan(QStringLiteral("install"), { item }, {}, QString());
        return;
    }

    // which package exactly, then what else it brings along
    auto candidates = std::make_shared<QStringList>();
    auto *resolve = pk(QStringLiteral("Resolve"), { QVariant::fromValue(quint64(FilterNotInstalled)), QStringList{ id } }, false);
    connect(resolve, &PkTransaction::package, this, [candidates](uint, const QString &pkgId, const QString &) {
        if (nativeArch(idArch(pkgId)))
            candidates->prepend(pkgId);
        else
            candidates->append(pkgId);
    });
    connect(resolve, &PkTransaction::finished, this, [this, item, candidates](bool ok, const QString &error) {
        if (!ok || candidates->isEmpty()) {
            emit plan(QStringLiteral("install"), { item }, {}, ok ? QStringLiteral("It isn't available from your software sources.") : error);
            return;
        }
        const QString pkgId = candidates->first();
        auto others = std::make_shared<QStringList>();
        auto *simulate = pk(QStringLiteral("InstallPackages"), { QVariant::fromValue(quint64(FlagOnlyTrusted | FlagSimulate)), QStringList{ pkgId } }, false);
        connect(simulate, &PkTransaction::package, this, [others, pkgId](uint info, const QString &id, const QString &) {
            if (idName(id) != idName(pkgId) && (info == InfoInstalling || info == InfoUpdating || info == InfoRemoving || info == InfoObsoleting))
                *others << (info == InfoRemoving || info == InfoObsoleting ? QStringLiteral("-") : QString()) + idName(id);
        });
        connect(simulate, &PkTransaction::finished, this, [this, item, pkgId, others](bool ok, const QString &error) {
            if (!ok) {
                emit plan(QStringLiteral("install"), { item }, {}, error);
                return;
            }
            m_planSteps = { [this, pkgId] {
                auto *t = pk(QStringLiteral("InstallPackages"), { QVariant::fromValue(quint64(FlagOnlyTrusted)), QStringList{ pkgId } }, true);
                connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) { ok ? nextStep() : taskFailed(error); });
            } };
            others->sort();
            emit plan(QStringLiteral("install"), { item }, *others, QString());
        });
    });
}

void SoftwareCenter::prepareRemove(const QVariantMap &program)
{
    const QString name = program.value(QStringLiteral("name")).toString();
    m_planSteps.clear();
    m_planTitle = QStringLiteral("Removing ") + name;

    if (program.value(QStringLiteral("source")) == QLatin1String("flatpak")) {
        const QString id = program.value(QStringLiteral("id")).toString();
        const bool system = program.value(QStringLiteral("installation")).toString() == QLatin1String("system");
        const QString where = system ? QStringLiteral("--system") : QStringLiteral("--user");
        m_planSteps << [this, id, where] {
            runFlatpak({ QStringLiteral("uninstall"), where, QStringLiteral("--noninteractive"), QStringLiteral("-y"), id },
                       true, [this](int code, const QString &out) {
                code == 0 ? nextStep() : taskFailed(out.trimmed().section(QLatin1Char('\n'), -1));
            });
        };
        // and the runtimes nothing uses any more (the user's own only:
        // the system's may belong to other people)
        if (!system) {
            m_planSteps << [this] {
                runFlatpak({ QStringLiteral("uninstall"), QStringLiteral("--user"), QStringLiteral("--unused"), QStringLiteral("--noninteractive"), QStringLiteral("-y") },
                           true, [this](int, const QString &) { nextStep(); });
            };
        }
        emit plan(QStringLiteral("remove"), { program }, {}, QString());
        return;
    }

    const QString pkgId = program.value(QStringLiteral("packageId")).toString();
    auto others = std::make_shared<QStringList>();
    auto *simulate = pk(QStringLiteral("RemovePackages"), { QVariant::fromValue(quint64(FlagSimulate)), QStringList{ pkgId }, true, false }, false);
    connect(simulate, &PkTransaction::package, this, [others, pkgId](uint info, const QString &id, const QString &) {
        if (idName(id) != idName(pkgId) && (info == InfoRemoving || info == InfoObsoleting))
            *others << idName(id);
    });
    connect(simulate, &PkTransaction::finished, this, [this, program, pkgId, others](bool ok, const QString &error) {
        if (!ok) {
            emit plan(QStringLiteral("remove"), { program }, {}, error);
            return;
        }
        m_planSteps = { [this, pkgId] {
            auto *t = pk(QStringLiteral("RemovePackages"), { QVariant::fromValue(quint64(0)), QStringList{ pkgId }, true, false }, true);
            connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) { ok ? nextStep() : taskFailed(error); });
        } };
        others->sort();
        emit plan(QStringLiteral("remove"), { program }, *others, QString());
    });
}

void SoftwareCenter::prepareUpdates(const QVariantList &updates)
{
    QStringList debIds, userApps, systemApps;
    for (const QVariant &v : updates) {
        const QVariantMap u = v.toMap();
        if (u.value(QStringLiteral("source")) == QLatin1String("deb"))
            debIds << u.value(QStringLiteral("packageId")).toString();
        else if (u.value(QStringLiteral("installation")) == QLatin1String("system"))
            systemApps << u.value(QStringLiteral("id")).toString();
        else
            userApps << u.value(QStringLiteral("id")).toString();
    }
    m_planSteps.clear();
    m_planTitle = updates.size() == 1 ? QStringLiteral("Installing 1 update") : QStringLiteral("Installing %1 updates").arg(updates.size());

    if (!debIds.isEmpty()) {
        m_planSteps << [this, debIds] {
            auto *t = pk(QStringLiteral("UpdatePackages"), { QVariant::fromValue(quint64(FlagOnlyTrusted)), debIds }, true);
            connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) { ok ? nextStep() : taskFailed(error); });
        };
    }
    for (const auto &[apps, where] : { std::pair{ userApps, QStringLiteral("--user") }, std::pair{ systemApps, QStringLiteral("--system") } }) {
        if (apps.isEmpty())
            continue;
        m_planSteps << [this, apps, where] {
            runFlatpak(QStringList{ QStringLiteral("update"), where, QStringLiteral("--noninteractive"), QStringLiteral("-y") } + apps,
                       true, [this](int code, const QString &out) {
                code == 0 ? nextStep() : taskFailed(out.trimmed().section(QLatin1Char('\n'), -1));
            });
        };
    }

    if (debIds.isEmpty()) {
        emit plan(QStringLiteral("update"), updates, {}, QString());
        return;
    }
    // new packages the updates need
    auto others = std::make_shared<QStringList>();
    auto *simulate = pk(QStringLiteral("UpdatePackages"), { QVariant::fromValue(quint64(FlagOnlyTrusted | FlagSimulate)), debIds }, false);
    QSet<QString> asked;
    for (const QString &id : std::as_const(debIds))
        asked.insert(idName(id));
    connect(simulate, &PkTransaction::package, this, [others, asked](uint info, const QString &id, const QString &) {
        if (asked.contains(idName(id)))
            return;
        if (info == InfoInstalling)
            *others << idName(id);
        else if (info == InfoRemoving || info == InfoObsoleting)
            *others << QStringLiteral("-") + idName(id);
    });
    connect(simulate, &PkTransaction::finished, this, [this, updates, others](bool ok, const QString &error) {
        others->removeDuplicates();
        others->sort();
        emit plan(QStringLiteral("update"), updates, *others, ok ? QString() : error);
    });
}

void SoftwareCenter::commit()
{
    if (m_busy || m_planSteps.isEmpty())
        return;
    const QList<Step> steps = m_planSteps;
    m_planSteps.clear();
    runTask(m_planTitle, steps);
}

void SoftwareCenter::cancel()
{
    m_steps.clear();
    if (m_current)
        m_current->cancel();
    if (m_process)
        m_process->terminate();
}

void SoftwareCenter::runTask(const QString &title, const QList<Step> &steps)
{
    m_busy = true;
    m_task = title;
    m_status = QStringLiteral("Starting...");
    m_percent = -1;
    m_steps = steps;
    emit taskChanged();
    nextStep();
}

void SoftwareCenter::nextStep()
{
    if (!m_busy)
        return;
    if (m_steps.isEmpty()) {
        m_busy = false;
        m_current = nullptr;
        emit taskChanged();
        emit taskFinished(true, QString());
        reload();
        if (m_updatesChecked)
            checkUpdates(false);
        return;
    }
    const Step step = m_steps.takeFirst();
    step();
}

void SoftwareCenter::taskFailed(const QString &error)
{
    m_steps.clear();
    m_busy = false;
    m_current = nullptr;
    emit taskChanged();
    emit taskFinished(false, error.isEmpty() ? QStringLiteral("Something went wrong.") : error);
    reload();
}

void SoftwareCenter::setStatus(const QString &status, int percent)
{
    if (!status.isEmpty())
        m_status = status;
    m_percent = percent;
    emit taskChanged();
}

PkTransaction *SoftwareCenter::pk(const QString &method, const QVariantList &args, bool forTask)
{
    auto *t = new PkTransaction(this);
    if (forTask) {
        m_current = t;
        connect(t, &PkTransaction::progress, this, [this](int percent, uint status) { setStatus(statusText(status), percent); });
    }
    t->start(method, args, forTask);
    return t;
}

void SoftwareCenter::runFlatpak(const QStringList &args, bool forTask, std::function<void(int, const QString &)> done)
{
    auto *p = new QProcess(this);
    p->setProgram(m_flatpakBin);
    p->setArguments(args);
    p->setProcessChannelMode(QProcess::MergedChannels);
    auto output = std::make_shared<QString>();
    if (forTask) {
        m_process = p;
        setStatus(args.value(0) == QLatin1String("uninstall") ? QStringLiteral("Removing...")
                  : args.value(0) == QLatin1String("update") ? QStringLiteral("Updating...")
                  : QStringLiteral("Downloading and installing..."), -1);
    }
    connect(p, &QProcess::readyRead, this, [this, p, output, forTask] {
        const QString chunk = QString::fromUtf8(p->readAll());
        *output += chunk;
        if (!forTask)
            return;
        // "Installing 2/3… ████  45%  1.2 MB/s" and the like
        static const QRegularExpression percent(QStringLiteral("(\\d{1,3})%"));
        auto it = percent.globalMatch(chunk);
        int last = -1;
        while (it.hasNext())
            last = it.next().captured(1).toInt();
        static const QRegularExpression step(QStringLiteral("(Installing|Updating|Uninstalling) (\\d+/\\d+)"));
        const auto m = step.match(chunk);
        if (last >= 0 || m.hasMatch())
            setStatus(m.hasMatch() ? QStringLiteral("%1 part %2...").arg(m.captured(1), m.captured(2)) : QString(), last >= 0 ? qMin(last, 100) : m_percent);
    });
    connect(p, &QProcess::finished, this, [p, output, done](int code, QProcess::ExitStatus status) {
        *output += QString::fromUtf8(p->readAll());
        p->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, *output);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            done(-1, QStringLiteral("Couldn't run flatpak."));
        }
    });
    p->start();
}

// Wayland 95's own update at the top of the list
static QVariantList desktopFirst(QVariantList list)
{
    std::stable_partition(list.begin(), list.end(), [](const QVariant &v) {
        return v.toMap().value(QStringLiteral("id")).toString() == QLatin1String("wayland95");
    });
    return list;
}

void SoftwareCenter::checkUpdates(bool refresh)
{
    if (m_checking > 0)
        return;
    m_updatesChecked = true;

    if (m_packageKit) {
        ++m_checking;
        auto list = [this] {
            auto found = std::make_shared<QVariantList>();
            // installed programs' own icons
            QHash<QString, QString> icons;
            for (const QVariant &v : std::as_const(m_debPrograms))
                icons.insert(v.toMap().value(QStringLiteral("id")).toString(), v.toMap().value(QStringLiteral("icon")).toString());
            auto *t = pk(QStringLiteral("GetUpdates"), { QVariant::fromValue(quint64(FilterNone)) }, false);
            connect(t, &PkTransaction::package, this, [found, icons](uint info, const QString &id, const QString &summary) {
                if (info == InfoBlocked)
                    return;
                const QString kind = info == InfoSecurity ? QStringLiteral("security")
                                   : info == InfoImportant ? QStringLiteral("important")
                                   : info == InfoBugfix ? QStringLiteral("bugfix")
                                   : info == InfoEnhancement ? QStringLiteral("enhancement")
                                   : info == InfoLow ? QStringLiteral("low") : QStringLiteral("normal");
                *found << QVariantMap{
                    { QStringLiteral("key"), QStringLiteral("deb:") + idName(id) + QLatin1Char(':') + idArch(id) },
                    { QStringLiteral("source"), QStringLiteral("deb") },
                    { QStringLiteral("id"), idName(id) },
                    { QStringLiteral("packageId"), id },
                    { QStringLiteral("name"), idName(id) },
                    { QStringLiteral("version"), idVersion(id) },
                    { QStringLiteral("summary"), summary },
                    { QStringLiteral("kind"), kind },
                    { QStringLiteral("icon"), icons.value(idName(id)) },
                };
            });
            connect(t, &PkTransaction::finished, this, [this, found](bool) {
                m_debUpdates = *found;
                m_updates = desktopFirst(m_debUpdates + m_flatpakUpdates);
                --m_checking;
                emit updatesChanged();
            });
        };
        if (refresh) {
            // fetch the latest lists first (no password needed for this)
            auto *t = pk(QStringLiteral("RefreshCache"), { false }, false);
            connect(t, &PkTransaction::finished, this, [list](bool) { list(); });
        } else {
            list();
        }
    }

    if (flatpak()) {
        for (const QString &where : { QStringLiteral("--user"), QStringLiteral("--system") }) {
            ++m_checking;
            if (where == QLatin1String("--user"))
                m_flatpakUpdates.clear();
            runFlatpak({ QStringLiteral("remote-ls"), where, QStringLiteral("--updates"), QStringLiteral("--app"),
                         QStringLiteral("--columns=application,name,version") },
                       false, [this, where](int code, const QString &out) {
                if (code == 0) {
                    for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                        const QStringList f = line.split(QLatin1Char('\t'));
                        if (f.size() < 2)
                            continue;
                        const QString id = f.at(0).trimmed();
                        m_flatpakUpdates << QVariantMap{
                            { QStringLiteral("key"), QStringLiteral("flatpak:") + id + where },
                            { QStringLiteral("source"), QStringLiteral("flatpak") },
                            { QStringLiteral("id"), id },
                            { QStringLiteral("installation"), where.mid(2) },
                            { QStringLiteral("name"), f.at(1).trimmed().isEmpty() ? id : f.at(1).trimmed() },
                            { QStringLiteral("version"), f.value(2).trimmed() },
                            { QStringLiteral("summary"), QString() },
                            { QStringLiteral("kind"), QStringLiteral("normal") },
                            { QStringLiteral("icon"), iconUrl(id) },
                        };
                    }
                }
                m_updates = desktopFirst(m_debUpdates + m_flatpakUpdates);
                --m_checking;
                emit updatesChanged();
            });
        }
    }
    emit updatesChanged();
}

void SoftwareCenter::setUpFlathub()
{
    if (!flatpak() || m_busy)
        return;
    runTask(QStringLiteral("Setting up Flathub"), {
        [this] {
            runFlatpak({ QStringLiteral("remote-add"), QStringLiteral("--user"), QStringLiteral("--if-not-exists"), QStringLiteral("flathub"),
                         QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo") },
                       true, [this](int code, const QString &out) {
                code == 0 ? nextStep() : taskFailed(out.trimmed().section(QLatin1Char('\n'), -1));
            });
        },
        [this] {
            setStatus(QStringLiteral("Downloading the list of apps..."), -1);
            runFlatpak({ QStringLiteral("update"), QStringLiteral("--user"), QStringLiteral("--appstream"), QStringLiteral("flathub") },
                       true, [this](int code, const QString &out) {
                if (code != 0) {
                    taskFailed(out.trimmed().section(QLatin1Char('\n'), -1));
                    return;
                }
                loadCatalogAsync();
                nextStep();
            });
        },
    });
}
