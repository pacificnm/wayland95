#include "services.h"
#include <QMap>
#include "software.h" // PkTransaction

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJSEngine>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <memory>
#include <sqlite3.h>
#include <unistd.h>
#include <pwd.h>

namespace {

// W95_SERVICES_DEBUG: what's found, logged (for the tests)
bool debug() { return qEnvironmentVariableIsSet("W95_SERVICES_DEBUG"); }

// PackageKit's enums (pk-enum.h)
constexpr quint64 FilterInstalled = 1 << 2;
constexpr quint64 FilterNotInstalled = 1 << 3;
constexpr quint64 FlagOnlyTrusted = 1 << 1;

QString idName(const QString &id) { return id.section(QLatin1Char(';'), 0, 0); }
QString idArch(const QString &id) { return id.section(QLatin1Char(';'), 2, 2); }

bool nativeArch(const QString &arch)
{
    static const QStringList native = [] {
        const QString cpu = QSysInfo::currentCpuArchitecture();
        if (cpu == QLatin1String("x86_64"))
            return QStringList{ QStringLiteral("amd64"), QStringLiteral("x86_64") };
        if (cpu == QLatin1String("arm64"))
            return QStringList{ QStringLiteral("arm64"), QStringLiteral("aarch64") };
        return QStringList{ cpu };
    }();
    return arch.isEmpty() || native.contains(arch) || arch == QLatin1String("all") || arch == QLatin1String("noarch");
}

QString pkStatusText(uint status)
{
    switch (status) {
    case 1: return QStringLiteral("Waiting...");
    case 4: case 5: case 17: return QStringLiteral("Getting information...");
    case 6: return QStringLiteral("Removing...");
    case 8: return QStringLiteral("Downloading...");
    case 9: return QStringLiteral("Installing...");
    case 11: return QStringLiteral("Cleaning up...");
    case 13: return QStringLiteral("Finding what else is needed...");
    case 14: return QStringLiteral("Checking signatures...");
    case 16: return QStringLiteral("Making changes...");
    case 30: return QStringLiteral("Waiting for another program that's installing software...");
    case 31: return QStringLiteral("Waiting for permission...");
    case 36: return QStringLiteral("Running setup scripts...");
    default: return QString();
    }
}

// which of `names` are installed, by dpkg's database or PackageKit
void queryInstalled(QObject *context, const QStringList &names, std::function<void(QSet<QString>)> done)
{
    const QString dpkg = QStandardPaths::findExecutable(QStringLiteral("dpkg-query"));
    if (!dpkg.isEmpty()) {
        auto *p = new QProcess(context);
        QObject::connect(p, &QProcess::finished, context, [p, done] {
            QSet<QString> found;
            const QStringList lines = QString::fromUtf8(p->readAllStandardOutput()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                const QString name = line.section(QLatin1Char('\t'), 0, 0).section(QLatin1Char(':'), 0, 0);
                if (line.section(QLatin1Char('\t'), 1).startsWith(QLatin1String("ii")))
                    found.insert(name);
            }
            p->deleteLater();
            done(found);
        });
        // exits 1 when some names aren't known at all; the rest still print
        p->start(dpkg, QStringList{ QStringLiteral("-W"), QStringLiteral("-f=${Package}\t${db:Status-Abbrev}\n") } + names);
        return;
    }
    auto found = std::make_shared<QSet<QString>>();
    auto *t = new PkTransaction(context);
    QObject::connect(t, &PkTransaction::package, context, [found](uint, const QString &id, const QString &) {
        found->insert(idName(id));
    });
    QObject::connect(t, &PkTransaction::finished, context, [found, done](bool, const QString &) { done(*found); });
    t->start(QStringLiteral("Resolve"), { QVariant::fromValue(FilterInstalled), names });
}

// "key value" or "key = value" config lines (sshd, PostgreSQL)
QString unquote(QString v)
{
    v = v.trimmed();
    const int hash = v.indexOf(QLatin1Char('#'));
    if (hash >= 0 && !v.startsWith(QLatin1Char('\'')))
        v = v.left(hash).trimmed();
    if (v.size() >= 2 && v.startsWith(QLatin1Char('\''))) {
        const int end = v.indexOf(QLatin1Char('\''), 1);
        return v.mid(1, end > 0 ? end - 1 : -1);
    }
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
        return v.mid(1, v.size() - 2);
    return v;
}

QStringList globFiles(const QString &pattern)
{
    const QFileInfo fi(pattern);
    const QStringList names = QDir(fi.path()).entryList({ fi.fileName() }, QDir::Files, QDir::Name);
    QStringList out;
    for (const QString &n : names)
        out << fi.path() + QLatin1Char('/') + n;
    return out;
}

// sshd: the first value of each keyword wins, Include is read where it
// stands, and Match blocks (for some users only) are left out
void readSshd(const QString &path, QVariantMap &out, int depth = 0)
{
    QFile f(path);
    if (depth > 4 || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        static const QRegularExpression split(QStringLiteral("^(\\S+)\\s*(?:=\\s*|\\s+)(.*)$"));
        const auto m = split.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1).toLower();
        const QString value = unquote(m.captured(2));
        if (key == QLatin1String("match"))
            return;
        if (key == QLatin1String("include")) {
            for (QString pattern : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (!pattern.startsWith(QLatin1Char('/')))
                    pattern = QStringLiteral("/etc/ssh/") + pattern;
                for (const QString &file : globFiles(pattern))
                    readSshd(file, out, depth + 1);
            }
            continue;
        }
        if (!out.contains(key))
            out.insert(key, value);
    }
}

// what building Wayland 95 needs: debian/control's Build-Depends (CMake
// reads them, apps/control/CMakeLists.txt), and the compilers and git
// that it takes for granted
QStringList buildDepends()
{
    QStringList list{ QStringLiteral("build-essential"), QStringLiteral("git") };
#ifdef WAYLAND95_BUILD_DEPENDS
    for (const QString &p : QStringLiteral(WAYLAND95_BUILD_DEPENDS).split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (!list.contains(p))
            list << p;
#endif
    return list;
}

QString terminalProgram()
{
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/win95terminal");
    return QFileInfo::exists(local) ? local : QStringLiteral("win95terminal");
}

} // namespace

ServiceManager::ServiceManager(QObject *parent)
    : QObject(parent)
{
    m_defs = {
        { QStringLiteral("ssh"), QStringLiteral("SSH Server"), QStringLiteral("server"),
          QStringLiteral("Lets people log on to this computer from other computers over the network, and copy "
                         "files to and from it (ssh, scp, sftp)."),
          QStringLiteral("ssh"), QStringLiteral("SshSettings.qml"),
          { QStringLiteral("openssh-server") }, {}, {} },
        { QStringLiteral("postgresql"), QStringLiteral("PostgreSQL"), QStringLiteral("server"),
          QStringLiteral("A database server. Programs on this computer, or on others if you allow it, keep "
                         "their data in its databases."),
          QStringLiteral("postgresql"), QStringLiteral("PostgresSettings.qml"),
          { QStringLiteral("postgresql") }, {}, {} },
        { QStringLiteral("mysql"), QStringLiteral("MySQL (MariaDB)"), QStringLiteral("server"),
          QStringLiteral("A MySQL database server (MariaDB, Debian's MySQL: the same mysql commands and "
                         "connections). Programs here, or on others if you allow it, keep their data in it."),
          QStringLiteral("mariadb"), QStringLiteral("MysqlSettings.qml"),
          { QStringLiteral("mariadb-server") }, {}, {} },
        { QStringLiteral("nginx"), QStringLiteral("nginx Web Server"), QStringLiteral("server"),
          QStringLiteral("A web server: shows a folder's pages to web browsers on this computer, or on others "
                         "if you allow it. Settings chooses the folder."),
          QStringLiteral("nginx"), QStringLiteral("NginxSettings.qml"),
          { QStringLiteral("nginx") }, {}, {} },
        { QStringLiteral("samba"), QStringLiteral("Samba File Sharing"), QStringLiteral("server"),
          QStringLiteral("Shares folders with other computers on the network: Windows (\\\\computer\\folder), "
                         "Macs and Linux. Settings chooses the folders and who may use them."),
          QStringLiteral("smbd"), QStringLiteral("SambaSettings.qml"),
          { QStringLiteral("samba") }, {}, {} },
        { QStringLiteral("wireguard"), QStringLiteral("WireGuard VPN"), QStringLiteral("server"),
          QStringLiteral("Private, encrypted tunnels to other computers and networks (a VPN). Each tunnel has "
                         "its own keys and peers; Settings shows the ones this computer has."),
          QStringLiteral("wireguard"), QStringLiteral("WireGuardSettings.qml"),
          { QStringLiteral("wireguard-tools") }, {}, {} },
        { QStringLiteral("python"), QStringLiteral("Python"), QStringLiteral("language"),
          QStringLiteral("The Python programming language, with pip (its package installer) and virtual "
                         "environments for each project's packages."),
          QString(), QStringLiteral("PythonSettings.qml"),
          { QStringLiteral("python3"), QStringLiteral("python3-pip"), QStringLiteral("python3-venv") },
          { QStringLiteral("python3-pip"), QStringLiteral("python3-venv") },
          { QStringLiteral("/usr/bin/python3"), QStringLiteral("--version") } },
        { QStringLiteral("nodejs"), QStringLiteral("Node.js"), QStringLiteral("language"),
          QStringLiteral("Runs JavaScript programs outside a web browser, with npm to install their packages."),
          QString(), QStringLiteral("NodeSettings.qml"),
          { QStringLiteral("nodejs") },
          { QStringLiteral("nodejs"), QStringLiteral("npm") },
          { QStringLiteral("/usr/bin/node"), QStringLiteral("--version") } },
        { QStringLiteral("sqlite"), QStringLiteral("SQLite"), QStringLiteral("tool"),
          QStringLiteral("A database kept in a single file, with no server to run. Installs the sqlite3 command; "
                         "Settings lists your database files and what's in them."),
          QString(), QStringLiteral("SqliteSettings.qml"),
          { QStringLiteral("sqlite3") },
          { QStringLiteral("sqlite3"), QStringLiteral("sqlitebrowser") },
          { QStringLiteral("/usr/bin/sqlite3"), QStringLiteral("--version") } },
        { QStringLiteral("wayland95dev"), QStringLiteral("Wayland 95 Development"), QStringLiteral("tool"),
          QStringLiteral("Everything needed to build Wayland 95 itself from its source: compilers, CMake, git, and "
                         "Qt 6's and wlroots' development files. Settings gets the source and builds it."),
          QString(), QStringLiteral("DevSettings.qml"),
          buildDepends(),
          // turning it off takes away only the development files: compilers,
          // CMake and the rest may be wanted for other things, and some
          // (fonts, GRUB's tools) the desktop and the boot menu need
          buildDepends().filter(QRegularExpression(QStringLiteral("-dev(-tools)?$"))),
          {} },
        // not in Debian: installed from ollama.com by wayland95-services
        // ollama-install (no packages; it's there when its program is)
        { QStringLiteral("ollama"), QStringLiteral("Ollama (AI Models)"), QStringLiteral("server"),
          QStringLiteral("Runs AI language models on this computer, for programs such as Athenaeum: nothing is "
                         "sent to the internet. Installed from ollama.com; Settings gets and removes models."),
          QStringLiteral("ollama"), QStringLiteral("OllamaSettings.qml"),
          {}, {},
          { QStringLiteral("/usr/local/bin/ollama"), QStringLiteral("--version") } },
    };
    refresh();
}

QString ServiceManager::userName() const
{
    const passwd *pw = getpwuid(getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : qEnvironmentVariable("USER");
}

QString ServiceManager::homePath() const { return QDir::homePath(); }

const ServiceManager::Def *ServiceManager::def(const QString &id) const
{
    for (const Def &d : m_defs)
        if (d.id == id)
            return &d;
    return nullptr;
}

QString ServiceManager::helperPath() const
{
#ifdef WAYLAND95_SERVICES_HELPER
    if (QFileInfo::exists(QStringLiteral(WAYLAND95_SERVICES_HELPER)))
        return QStringLiteral(WAYLAND95_SERVICES_HELPER);
#endif
#ifdef WAYLAND95_SERVICES_HELPER_SRC
    // not installed yet: the copy in the source tree
    return QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC);
#else
    return QString();
#endif
}

// ---------------------------------------------------------------------------
// What's installed and running

void ServiceManager::refresh()
{
    QStringList packages;
    QStringList units;
    for (const Def &d : m_defs) {
        packages << d.packages << d.removable;
        if (d.id == QLatin1String("ssh"))
            units << QStringLiteral("ssh.service") << QStringLiteral("ssh.socket");
        else if (d.id == QLatin1String("wireguard"))
            continue; // (a unit for each tunnel: wireguardTunnels())
        else if (!d.unit.isEmpty())
            units << d.unit + QStringLiteral(".service");
    }
    packages.removeDuplicates();

    m_loading += 2;
    emit servicesChanged();

    queryInstalled(this, packages, [this](QSet<QString> found) {
        m_installed = found;
        --m_loading;
        publish();
        // versions of the languages that are there
        for (const Def &d : std::as_const(m_defs)) {
            if (d.version.isEmpty() || !QFileInfo::exists(d.version.first())) {
                m_versions.remove(d.id);
                continue;
            }
            ++m_loading;
            auto *p = new QProcess(this);
            const QString id = d.id;
            connect(p, &QProcess::finished, this, [this, p, id] {
                QString v = QString::fromUtf8(p->readAllStandardOutput() + p->readAllStandardError()).trimmed();
                // the first version number: "Python 3.13.5", "v22.1.0",
                // "3.46.1 2024-08-13 ..." -> 3.13.5, 22.1.0, 3.46.1
                static const QRegularExpression number(QStringLiteral("\\d+(?:\\.\\d+)+"));
                // (or a later line: ollama --version warns first when its server isn't running)
                const QString first = number.match(v.section(QLatin1Char('\n'), 0, 0)).captured(0);
                v = first.isEmpty() ? number.match(v).captured(0) : first;
                m_versions.insert(id, v);
                p->deleteLater();
                --m_loading;
                publish();
            });
            connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart) {
                    p->deleteLater();
                    --m_loading;
                    publish();
                }
            });
            p->start(d.version.first(), d.version.mid(1));
        }
    });

    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        m_unitState.clear();
        const QStringList blocks = QString::fromUtf8(p->readAllStandardOutput()).split(QStringLiteral("\n\n"));
        for (const QString &block : blocks) {
            QVariantMap m;
            for (const QString &line : block.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                m.insert(line.section(QLatin1Char('='), 0, 0), line.section(QLatin1Char('='), 1));
            if (m.contains(QStringLiteral("Id")))
                m_unitState.insert(m.value(QStringLiteral("Id")).toString(), m);
        }
        p->deleteLater();
        --m_loading;
        publish();
    });
    p->start(QStringLiteral("systemctl"), QStringList{ QStringLiteral("show"), QStringLiteral("--no-pager"),
                                                       QStringLiteral("-p"), QStringLiteral("Id,LoadState,ActiveState,UnitFileState") } + units);
}

void ServiceManager::publish()
{
    QVariantList list;
    for (const Def &d : std::as_const(m_defs)) {
        bool installed = !m_installed.isEmpty();
        for (const QString &p : d.packages)
            installed = installed && m_installed.contains(p);
        if (d.packages.isEmpty()) // not from a package (Ollama): there when its program is
            installed = !d.version.isEmpty() && QFileInfo::exists(d.version.first());

        bool enabled = installed, active = false;
        QString status;
        if (d.id == QLatin1String("wireguard")) {
            // its tunnels: on if any starts with the computer, running if
            // any is up (their names are no secret; their files are root's)
            QStringList up, starting;
            const QVariantList tunnels = wireguardTunnels();
            for (const QVariant &v : tunnels) {
                const QVariantMap t = v.toMap();
                if (t.value(QStringLiteral("active")).toBool())
                    up << t.value(QStringLiteral("name")).toString();
                if (t.value(QStringLiteral("enabled")).toBool())
                    starting << t.value(QStringLiteral("name")).toString();
            }
            enabled = installed && !starting.isEmpty();
            active = installed && !up.isEmpty();
            status = !installed ? QStringLiteral("Not installed")
                   : active ? QStringLiteral("Connected (") + up.join(QStringLiteral(", ")) + QLatin1Char(')')
                   : enabled ? QStringLiteral("Stopped")
                   : QStringLiteral("Off");
        } else if (d.kind == QLatin1String("server")) {
            const QStringList units = d.id == QLatin1String("ssh")
                    ? QStringList{ QStringLiteral("ssh.service"), QStringLiteral("ssh.socket") }
                    : QStringList{ d.unit + QStringLiteral(".service") };
            enabled = false;
            for (const QString &u : units) {
                const QVariantMap s = m_unitState.value(u);
                if (s.value(QStringLiteral("LoadState")) != QLatin1String("loaded"))
                    continue;
                enabled = enabled || s.value(QStringLiteral("UnitFileState")).toString().startsWith(QLatin1String("enabled"));
                active = active || s.value(QStringLiteral("ActiveState")) == QLatin1String("active");
            }
            enabled = installed && enabled;
            active = installed && active;
            status = !installed ? QStringLiteral("Not installed")
                   : active ? QStringLiteral("Running")
                   : enabled ? QStringLiteral("Stopped")
                   : QStringLiteral("Off");
        } else {
            bool some = false;
            for (const QString &p : d.packages)
                some = some || m_installed.contains(p);
            status = installed ? QStringLiteral("Installed") : some ? QStringLiteral("Partly installed") : QStringLiteral("Not installed");
        }
        // (a tool's settings work without it: SQLite's look at files)
        const bool available = d.kind == QLatin1String("tool") || (d.version.isEmpty() ? installed : QFileInfo::exists(d.version.first()));
        const QString version = available ? m_versions.value(d.id) : QString();
        if (installed && !version.isEmpty())
            status = QStringLiteral("Version ") + version;

        list << QVariantMap{
            { QStringLiteral("id"), d.id },
            { QStringLiteral("name"), d.name },
            { QStringLiteral("kind"), d.kind },
            { QStringLiteral("description"), d.description },
            { QStringLiteral("packages"), d.packages },
            { QStringLiteral("installed"), installed },
            // its settings can be shown (a language that's partly there)
            { QStringLiteral("available"), available },
            { QStringLiteral("enabled"), enabled },
            { QStringLiteral("active"), active },
            { QStringLiteral("version"), version },
            { QStringLiteral("status"), status },
            { QStringLiteral("settings"), d.settings },
        };
    }
    m_services = list;
    if (debug())
        for (const QVariant &v : std::as_const(list))
            qInfo("services: %s %s", qPrintable(v.toMap().value(QStringLiteral("id")).toString()),
                  qPrintable(v.toMap().value(QStringLiteral("status")).toString()));
    emit servicesChanged();
}

QVariantMap ServiceManager::service(const QString &id) const
{
    for (const QVariant &v : m_services)
        if (v.toMap().value(QStringLiteral("id")) == id)
            return v.toMap();
    return {};
}

void ServiceManager::installedPackages(const QStringList &names, const QJSValue &done)
{
    queryInstalled(this, names, [this, done](QSet<QString> found) {
        QStringList list(found.begin(), found.end());
        QJSValue cb = done;
        if (QJSEngine *e = qjsEngine(this))
            cb.call({ e->toScriptValue(list) });
    });
}

// ---------------------------------------------------------------------------
// Turning things on and off

void ServiceManager::turnOn(const QString &id)
{
    const Def *d = def(id);
    if (!d || m_busy)
        return;
    QList<Step> steps;
    if (!d->packages.isEmpty())
        steps << installStep(d->packages);
    if (id == QLatin1String("ollama") && !QFileInfo::exists(d->version.first())) {
        // from ollama.com (curl and zstd fetch and unpack it), with progress
        steps << installStep({ QStringLiteral("curl"), QStringLiteral("zstd") })
              << adminStep({ QStringLiteral("ollama-install") }, QStringLiteral("Downloading Ollama..."));
    }
    if (id == QLatin1String("nodejs")) {
        // Debian's nodejs leaves npm to its own package; NodeSource's
        // includes it (and conflicts with Debian's)
        steps << [this] {
            if (QFileInfo::exists(QStringLiteral("/usr/bin/npm")))
                nextStep();
            else
                installStep({ QStringLiteral("npm") })();
        };
    }
    if (id == QLatin1String("nginx")) {
        // (setfacl: how nginx gets to read a website folder in your home)
        steps << [this] {
            if (QFileInfo::exists(QStringLiteral("/usr/bin/setfacl")))
                nextStep();
            else
                installStep({ QStringLiteral("acl") })();
        };
    }
    QString done;
    if (d->kind == QLatin1String("server")) {
        steps << adminStep({ QStringLiteral("unit"), QStringLiteral("enable"), d->unit }, QStringLiteral("Starting ") + d->name + QStringLiteral("..."));
        done = id == QLatin1String("wireguard")
                ? QStringLiteral("WireGuard is on: its tunnels connect now and whenever the computer starts. "
                                 "(If it has none yet, add one in Settings.)")
                : id == QLatin1String("samba")
                ? QStringLiteral("Samba is on. It starts whenever the computer starts.\n\n"
                                 "Choose the folders to share in Settings.")
                : id == QLatin1String("ollama")
                ? QStringLiteral("Ollama is on. It starts whenever the computer starts.\n\n"
                                 "Get models to run in Settings (Athenaeum needs qwen3.5:2b and nomic-embed-text).")
                : d->name + QStringLiteral(" is on. It starts whenever the computer starts.");
    } else {
        done = d->name + QStringLiteral(" is installed.");
    }
    runTask(QStringLiteral("Turning on ") + d->name, steps, done);
}

void ServiceManager::turnOff(const QString &id)
{
    const Def *d = def(id);
    if (!d || m_busy)
        return;
    if (d->kind == QLatin1String("server")) {
        runTask(QStringLiteral("Turning off ") + d->name,
                { adminStep({ QStringLiteral("unit"), QStringLiteral("disable"), d->unit }, QStringLiteral("Stopping ") + d->name + QStringLiteral("...")) },
                d->name + QStringLiteral(" is off. It's still installed; turn it on again any time."));
    } else {
        QStringList remove;
        for (const QString &p : d->removable)
            if (m_installed.contains(p))
                remove << p;
        runTask(QStringLiteral("Removing ") + d->name, { removeStep(remove) }, d->name + QStringLiteral(" has been removed."));
    }
}

void ServiceManager::control(const QString &id, const QString &action)
{
    const Def *d = def(id);
    if (!d || m_busy || d->unit.isEmpty())
        return;
    const QString verb = action == QLatin1String("stop") ? QStringLiteral("Stopping ")
                       : action == QLatin1String("restart") ? QStringLiteral("Restarting ")
                       : QStringLiteral("Starting ");
    runTask(verb + d->name, { adminStep({ QStringLiteral("unit"), action, d->unit }, verb + d->name + QStringLiteral("...")) }, QString());
}

void ServiceManager::changePackages(const QString &title, const QStringList &install, const QStringList &remove)
{
    if (m_busy)
        return;
    QList<Step> steps;
    if (!install.isEmpty())
        steps << installStep(install);
    if (!remove.isEmpty())
        steps << removeStep(remove);
    runTask(title, steps, QString());
}

void ServiceManager::runTask(const QString &title, const QList<Step> &steps, const QString &doneMessage)
{
    m_busy = true;
    m_task = title;
    m_status = QStringLiteral("Starting...");
    m_percent = -1;
    m_steps = steps;
    m_doneMessage = doneMessage;
    emit taskChanged();
    nextStep();
}

void ServiceManager::nextStep()
{
    if (!m_busy)
        return;
    if (m_steps.isEmpty()) {
        m_busy = false;
        m_current = nullptr;
        emit taskChanged();
        refresh();
        emit taskFinished(true, m_doneMessage);
        return;
    }
    const Step step = m_steps.takeFirst();
    step();
}

void ServiceManager::taskFailed(const QString &error)
{
    m_steps.clear();
    m_busy = false;
    m_current = nullptr;
    emit taskChanged();
    refresh();
    emit taskFinished(false, error.isEmpty() ? QStringLiteral("Something went wrong.") : error);
}

void ServiceManager::setStatus(const QString &status, int percent)
{
    if (!status.isEmpty())
        m_status = status;
    m_percent = percent;
    emit taskChanged();
}

PkTransaction *ServiceManager::pk(const QString &method, const QVariantList &args, bool interactive)
{
    auto *t = new PkTransaction(this);
    connect(t, &PkTransaction::progress, this, [this](int percent, uint status) {
        setStatus(pkStatusText(status), percent);
    });
    m_current = t;
    t->start(method, args, interactive);
    return t;
}

ServiceManager::Step ServiceManager::installStep(const QStringList &packages)
{
    return [this, packages] {
        setStatus(QStringLiteral("Looking for ") + packages.join(QStringLiteral(", ")) + QStringLiteral("..."), -1);
        // which packages exactly (this computer's kind), of those not there yet
        auto ids = std::make_shared<QHash<QString, QString>>();
        auto *resolve = pk(QStringLiteral("Resolve"), { QVariant::fromValue(FilterNotInstalled), packages }, false);
        connect(resolve, &PkTransaction::package, this, [ids](uint, const QString &id, const QString &) {
            if (nativeArch(idArch(id)) || !ids->contains(idName(id)))
                ids->insert(idName(id), id);
        });
        connect(resolve, &PkTransaction::finished, this, [this, ids, packages](bool ok, const QString &error) {
            if (!ok) {
                taskFailed(error);
                return;
            }
            if (ids->isEmpty()) {
                // nothing to do, or it can't be found
                queryInstalled(this, packages, [this, packages](QSet<QString> found) {
                    QStringList missing;
                    for (const QString &p : packages)
                        if (!found.contains(p))
                            missing << p;
                    if (missing.isEmpty())
                        nextStep();
                    else
                        taskFailed(missing.join(QStringLiteral(", "))
                                   + QStringLiteral(" isn't available from this computer's software sources. "
                                                    "Try Add/Remove Programs > Updates > Check Now, then try again."));
                });
                return;
            }
            setStatus(QStringLiteral("Installing ") + QStringList(ids->keys()).join(QStringLiteral(", ")) + QStringLiteral("..."), -1);
            auto *t = pk(QStringLiteral("InstallPackages"), { QVariant::fromValue(FlagOnlyTrusted), QStringList(ids->values()) }, true);
            connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) { ok ? nextStep() : taskFailed(error); });
        });
    };
}

ServiceManager::Step ServiceManager::removeStep(const QStringList &packages)
{
    return [this, packages] {
        if (packages.isEmpty()) {
            nextStep();
            return;
        }
        auto ids = std::make_shared<QStringList>();
        auto *resolve = pk(QStringLiteral("Resolve"), { QVariant::fromValue(FilterInstalled), packages }, false);
        connect(resolve, &PkTransaction::package, this, [ids](uint, const QString &id, const QString &) { *ids << id; });
        connect(resolve, &PkTransaction::finished, this, [this, ids](bool ok, const QString &error) {
            if (!ok) {
                taskFailed(error);
                return;
            }
            if (ids->isEmpty()) {
                nextStep();
                return;
            }
            setStatus(QStringLiteral("Removing..."), -1);
            // allow_deps false: if other programs need them, PackageKit
            // refuses rather than removing those too; autoremove: the
            // packages that came along only for these go as well
            auto *t = pk(QStringLiteral("RemovePackages"), { QVariant::fromValue(quint64(0)), *ids, false, true }, true);
            connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) {
                ok ? nextStep() : taskFailed(error.contains(QLatin1String("depend"), Qt::CaseInsensitive)
                                             ? QStringLiteral("Other installed programs need it, so it was left installed.\n\n") + error
                                             : error);
            });
        });
    };
}

ServiceManager::Step ServiceManager::adminStep(const QStringList &args, const QString &status)
{
    return [this, args, status] {
        setStatus(status, -1);
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        const QString helper = helperPath();
        if (pkexec.isEmpty()) {
            taskFailed(QStringLiteral("pkexec isn't installed, so Services can't change system settings (sudo apt install pkexec)."));
            return;
        }
        auto *p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        // "percent N" and "status TEXT" lines (ollama-install's download) move the
        // progress bar; the rest is kept, for what went wrong
        auto rest = std::make_shared<QString>();
        connect(p, &QProcess::readyRead, this, [this, p, rest] {
            while (p->canReadLine()) {
                const QString line = QString::fromUtf8(p->readLine()).trimmed();
                if (line.startsWith(QLatin1String("percent ")))
                    setStatus(QString(), line.mid(8).toInt());
                else if (line.startsWith(QLatin1String("status ")))
                    setStatus(line.mid(7), m_percent);
                else
                    *rest += line + QLatin1Char('\n');
            }
        });
        connect(p, &QProcess::finished, this, [this, p, rest](int code, QProcess::ExitStatus st) {
            const QString out = (*rest + QString::fromUtf8(p->readAll())).trimmed();
            p->deleteLater();
            if (st == QProcess::NormalExit && code == 0)
                nextStep();
            else if (code == 126 || code == 127)
                taskFailed(QStringLiteral("You need an administrator's password to change this."));
            else
                taskFailed(out);
        });
        p->start(pkexec, QStringList{ helper } + args);
        p->closeWriteChannel();
    };
}

void ServiceManager::admin(const QStringList &args, const QString &input, const QJSValue &done)
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QJSValue cb = done;
    if (pkexec.isEmpty()) {
        cb.call({ false, QStringLiteral("pkexec isn't installed, so Services can't change system settings (sudo apt install pkexec).") });
        return;
    }
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [p, cb](int code, QProcess::ExitStatus st) mutable {
        QString out = QString::fromUtf8(p->readAll()).trimmed();
        p->deleteLater();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (code == 126 || code == 127)
            out = QStringLiteral("You need an administrator's password to change this.");
        cb.call({ ok, out });
    });
    if (debug())
        qInfo("services: admin %s (%s)", qPrintable(args.join(QLatin1Char(' '))), qPrintable(pkexec));
    p->start(pkexec, QStringList{ helperPath() } + args);
    QByteArray data = input.toUtf8();
    if (!data.isEmpty() && !data.endsWith('\n'))
        data += '\n';
    p->write(data);
    p->closeWriteChannel();
}

void ServiceManager::run(const QString &program, const QStringList &args, const QString &input,
                         const QJSValue &done, const QString &workdir)
{
    auto *p = new QProcess(this);
    if (!workdir.isEmpty())
        p->setWorkingDirectory(workdir);
    if (program.startsWith(QLatin1String("/usr/bin/"))) {
        // the system's programs with the system's helpers: /usr/bin/npm
        // starts "env node", which would otherwise find a Node.js the user
        // installed for themselves (nvm), with its own folders
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin:") + env.value(QStringLiteral("PATH")));
        for (const QString &k : env.keys())
            if (k.startsWith(QLatin1String("NVM_")) || k.startsWith(QLatin1String("npm_config_"), Qt::CaseInsensitive))
                env.remove(k);
        p->setProcessEnvironment(env);
    }
    QJSValue cb = done;
    auto finished = std::make_shared<bool>(false);
    connect(p, &QProcess::finished, this, [p, cb, finished](int code, QProcess::ExitStatus st) mutable {
        *finished = true;
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        if (cb.isCallable())
            cb.call({ st == QProcess::NormalExit ? code : -1, out, err });
    });
    connect(p, &QProcess::errorOccurred, this, [p, cb, finished, program](QProcess::ProcessError e) mutable {
        if (e != QProcess::FailedToStart || *finished)
            return;
        *finished = true;
        p->deleteLater();
        if (cb.isCallable())
            cb.call({ -1, QString(), program + QStringLiteral(" couldn't be started.") });
    });
    p->start(program, args);
    if (!input.isEmpty())
        p->write(input.toUtf8());
    p->closeWriteChannel();
}

void ServiceManager::openTerminal(const QString &workdir, const QStringList &command)
{
    QStringList args;
    if (!workdir.isEmpty())
        args << QStringLiteral("--workdir") << workdir;
    if (!command.isEmpty())
        args << QStringLiteral("-e") << command;
    QProcess::startDetached(terminalProgram(), args);
}

// ---------------------------------------------------------------------------
// Files and settings

static QString expandHome(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString ServiceManager::readFile(const QString &path) const
{
    QFile f(expandHome(path));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool ServiceManager::writeFile(const QString &path, const QString &text, bool privateFile)
{
    const QString full = expandHome(path);
    const QString dir = QFileInfo(full).absolutePath();
    if (!QDir().exists(dir)) {
        if (!QDir().mkpath(dir))
            return false;
        if (privateFile)
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QSaveFile f(full);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(text.toUtf8());
    if (privateFile)
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return f.commit();
}

bool ServiceManager::exists(const QString &path) const { return QFileInfo::exists(expandHome(path)); }

QStringList ServiceManager::listDirs(const QString &path) const
{
    return QDir(expandHome(path)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
}

bool ServiceManager::removeDir(const QString &path)
{
    // only folders of the user's own, and never the home folder itself
    const QString full = QFileInfo(expandHome(path)).canonicalFilePath();
    const QString home = QFileInfo(QDir::homePath()).canonicalFilePath();
    if (full.isEmpty() || !full.startsWith(home + QLatin1Char('/')))
        return false;
    return QDir(full).removeRecursively();
}

QStringList ServiceManager::addresses() const
{
    // real network cards only: not loopback, nor the bridges containers
    // and virtual machines make (docker0, virbr0, veth...)
    QStringList out;
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &i : all) {
        const auto f = i.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack))
            continue;
        static const QRegularExpression virt(QStringLiteral("^(docker|br-|veth|virbr|vnet|lxc|lxd|podman|cni|flannel)"));
        if (virt.match(i.name()).hasMatch())
            continue;
        for (const QNetworkAddressEntry &e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (!a.isLoopback() && !a.isLinkLocal())
                out << a.toString();
        }
    }
    return out;
}

QString ServiceManager::hostName() const { return QHostInfo::localHostName(); }

// WireGuard's tunnels as anyone may see them: those set to start with the
// computer (systemd's links) and those that are up (their interfaces).
// One that's neither only shows once wayland95-services lists the files.
QVariantList ServiceManager::wireguardTunnels() const
{
    QMap<QString, QVariantMap> found;
    const QDir wants(QStringLiteral("/etc/systemd/system/multi-user.target.wants"));
    const QStringList links = wants.entryList({ QStringLiteral("wg-quick@*.service") }, QDir::Files | QDir::System);
    for (const QString &link : links) {
        const QString name = link.mid(9, link.size() - 9 - 8); // wg-quick@NAME.service
        found[name][QStringLiteral("enabled")] = true;
    }
    const QStringList interfaces = QDir(QStringLiteral("/sys/class/net")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : interfaces) {
        QFile uevent(QStringLiteral("/sys/class/net/") + name + QStringLiteral("/uevent"));
        if (uevent.open(QIODevice::ReadOnly) && uevent.readAll().contains("DEVTYPE=wireguard"))
            found[name][QStringLiteral("active")] = true;
    }
    QVariantList list;
    for (auto it = found.cbegin(); it != found.cend(); ++it) {
        QVariantMap t = it.value();
        t[QStringLiteral("name")] = it.key();
        t[QStringLiteral("enabled")] = t.value(QStringLiteral("enabled")).toBool();
        t[QStringLiteral("active")] = t.value(QStringLiteral("active")).toBool();
        list << t;
    }
    return list;
}

// ------------------------------------------------------------ login screen

// W95_FAKE_ROOT: tests/services/run.sh's scratch copy of /etc and
// /usr/share/sddm, so the Log On tab can be tried without changing the
// computer (only ever read here)
static QString systemPath(const QString &path)
{
    static const QString root = qEnvironmentVariable("W95_FAKE_ROOT");
    return root + path;
}

QVariantMap ServiceManager::loginScreen() const
{
    QVariantMap out;

    // which login screen the computer starts
    QString dm;
    const QFileInfo link(systemPath(QStringLiteral("/etc/systemd/system/display-manager.service")));
    if (link.isSymLink())
        dm = QFileInfo(link.symLinkTarget()).completeBaseName();
    if (dm.isEmpty()) {
        QFile f(systemPath(QStringLiteral("/etc/X11/default-display-manager")));
        if (f.open(QIODevice::ReadOnly))
            dm = QFileInfo(QString::fromUtf8(f.readAll()).trimmed()).fileName();
    }
    out.insert(QStringLiteral("displayManager"), dm);

    const QString themes = systemPath(QStringLiteral("/usr/share/sddm/themes"));
    out.insert(QStringLiteral("installed"), QFileInfo::exists(themes + QStringLiteral("/wayland95/Main.qml")));
    out.insert(QStringLiteral("preview"),
               QUrl::fromLocalFile(themes + QStringLiteral("/wayland95/preview.png")).toString());

    // SDDM's settings: /usr/lib/sddm/sddm.conf.d, then /etc/sddm.conf.d (each
    // in name order), then /etc/sddm.conf; a later [Theme] Current= wins
    QStringList files;
    for (const QString &dir : { QStringLiteral("/usr/lib/sddm/sddm.conf.d"), QStringLiteral("/etc/sddm.conf.d") }) {
        const QDir d(systemPath(dir));
        for (const QString &name : d.entryList({ QStringLiteral("*.conf") }, QDir::Files | QDir::Readable, QDir::Name))
            files << d.filePath(name);
    }
    files << systemPath(QStringLiteral("/etc/sddm.conf"));

    const QString ourFile = systemPath(QStringLiteral("/etc/sddm.conf.d/wayland95.conf"));
    // the theme the files choose (and the file that chose it), with or
    // without ours
    auto chosen = [&files, &ourFile](bool withOurs, QString *from) {
        QString theme;
        for (const QString &path : files) {
            if (!withOurs && path == ourFile)
                continue;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            bool inTheme = false;
            const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
            for (QString line : lines) {
                line = line.trimmed();
                const int eq = line.indexOf(QLatin1Char('='));
                if (line.startsWith(QLatin1Char('['))) {
                    inTheme = line == QLatin1String("[Theme]");
                } else if (inTheme && eq > 0 && line.left(eq).trimmed() == QLatin1String("Current")) {
                    theme = line.mid(eq + 1).trimmed();
                    if (from)
                        *from = path;
                }
            }
        }
        return theme;
    };
    // a theme's own name (metadata.desktop's Name=) and picture; SDDM's
    // default when none is chosen is Debian's (debian-theme)
    auto describe = [&themes](const QString &theme, const QString &prefix, QVariantMap &into) {
        const QString dir = themes + QLatin1Char('/') + (theme.isEmpty() ? QStringLiteral("debian-theme") : theme);
        QString name = theme, picture;
        QFile meta(dir + QStringLiteral("/metadata.desktop"));
        if (meta.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList lines = QString::fromUtf8(meta.readAll()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.startsWith(QLatin1String("Name=")) && name == theme)
                    name = line.mid(5).trimmed();
                else if (line.startsWith(QLatin1String("Screenshot=")))
                    picture = dir + QLatin1Char('/') + line.mid(11).trimmed();
            }
        }
        into.insert(prefix + QStringLiteral("Name"), name);
        into.insert(prefix + QStringLiteral("Preview"),
                    !picture.isEmpty() && QFileInfo::exists(picture) ? QUrl::fromLocalFile(picture).toString() : QString());
    };

    QString from;
    const QString current = chosen(true, &from);
    const QString without = chosen(false, nullptr);
    const bool hasOurFile = QFileInfo::exists(ourFile);
    out.insert(QStringLiteral("current"), current);
    out.insert(QStringLiteral("ours"), current == QLatin1String("wayland95"));
    out.insert(QStringLiteral("ourFile"), hasOurFile);
    out.insert(QStringLiteral("other"), without == QLatin1String("wayland95") ? QString() : without);
    describe(current, QStringLiteral("current"), out);
    describe(without == QLatin1String("wayland95") ? QString() : without, QStringLiteral("other"), out);

    QString overridden;
    if (hasOurFile && from != ourFile) {
        overridden = from;
        if (const QString root = qEnvironmentVariable("W95_FAKE_ROOT"); !root.isEmpty() && overridden.startsWith(root))
            overridden = overridden.mid(root.size());
    }
    out.insert(QStringLiteral("overriddenBy"), overridden);
    return out;
}

QVariantMap ServiceManager::sshConfig() const
{
    QVariantMap raw;
    readSshd(QStringLiteral("/etc/ssh/sshd_config"), raw);
    auto get = [&raw](const char *key, const char *def) {
        const QString v = raw.value(QString::fromLatin1(key).toLower()).toString();
        return v.isEmpty() ? QString::fromLatin1(def) : v;
    };
    QString root = get("PermitRootLogin", "prohibit-password");
    if (root == QLatin1String("without-password"))
        root = QStringLiteral("prohibit-password");
    return {
        { QStringLiteral("Port"), get("Port", "22").toInt() },
        { QStringLiteral("PasswordAuthentication"), get("PasswordAuthentication", "yes") == QLatin1String("yes") },
        { QStringLiteral("PubkeyAuthentication"), get("PubkeyAuthentication", "yes") == QLatin1String("yes") },
        { QStringLiteral("X11Forwarding"), get("X11Forwarding", "no") == QLatin1String("yes") },
        { QStringLiteral("PermitRootLogin"), root },
        { QStringLiteral("AllowUsers"), get("AllowUsers", "") },
    };
}

QVariantMap ServiceManager::postgresConfig() const
{
    QProcess p;
    p.start(QStringLiteral("pg_lsclusters"), { QStringLiteral("-h") });
    if (!p.waitForFinished(5000))
        return {};
    // "17 main 5432 online postgres /var/lib/postgresql/17/main /var/log/..."
    QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return {};
    std::sort(lines.begin(), lines.end(), [](const QString &a, const QString &b) {
        return a.section(QLatin1Char(' '), 0, 0).toDouble() < b.section(QLatin1Char(' '), 0, 0).toDouble();
    });
    const QStringList f = lines.last().simplified().split(QLatin1Char(' '));
    const QString dir = QStringLiteral("/etc/postgresql/%1/%2").arg(f.value(0), f.value(1));

    QString listen = QStringLiteral("localhost");
    int maxConnections = 100;
    bool remote = false;
    QStringList files{ dir + QStringLiteral("/postgresql.conf") };
    files << globFiles(dir + QStringLiteral("/conf.d/*.conf"));
    for (const QString &file : std::as_const(files)) {
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!in.atEnd()) {
            const QString line = QString::fromUtf8(in.readLine()).trimmed();
            if (line == QLatin1String("# wayland95: remote=yes"))
                remote = true;
            if (line.startsWith(QLatin1Char('#')) || !line.contains(QLatin1Char('=')))
                continue;
            const QString key = line.section(QLatin1Char('='), 0, 0).trimmed();
            const QString value = unquote(line.section(QLatin1Char('='), 1));
            if (key == QLatin1String("listen_addresses"))
                listen = value;
            else if (key == QLatin1String("max_connections"))
                maxConnections = value.toInt();
        }
    }
    const bool local = listen.isEmpty() || listen == QLatin1String("localhost")
            || listen == QLatin1String("127.0.0.1") || listen == QLatin1String("::1");
    return {
        { QStringLiteral("version"), f.value(0) },
        { QStringLiteral("cluster"), f.value(1) },
        { QStringLiteral("port"), f.value(2).toInt() },
        { QStringLiteral("online"), f.value(3) == QLatin1String("online") },
        { QStringLiteral("listen"), local ? QStringLiteral("localhost") : QStringLiteral("all") },
        { QStringLiteral("max_connections"), maxConnections },
        { QStringLiteral("remote"), remote && !local },
    };
}

// ---------------------------------------------------------------------------
// MySQL (MariaDB)

QVariantMap ServiceManager::mysqlConfig() const
{
    if (QStandardPaths::findExecutable(QStringLiteral("mariadb")).isEmpty())
        return {};
    // the server's options as it reads them: my.cnf, then the folders it
    // includes, each in name order; the last value wins
    QStringList files{ QStringLiteral("/etc/mysql/my.cnf") };
    for (const QString &dir : { QStringLiteral("/etc/mysql/conf.d"), QStringLiteral("/etc/mysql/mariadb.conf.d") })
        files << globFiles(dir + QStringLiteral("/*.cnf"));
    QString bind = QStringLiteral("127.0.0.1"), port = QStringLiteral("3306"), maxConnections = QStringLiteral("151");
    for (const QString &file : std::as_const(files)) {
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        bool server = false;
        while (!in.atEnd()) {
            const QString line = QString::fromUtf8(in.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                static const QSet<QString> sections{ QStringLiteral("[mysqld]"), QStringLiteral("[mariadb]"), QStringLiteral("[server]"),
                                                     QStringLiteral("[mariadbd]") };
                server = sections.contains(line.toLower());
                continue;
            }
            if (!server || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')) || !line.contains(QLatin1Char('=')))
                continue;
            const QString key = line.section(QLatin1Char('='), 0, 0).trimmed().replace(QLatin1Char('_'), QLatin1Char('-'));
            const QString value = unquote(line.section(QLatin1Char('='), 1));
            if (key == QLatin1String("bind-address"))
                bind = value;
            else if (key == QLatin1String("port"))
                port = value;
            else if (key == QLatin1String("max-connections"))
                maxConnections = value;
        }
    }
    QString version;
    QProcess p;
    p.start(QStringLiteral("mariadb"), { QStringLiteral("--version") });
    if (p.waitForFinished(5000)) {
        static const QRegularExpression v(QStringLiteral("(\\d+\\.\\d+\\.\\d+)-MariaDB"));
        version = v.match(QString::fromUtf8(p.readAllStandardOutput())).captured(1);
    }
    const bool local = bind.isEmpty() || bind == QLatin1String("127.0.0.1") || bind == QLatin1String("localhost")
            || bind == QLatin1String("::1");
    return {
        { QStringLiteral("version"), version },
        { QStringLiteral("port"), port.toInt() > 0 ? port.toInt() : 3306 },
        { QStringLiteral("listen"), local ? QStringLiteral("localhost") : QStringLiteral("all") },
        { QStringLiteral("max_connections"), maxConnections.toInt() > 0 ? maxConnections.toInt() : 151 },
    };
}

// ---------------------------------------------------------------------------
// SQLite: database files (looked at read-only, with SQLite's own library)

namespace {
bool isSqliteFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && f.read(16) == QByteArray("SQLite format 3\0", 16);
}
}

QVariantList ServiceManager::sqliteFiles(const QString &folder) const
{
    QVariantList out;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : entries) {
        if (fi.size() > 0 && !isSqliteFile(fi.filePath()))
            continue;
        // (an empty file is a database too, if it's named like one)
        static const QStringList exts{ QStringLiteral("db"), QStringLiteral("sqlite"), QStringLiteral("sqlite3"), QStringLiteral("db3") };
        if (fi.size() == 0 && !exts.contains(fi.suffix().toLower()))
            continue;
        out << QVariantMap{
            { QStringLiteral("path"), fi.filePath() },
            { QStringLiteral("name"), fi.fileName() },
            { QStringLiteral("size"), fi.size() },
            { QStringLiteral("modified"), fi.lastModified() },
        };
    }
    if (debug()) {
        QStringList names;
        for (const QVariant &v : std::as_const(out))
            names << v.toMap().value(QStringLiteral("name")).toString();
        qInfo("services: sqlite files %s", qPrintable(names.join(QLatin1Char(' '))));
    }
    return out;
}

QVariantMap ServiceManager::sqliteInfo(const QString &path) const
{
    sqlite3 *db = nullptr;
    // read-only: looking never changes it (nor makes a journal)
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded() + "?mode=ro";
    if (sqlite3_open_v2(uri.constData(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        const QString e = QString::fromUtf8(db ? sqlite3_errmsg(db) : "can't open it");
        sqlite3_close(db);
        return { { QStringLiteral("error"), e } };
    }
    sqlite3_busy_timeout(db, 1000);
    QVariantList tables;
    QString error;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name, type FROM sqlite_schema WHERE type IN ('table','view') "
                               "AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY type, name", -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const QString name = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 0)));
            const QString type = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 1)));
            qint64 rows = -1;
            if (type == QLatin1String("table")) {
                QString quoted = name;
                quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                const QByteArray q = QStringLiteral("SELECT count(*) FROM \"%1\"").arg(quoted).toUtf8();
                sqlite3_stmt *c = nullptr;
                if (sqlite3_prepare_v2(db, q.constData(), -1, &c, nullptr) == SQLITE_OK && sqlite3_step(c) == SQLITE_ROW)
                    rows = sqlite3_column_int64(c, 0);
                sqlite3_finalize(c);
            }
            tables << QVariantMap{ { QStringLiteral("name"), name }, { QStringLiteral("type"), type },
                                   { QStringLiteral("rows"), rows } };
        }
    } else {
        error = QString::fromUtf8(sqlite3_errmsg(db));
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    QVariantMap out{ { QStringLiteral("tables"), tables } };
    if (!error.isEmpty())
        out.insert(QStringLiteral("error"), error);
    if (debug()) {
        QStringList t;
        for (const QVariant &v : std::as_const(tables)) {
            const QVariantMap m = v.toMap();
            t << m.value(QStringLiteral("name")).toString() + QLatin1Char('=')
                 + (m.value(QStringLiteral("type")) == QLatin1String("view") ? QStringLiteral("view") : m.value(QStringLiteral("rows")).toString());
        }
        qInfo("services: sqlite info %s %s%s", qPrintable(QFileInfo(path).fileName()), qPrintable(t.join(QLatin1Char(' '))),
              qPrintable(error.isEmpty() ? QString() : QStringLiteral(" error ") + error));
    }
    return out;
}

QString ServiceManager::sqliteCreate(const QString &path)
{
    if (QFileInfo::exists(path))
        return QStringLiteral("There's already a file called %1.").arg(QFileInfo(path).fileName());
    QDir().mkpath(QFileInfo(path).absolutePath());
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(QFile::encodeName(path).constData(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const QString e = QString::fromUtf8(db ? sqlite3_errmsg(db) : "can't make it");
        sqlite3_close(db);
        return e;
    }
    // (VACUUM writes its first page: a database file others recognise)
    char *msg = nullptr;
    QString error;
    if (sqlite3_exec(db, "VACUUM", nullptr, nullptr, &msg) != SQLITE_OK)
        error = QString::fromUtf8(msg ? msg : "it couldn't be written");
    sqlite3_free(msg);
    sqlite3_close(db);
    return error;
}

bool ServiceManager::moveToTrash(const QString &path)
{
    // (Qt won't make the Recycle Bin's folders when they aren't there yet)
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    QDir().mkpath(trash + QStringLiteral("/files"));
    QDir().mkpath(trash + QStringLiteral("/info"));
    return QFile::moveToTrash(path);
}

// ---------------------------------------------------------------------------
// nginx

QVariantMap ServiceManager::nginxConfig() const
{
    if (QStandardPaths::findExecutable(QStringLiteral("nginx"), { QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin") }).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("nginx")).isEmpty())
        return {};
    QVariantMap out{
        { QStringLiteral("listen"), QStringLiteral("all") },
        { QStringLiteral("port"), 80 },
        { QStringLiteral("root"), QStringLiteral("/var/www/html") },
        { QStringLiteral("autoindex"), false },
        { QStringLiteral("ours"), false },
    };
    // ours: its first line says what it was written with
    QFile f(QStringLiteral("/etc/nginx/sites-enabled/wayland95"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (!line.startsWith(QLatin1String("# wayland95: ")))
                continue;
            out.insert(QStringLiteral("ours"), true);
            for (const QString &kv : line.mid(13).split(QLatin1Char('\t'))) {
                const QString key = kv.section(QLatin1Char('='), 0, 0), value = kv.section(QLatin1Char('='), 1);
                if (key == QLatin1String("listen") || key == QLatin1String("root"))
                    out.insert(key, value);
                else if (key == QLatin1String("port"))
                    out.insert(key, value.toInt());
                else if (key == QLatin1String("autoindex"))
                    out.insert(key, value == QLatin1String("yes"));
            }
            break;
        }
    }
    const QFileInfo root(out.value(QStringLiteral("root")).toString());
    out.insert(QStringLiteral("rootExists"), root.isDir());
    out.insert(QStringLiteral("rootWritable"), root.isDir() && root.isWritable());
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral("nginx"), { QStringLiteral("-v") });
    if (p.waitForFinished(5000)) {
        static const QRegularExpression v(QStringLiteral("nginx/(\\d+(?:\\.\\d+)+)"));
        out.insert(QStringLiteral("version"), v.match(QString::fromUtf8(p.readAll())).captured(1));
    }
    return out;
}

QString ServiceManager::makeWebsite()
{
    const QString dir = QDir::homePath() + QStringLiteral("/Website");
    QDir().mkpath(dir);
    const QString index = dir + QStringLiteral("/index.html");
    if (!QFileInfo::exists(index)) {
        QSaveFile f(index);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QStringLiteral(
                "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>My Website</title>\n</head>\n<body>\n"
                "<h1>It works!</h1>\n<p>This page is %1/index.html. Change it, or add more pages beside it.</p>\n"
                "</body>\n</html>\n").arg(dir.toHtmlEscaped()).toUtf8());
            f.commit();
        }
    }
    return dir;
}

void ServiceManager::openUrl(const QString &url) const
{
    QDesktopServices::openUrl(QUrl(url));
}

// ---------------------------------------------------------------------------
// Samba

QVariantMap ServiceManager::sambaConfig() const
{
    const QString conf = systemPath(QStringLiteral("/etc/samba/smb.conf"));
    const QString ours = systemPath(QStringLiteral("/etc/samba/wayland95.conf"));
    if (!QFileInfo::exists(conf))
        return {};
    // smb.conf's [global] values, with ours read where it's included (the
    // last value wins); the shares are the ones in ours
    QString workgroup = QStringLiteral("WORKGROUP"), description = QStringLiteral("Samba %v");
    bool included = false;
    QVariantList shares;
    const QString me = userName();
    std::function<void(const QString &, bool)> read = [&](const QString &path, bool isOurs) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        QString section;
        QVariantMap share;
        auto flush = [&] {
            if (isOurs && !share.isEmpty() && !share.value(QStringLiteral("owner")).toString().isEmpty()) {
                share.insert(QStringLiteral("mine"), share.value(QStringLiteral("owner")) == me);
                shares << share;
            }
            share.clear();
        };
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                flush();
                section = line.mid(1, line.size() - 2).trimmed();
                if (section.compare(QLatin1String("global"), Qt::CaseInsensitive) != 0) {
                    // (defaults, as Samba has them)
                    share = { { QStringLiteral("name"), section }, { QStringLiteral("comment"), QString() },
                              { QStringLiteral("access"), QStringLiteral("read") }, { QStringLiteral("who"), QStringLiteral("me") } };
                }
                continue;
            }
            if (line.startsWith(QLatin1String("# wayland95: owner="))) {
                if (!share.isEmpty())
                    share.insert(QStringLiteral("owner"), line.mid(19).trimmed());
                continue;
            }
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')) || !line.contains(QLatin1Char('=')))
                continue;
            const QString key = line.section(QLatin1Char('='), 0, 0).simplified().toLower();
            const QString value = line.section(QLatin1Char('='), 1).trimmed();
            if (section.isEmpty() || section.compare(QLatin1String("global"), Qt::CaseInsensitive) == 0) {
                if (key == QLatin1String("workgroup"))
                    workgroup = value;
                else if (key == QLatin1String("server string"))
                    description = value;
                else if (key == QLatin1String("include") && !isOurs && value == ours) {
                    included = true;
                    read(ours, true);
                }
            } else if (!share.isEmpty()) {
                const bool yes = value.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0;
                if (key == QLatin1String("path"))
                    share.insert(QStringLiteral("path"), value);
                else if (key == QLatin1String("comment"))
                    share.insert(QStringLiteral("comment"), value);
                else if (key == QLatin1String("read only"))
                    share.insert(QStringLiteral("access"), yes ? QStringLiteral("read") : QStringLiteral("full"));
                else if (key == QLatin1String("guest ok"))
                    share.insert(QStringLiteral("who"), yes ? QStringLiteral("anyone") : QStringLiteral("me"));
            }
        }
        flush();
    };
    read(conf, false);

    QVariantMap out{
        { QStringLiteral("workgroup"), workgroup },
        { QStringLiteral("description"), description },
        { QStringLiteral("shares"), shares },
        { QStringLiteral("ours"), included },
    };
    QProcess p;
    p.start(QStringLiteral("/usr/sbin/smbd"), { QStringLiteral("--version") });
    if (p.waitForFinished(5000)) {
        static const QRegularExpression v(QStringLiteral("(\\d+\\.\\d+\\.\\d+)"));
        out.insert(QStringLiteral("version"), v.match(QString::fromUtf8(p.readAllStandardOutput())).captured(1));
    }
    if (debug()) {
        QStringList names;
        for (const QVariant &s : std::as_const(shares))
            names << s.toMap().value(QStringLiteral("name")).toString();
        qInfo("services: samba %s \"%s\" shares %s", qPrintable(workgroup), qPrintable(description), qPrintable(names.join(QLatin1Char(','))));
    }
    return out;
}

void ServiceManager::adminTask(const QString &title, const QStringList &args, const QString &status, const QString &doneMessage)
{
    if (m_busy)
        return;
    runTask(title, { adminStep(args, status) }, doneMessage);
}
