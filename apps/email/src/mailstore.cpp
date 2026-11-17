#include "mailstore.h"

#include "secrets.h"
#include "smtp.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {

QString configPath()
{
    return qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config")) + QStringLiteral("/wayland95/email.json");
}

QString cacheRoot()
{
    return qEnvironmentVariable("XDG_CACHE_HOME", QDir::homePath() + QStringLiteral("/.cache")) + QStringLiteral("/wayland95/email");
}

bool writePrivate(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile::setPermissions(cacheRoot(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(data);
    return f.commit();
}

QString kindOf(const Imap::Folder &f)
{
    const QString n = f.name.section(QString::fromLatin1(f.delimiter.isEmpty() ? "/" : f.delimiter), -1).toLower();
    if (f.name.compare(QLatin1String("INBOX"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("inbox");
    for (const QByteArray &a : f.attributes) {
        if (a == "\\sent") return QStringLiteral("sent");
        if (a == "\\drafts") return QStringLiteral("drafts");
        if (a == "\\trash") return QStringLiteral("trash");
        if (a == "\\junk") return QStringLiteral("junk");
    }
    if (n == QLatin1String("sent") || n == QLatin1String("sent items") || n == QLatin1String("sent messages") || n == QLatin1String("sent mail"))
        return QStringLiteral("sent");
    if (n == QLatin1String("drafts"))
        return QStringLiteral("drafts");
    if (n == QLatin1String("trash") || n == QLatin1String("deleted items") || n == QLatin1String("deleted messages") || n == QLatin1String("bin"))
        return QStringLiteral("trash");
    if (n == QLatin1String("junk") || n == QLatin1String("spam") || n == QLatin1String("junk e-mail") || n == QLatin1String("bulk mail"))
        return QStringLiteral("junk");
    return QStringLiteral("other");
}

QString quoteText(const QString &text)
{
    QStringList out;
    for (const QString &l : text.split(QLatin1Char('\n')))
        out << QStringLiteral("> ") + l;
    return out.join(QLatin1Char('\n'));
}

QString strippedSubject(const QString &s, const QString &prefix)
{
    static const QRegularExpression re(QStringLiteral("^\\s*(re|fw|fwd|aw|sv)\\s*:\\s*"), QRegularExpression::CaseInsensitiveOption);
    QString t = s;
    while (re.match(t).hasMatch())
        t.remove(re);
    return prefix + t;
}

} // namespace

MailStore::MailStore(QObject *parent)
    : QObject(parent)
{
    loadSettings();
    rebuildFolders();
    connect(&m_check, &QTimer::timeout, this, &MailStore::refresh);
    setCheckMinutes(m_checkMinutes);
    // the first account's Inbox, as it was, then brought up to date
    if (!m_accounts.isEmpty()) {
        const QString id = m_accounts.first().toMap().value(QStringLiteral("id")).toString();
        QTimer::singleShot(0, this, [this, id] {
            selectFolder(keyFor(id, "INBOX"));
            refresh();
        });
    }
}

// --------------------------------------------------------------- settings

void MailStore::loadSettings()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_accounts = o.value(QStringLiteral("accounts")).toArray().toVariantList();
    m_checkMinutes = o.value(QStringLiteral("checkMinutes")).toInt(10);
    for (const QVariant &v : std::as_const(m_accounts)) {
        Account a;
        a.settings = v.toMap();
        m_state.insert(a.id(), a);
        // the folders it had, until the server says
        QFile cache(cacheRoot() + QLatin1Char('/') + a.id() + QStringLiteral("/folders.json"));
        if (cache.open(QIODevice::ReadOnly)) {
            for (const QJsonValue &fv : QJsonDocument::fromJson(cache.readAll()).array()) {
                const QJsonObject fo = fv.toObject();
                Imap::Folder folder;
                folder.raw = fo.value(QStringLiteral("raw")).toString().toLatin1();
                folder.name = Imap::decodeFolderName(folder.raw);
                folder.delimiter = fo.value(QStringLiteral("delimiter")).toString().toLatin1();
                for (const QJsonValue &at : fo.value(QStringLiteral("attributes")).toArray())
                    folder.attributes << at.toString().toLatin1();
                m_state[a.id()].serverFolders << folder;
            }
        }
    }
}

void MailStore::saveSettings()
{
    QJsonObject o;
    o.insert(QStringLiteral("accounts"), QJsonArray::fromVariantList(m_accounts));
    o.insert(QStringLiteral("checkMinutes"), m_checkMinutes);
    writePrivate(configPath(), QJsonDocument(o).toJson());
}

void MailStore::setCheckMinutes(int minutes)
{
    m_checkMinutes = qBound(0, minutes, 24 * 60);
    if (m_checkMinutes > 0)
        m_check.start(m_checkMinutes * 60 * 1000);
    else
        m_check.stop();
    saveSettings();
    emit settingsChanged();
}

MailStore::Account *MailStore::account(const QString &id)
{
    auto it = m_state.find(id);
    return it == m_state.end() ? nullptr : &it.value();
}

QString MailStore::homePath() const
{
    return QDir::homePath();
}

QVariantMap MailStore::serversFor(const QString &email) const
{
    const QString domain = email.section(QLatin1Char('@'), 1).toLower();
    struct P { const char *domains; const char *imap; int imapPort; const char *smtp; int smtpPort; const char *smtpSec; };
    static const P providers[] = {
        {"gmail.com googlemail.com", "imap.gmail.com", 993, "smtp.gmail.com", 465, "ssl"},
        {"outlook.com hotmail.com live.com msn.com", "outlook.office365.com", 993, "smtp-mail.outlook.com", 587, "starttls"},
        {"yahoo.com ymail.com", "imap.mail.yahoo.com", 993, "smtp.mail.yahoo.com", 465, "ssl"},
        {"icloud.com me.com mac.com", "imap.mail.me.com", 993, "smtp.mail.me.com", 587, "starttls"},
        {"fastmail.com fastmail.fm", "imap.fastmail.com", 993, "smtp.fastmail.com", 465, "ssl"},
        {"aol.com", "imap.aol.com", 993, "smtp.aol.com", 465, "ssl"},
        {"gmx.com gmx.net gmx.de", "imap.gmx.com", 993, "mail.gmx.com", 587, "starttls"},
        {"zoho.com", "imap.zoho.com", 993, "smtp.zoho.com", 465, "ssl"},
        {"proton.me protonmail.com", "127.0.0.1", 1143, "127.0.0.1", 1025, "starttls"},
    };
    for (const P &p : providers) {
        if (!QString::fromLatin1(p.domains).split(QLatin1Char(' ')).contains(domain))
            continue;
        QVariantMap m;
        m[QStringLiteral("imapHost")] = QString::fromLatin1(p.imap);
        m[QStringLiteral("imapPort")] = p.imapPort;
        m[QStringLiteral("imapSecurity")] = p.imapPort == 993 ? QStringLiteral("ssl") : QStringLiteral("starttls");
        m[QStringLiteral("smtpHost")] = QString::fromLatin1(p.smtp);
        m[QStringLiteral("smtpPort")] = p.smtpPort;
        m[QStringLiteral("smtpSecurity")] = QString::fromLatin1(p.smtpSec);
        // Gmail and friends want an app password (with 2-step verification on)
        if (domain.contains(QLatin1String("gmail")) || domain.contains(QLatin1String("yahoo")) || domain.contains(QLatin1String("icloud"))
            || domain.contains(QLatin1String("aol")) || domain == QLatin1String("me.com") || domain == QLatin1String("mac.com"))
            m[QStringLiteral("note")] = QStringLiteral("Use an app password (made in your account's security settings), not your usual password.");
        if (domain.contains(QLatin1String("proton")))
            m[QStringLiteral("note")] = QStringLiteral("Proton Mail needs Proton Mail Bridge running on this computer.");
        if (domain == QLatin1String("outlook.com") || domain == QLatin1String("hotmail.com") || domain == QLatin1String("live.com"))
            m[QStringLiteral("note")] = QStringLiteral("Microsoft may only allow modern sign-in for these accounts; if your password isn't accepted, it can't be used here yet.");
        return m;
    }
    if (domain.isEmpty())
        return {};
    // the usual names
    return {{QStringLiteral("imapHost"), QStringLiteral("imap.") + domain}, {QStringLiteral("imapPort"), 993},
            {QStringLiteral("imapSecurity"), QStringLiteral("ssl")}, {QStringLiteral("smtpHost"), QStringLiteral("smtp.") + domain},
            {QStringLiteral("smtpPort"), 587}, {QStringLiteral("smtpSecurity"), QStringLiteral("starttls")}, {QStringLiteral("guess"), true}};
}

// --------------------------------------------------------------- accounts

void MailStore::saveAccount(const QVariantMap &input, const QString &password, bool remember, const QJSValue &done)
{
    QVariantMap settings = input;
    QString id = settings.value(QStringLiteral("id")).toString();
    const bool isNew = id.isEmpty();
    if (isNew) {
        id = QUuid::createUuid().toString(QUuid::Id128).left(12);
        settings[QStringLiteral("id")] = id;
    }
    if (settings.value(QStringLiteral("user")).toString().isEmpty())
        settings[QStringLiteral("user")] = settings.value(QStringLiteral("email"));
    QJSValue cb = done;
    auto answer = [cb](bool ok, const QString &err) mutable {
        if (cb.isCallable())
            cb.call({ok, err});
    };
    // a new password (or new servers): checked by logging in before it's kept
    Account probe;
    probe.settings = settings;
    QString pw = password;
    if (pw.isEmpty() && !isNew && m_state.contains(id))
        pw = m_state.value(id).password;
    if (pw.isEmpty()) {
        answer(false, QStringLiteral("Type the password."));
        return;
    }
    work(1);
    setStatus(QStringLiteral("Connecting to %1...").arg(settings.value(QStringLiteral("imapHost")).toString()));
    auto *imap = new Imap(this);
    imap->open(settings.value(QStringLiteral("imapHost")).toString(), settings.value(QStringLiteral("imapPort")).toInt(),
               settings.value(QStringLiteral("imapSecurity")).toString(), settings.value(QStringLiteral("user")).toString(), pw,
               [this, imap, settings, id, pw, remember, isNew, answer](bool ok, const QString &err) mutable {
        work(-1);
        if (!ok) {
            imap->deleteLater();
            setStatus(QString());
            answer(false, err);
            return;
        }
        // it works: kept, and this connection used from now on
        bool replaced = false;
        for (QVariant &v : m_accounts) {
            if (v.toMap().value(QStringLiteral("id")) == id) {
                v = settings;
                replaced = true;
            }
        }
        if (!replaced)
            m_accounts << settings;
        Account &a = m_state[id];
        if (a.imap)
            a.imap->deleteLater();
        a.settings = settings;
        a.imap = imap;
        a.password = pw;
        a.selected.clear();
        connect(imap, &Imap::lost, this, [this, id](const QString &) {
            if (Account *acc = account(id)) {
                acc->imap = nullptr;
                acc->selected.clear();
            }
        });
        saveSettings();
        emit accountsChanged();
        if (remember) {
            Secrets::store(id, QStringLiteral("Email: %1").arg(settings.value(QStringLiteral("email")).toString()), pw,
                           [this](bool stored, const QString &why) {
                if (!stored)
                    emit error(QStringLiteral("Email"), why + QStringLiteral(" Email will ask for it each time it starts."));
            });
        }
        answer(true, QString());
        syncFolders(id);
        if (isNew || m_folder.isEmpty())
            selectFolder(keyFor(id, "INBOX"));
    });
}

void MailStore::removeAccount(const QString &id)
{
    Secrets::remove(id);
    if (Account *a = account(id); a && a->imap)
        a->imap->deleteLater();
    m_state.remove(id);
    for (int i = int(m_accounts.size()) - 1; i >= 0; --i)
        if (m_accounts.at(i).toMap().value(QStringLiteral("id")) == id)
            m_accounts.removeAt(i);
    QDir(cacheRoot() + QLatin1Char('/') + id).removeRecursively();
    saveSettings();
    emit accountsChanged();
    rebuildFolders();
    QString acct;
    splitKey(m_folder, &acct, nullptr);
    if (acct == id) {
        m_folder.clear();
        m_messages.clear();
        m_message.clear();
        emit folderChanged();
        emit messagesChanged();
        emit messageChanged();
    }
}

void MailStore::withPassword(const QString &accountId, const std::function<void(bool ok, const QString &password)> &then)
{
    Account *a = account(accountId);
    if (!a) {
        then(false, QString());
        return;
    }
    if (!a->password.isEmpty()) {
        then(true, a->password);
        return;
    }
    m_passwordWaiters[accountId] << then;
    if (m_passwordWaiters.value(accountId).size() > 1)
        return; // already looking
    Secrets::lookup(accountId, [this, accountId](bool found, const QString &password) {
        Account *a = account(accountId);
        if (!a)
            return;
        if (found && !password.isEmpty()) {
            a->password = password;
            const auto waiters = m_passwordWaiters.take(accountId);
            for (const auto &w : waiters)
                w(true, password);
            return;
        }
        a->asking = true;
        emit passwordNeeded(accountId, a->settings.value(QStringLiteral("email")).toString(), QString());
    });
}

void MailStore::providePassword(const QString &accountId, const QString &password, bool remember)
{
    Account *a = account(accountId);
    if (!a)
        return;
    a->password = password;
    a->asking = false;
    // kept in the keyring once the server has taken it (connectAccount)
    a->rememberPending = remember;
    const auto waiters = m_passwordWaiters.take(accountId);
    for (const auto &w : waiters)
        w(true, password);
}

void MailStore::cancelPassword(const QString &accountId)
{
    if (Account *a = account(accountId))
        a->asking = false;
    const auto waiters = m_passwordWaiters.take(accountId);
    for (const auto &w : waiters)
        w(false, QString());
}

// ------------------------------------------------------------- connection

void MailStore::withImap(const QString &accountId, const std::function<void(Imap *, const QString &)> &then)
{
    Account *a = account(accountId);
    if (!a) {
        then(nullptr, QStringLiteral("There's no such account."));
        return;
    }
    if (a->imap && a->imap->isOpen()) {
        then(a->imap, QString());
        return;
    }
    a->waiting << then;
    if (!a->connecting)
        connectAccount(accountId);
}

void MailStore::flushWaiting(const QString &accountId, Imap *imap, const QString &error)
{
    Account *a = account(accountId);
    if (!a)
        return;
    const auto waiting = a->waiting;
    a->waiting.clear();
    for (const auto &w : waiting)
        w(imap, error);
}

void MailStore::connectAccount(const QString &accountId)
{
    Account *a = account(accountId);
    a->connecting = true;
    withPassword(accountId, [this, accountId](bool ok, const QString &password) {
        Account *a = account(accountId);
        if (!a)
            return;
        if (!ok) {
            a->connecting = false;
            flushWaiting(accountId, nullptr, QStringLiteral("No password was given."));
            return;
        }
        const QVariantMap s = a->settings;
        setStatus(QStringLiteral("Connecting to %1...").arg(s.value(QStringLiteral("imapHost")).toString()));
        work(1);
        auto *imap = new Imap(this);
        imap->open(s.value(QStringLiteral("imapHost")).toString(), s.value(QStringLiteral("imapPort")).toInt(),
                   s.value(QStringLiteral("imapSecurity")).toString(), s.value(QStringLiteral("user")).toString(), password,
                   [this, accountId, imap](bool ok, const QString &err) {
            work(-1);
            Account *a = account(accountId);
            if (!a) {
                imap->deleteLater();
                return;
            }
            a->connecting = false;
            if (!ok) {
                imap->deleteLater();
                setStatus(QStringLiteral("Not connected"));
                if (err.contains(QLatin1String("user name and password"))) {
                    // a wrong (or changed) password: asked for again (the
                    // keyring's forgotten only if it was the keyring's)
                    if (!a->rememberPending)
                        Secrets::remove(accountId);
                    a->password.clear();
                    a->asking = true;
                    a->connecting = true;
                    m_passwordWaiters[accountId] << [this, accountId](bool ok2, const QString &) {
                        Account *acc = account(accountId);
                        if (!acc)
                            return;
                        acc->connecting = false;
                        if (ok2)
                            connectAccount(accountId);
                        else
                            flushWaiting(accountId, nullptr, QStringLiteral("No password was given."));
                    };
                    emit passwordNeeded(accountId, a->settings.value(QStringLiteral("email")).toString(), err);
                    return;
                }
                flushWaiting(accountId, nullptr, err);
                return;
            }
            a->imap = imap;
            a->selected.clear();
            if (a->rememberPending) {
                a->rememberPending = false;
                Secrets::store(accountId, QStringLiteral("Email: %1").arg(a->settings.value(QStringLiteral("email")).toString()),
                               a->password, [this](bool stored, const QString &why) {
                    if (!stored)
                        emit error(QStringLiteral("Email"), why);
                });
            }
            connect(imap, &Imap::lost, this, [this, accountId](const QString &why) {
                if (Account *acc = account(accountId)) {
                    acc->imap = nullptr;
                    acc->selected.clear();
                }
                setStatus(QStringLiteral("Disconnected: %1").arg(why));
            });
            setStatus(QString());
            flushWaiting(accountId, imap, QString());
        });
    });
}

void MailStore::job(const QString &accountId, const QByteArray &folder, bool reselect,
                    const std::function<void(Imap *, const QString &, const Next &)> &run)
{
    Account *a = account(accountId);
    if (!a) {
        run(nullptr, QStringLiteral("There's no such account."), [] {});
        return;
    }
    a->jobs << Job{folder, reselect, run};
    pump(accountId);
}

void MailStore::pump(const QString &accountId)
{
    Account *a = account(accountId);
    if (!a || a->running || a->jobs.isEmpty())
        return;
    a->running = true;
    const Job j = a->jobs.takeFirst();
    work(1);
    auto next = [this, accountId]() {
        work(-1);
        if (Account *acc = account(accountId)) {
            acc->running = false;
            pump(accountId);
        }
    };
    withImap(accountId, [this, accountId, j, next](Imap *imap, const QString &err) {
        Account *a = account(accountId);
        if (!imap || !a) {
            j.run(nullptr, err, next);
            return;
        }
        if (j.folder.isEmpty() || (a->selected == j.folder && !j.reselect)) {
            j.run(imap, QString(), next);
            return;
        }
        imap->select(j.folder, [this, accountId, j, imap, next](bool ok, quint32 exists, quint32, const QString &err) {
            Account *a = account(accountId);
            if (a) {
                a->selected = ok ? j.folder : QByteArray();
                a->exists = exists;
            }
            j.run(ok ? imap : nullptr, ok ? QString() : QStringLiteral("The folder can't be opened: %1").arg(err), next);
        });
    });
}

// ---------------------------------------------------------------- folders

QString MailStore::keyFor(const QString &account, const QByteArray &raw)
{
    return account + QLatin1Char('/') + QString::fromLatin1(raw.toPercentEncoding());
}

void MailStore::splitKey(const QString &key, QString *account, QByteArray *raw)
{
    const int slash = int(key.indexOf(QLatin1Char('/')));
    if (account)
        *account = slash < 0 ? key : key.left(slash);
    if (raw)
        *raw = slash < 0 ? QByteArray() : QByteArray::fromPercentEncoding(key.mid(slash + 1).toLatin1());
}

QString MailStore::specialFolder(const QString &accountId, const QString &kind) const
{
    const auto it = m_state.constFind(accountId);
    if (it == m_state.cend())
        return {};
    for (const Imap::Folder &f : it->serverFolders)
        if (kindOf(f) == kind && !f.attributes.contains("\\noselect"))
            return QString::fromLatin1(f.raw);
    return {};
}

void MailStore::syncFolders(const QString &accountId)
{
    job(accountId, QByteArray(), false, [this, accountId](Imap *imap, const QString &err, const Next &next) {
        if (!imap) {
            setStatus(err);
            next();
            return;
        }
        imap->list([this, accountId, imap, next](bool ok, const QList<Imap::Folder> &folders, const QString &) {
            Account *a = account(accountId);
            if (!ok || !a) {
                next();
                return;
            }
            a->serverFolders = folders;
            // kept for next time
            QJsonArray arr;
            for (const Imap::Folder &f : folders) {
                QJsonArray attrs;
                for (const QByteArray &at : f.attributes)
                    attrs << QString::fromLatin1(at);
                arr << QJsonObject{{QStringLiteral("raw"), QString::fromLatin1(f.raw)},
                                   {QStringLiteral("delimiter"), QString::fromLatin1(f.delimiter)},
                                   {QStringLiteral("attributes"), attrs}};
            }
            writePrivate(cacheRoot() + QLatin1Char('/') + accountId + QStringLiteral("/folders.json"), QJsonDocument(arr).toJson());
            rebuildFolders();
            // the unread in each (not the one selected: STATUS would be wrong there)
            auto remaining = std::make_shared<int>(0);
            for (const Imap::Folder &f : folders)
                if (!f.attributes.contains("\\noselect") && !f.attributes.contains("\\nonexistent"))
                    ++*remaining;
            if (*remaining == 0) {
                next();
                return;
            }
            for (const Imap::Folder &f : folders) {
                if (f.attributes.contains("\\noselect") || f.attributes.contains("\\nonexistent"))
                    continue;
                const QString key = keyFor(accountId, f.raw);
                const bool inbox = kindOf(f) == QLatin1String("inbox");
                imap->status(f.raw, [this, key, inbox, remaining, next](bool ok, const QMap<QByteArray, quint32> &v) {
                    if (ok) {
                        m_unread[key] = int(v.value("UNSEEN"));
                        if (inbox) {
                            const quint32 uidNext = v.value("UIDNEXT");
                            const quint32 before = m_uidNext.value(key);
                            if (before && uidNext > before)
                                notifyNewMail(int(uidNext - before), QString(), QString());
                            m_uidNext[key] = uidNext;
                        }
                    }
                    if (--*remaining == 0) {
                        rebuildFolders();
                        next();
                    }
                });
            }
        });
    });
}

void MailStore::rebuildFolders()
{
    QVariantList list;
    for (const QVariant &v : std::as_const(m_accounts)) {
        const QVariantMap s = v.toMap();
        const QString id = s.value(QStringLiteral("id")).toString();
        const Account &a = m_state[id];
        list << QVariantMap{{QStringLiteral("key"), id}, {QStringLiteral("account"), id},
                            {QStringLiteral("name"), s.value(QStringLiteral("name"), s.value(QStringLiteral("email")))},
                            {QStringLiteral("depth"), 0}, {QStringLiteral("kind"), QStringLiteral("account")},
                            {QStringLiteral("unread"), 0}, {QStringLiteral("selectable"), false}};
        // Inbox, the special ones, then the rest by name (children under parents)
        QList<Imap::Folder> folders = a.serverFolders;
        if (folders.isEmpty()) {
            Imap::Folder inbox;
            inbox.raw = "INBOX";
            inbox.name = QStringLiteral("INBOX");
            folders << inbox;
        }
        static const QStringList order{QStringLiteral("inbox"), QStringLiteral("drafts"), QStringLiteral("sent"),
                                       QStringLiteral("trash"), QStringLiteral("junk")};
        std::sort(folders.begin(), folders.end(), [](const Imap::Folder &x, const Imap::Folder &y) {
            const int ox = int(order.indexOf(kindOf(x))), oy = int(order.indexOf(kindOf(y)));
            const int kx = ox < 0 ? 99 : ox, ky = oy < 0 ? 99 : oy;
            if (kx != ky)
                return kx < ky;
            return x.name.compare(y.name, Qt::CaseInsensitive) < 0;
        });
        for (const Imap::Folder &f : std::as_const(folders)) {
            const QString delim = QString::fromLatin1(f.delimiter.isEmpty() ? "/" : f.delimiter);
            const QString kind = kindOf(f);
            QString name = f.name.section(delim, -1);
            if (kind == QLatin1String("inbox")) name = QStringLiteral("Inbox");
            const int depth = 1 + (kind == QLatin1String("other") ? int(f.name.count(delim)) : 0);
            const QString key = keyFor(id, f.raw);
            list << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("account"), id}, {QStringLiteral("name"), name},
                                {QStringLiteral("depth"), depth}, {QStringLiteral("kind"), kind},
                                {QStringLiteral("unread"), m_unread.value(key)},
                                {QStringLiteral("selectable"), !f.attributes.contains("\\noselect")}};
        }
    }
    m_folders = list;
    emit foldersChanged();
}

// --------------------------------------------------------------- messages

QString MailStore::cacheDir(const QString &key) const
{
    QString acct;
    QByteArray raw;
    splitKey(key, &acct, &raw);
    return cacheRoot() + QLatin1Char('/') + acct + QLatin1Char('/') + QString::fromLatin1(raw.toHex());
}

void MailStore::cacheList(const QString &key) const
{
    if (key != m_folder)
        return;
    writePrivate(cacheDir(key) + QStringLiteral("/list.json"), QJsonDocument(QJsonArray::fromVariantList(m_messages)).toJson(QJsonDocument::Compact));
}

void MailStore::loadCachedList(const QString &key)
{
    QFile f(cacheDir(key) + QStringLiteral("/list.json"));
    m_messages = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).array().toVariantList() : QVariantList();
    emit messagesChanged();
}

QVariantMap MailStore::headerEntry(const Imap::Header &h) const
{
    const QMap<QByteArray, QByteArray> hdr = Mime::parseHeaders(h.headers);
    const QList<Mime::Address> from = Mime::parseAddresses(Mime::decodeWords(hdr.value("from")));
    const QList<Mime::Address> to = Mime::parseAddresses(Mime::decodeWords(hdr.value("to")));
    QString date = QString::fromLatin1(hdr.value("date")).trimmed();
    date.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)\\s*$")));
    const QDateTime when = QDateTime::fromString(date, Qt::RFC2822Date);
    QList<QByteArray> flags;
    for (const QByteArray &f : h.flags)
        flags << f.toLower();
    const QByteArray type = hdr.value("content-type").toLower();
    return {
        {QStringLiteral("uid"), h.uid},
        {QStringLiteral("from"), from.isEmpty() ? QString() : from.first().display()},
        {QStringLiteral("fromEmail"), from.isEmpty() ? QString() : from.first().email},
        {QStringLiteral("to"), to.isEmpty() ? QString() : to.first().display()},
        {QStringLiteral("subject"), Mime::decodeWords(hdr.value("subject"))},
        {QStringLiteral("date"), when.isValid() ? when.toMSecsSinceEpoch() : 0},
        {QStringLiteral("size"), h.size},
        {QStringLiteral("seen"), flags.contains("\\seen")},
        {QStringLiteral("flagged"), flags.contains("\\flagged")},
        {QStringLiteral("answered"), flags.contains("\\answered")},
        {QStringLiteral("attachment"), type.startsWith("multipart/mixed") || type.startsWith("multipart/related")},
    };
}

void MailStore::selectFolder(const QString &key)
{
    if (key == m_folder) {
        syncMessages(key);
        return;
    }
    m_folder = key;
    emit folderChanged();
    closeMessage();
    loadCachedList(key);
    syncMessages(key);
}

void MailStore::syncMessages(const QString &key)
{
    QString acct;
    QByteArray raw;
    splitKey(key, &acct, &raw);
    if (raw.isEmpty())
        return;
    job(acct, raw, true, [this, key, acct](Imap *imap, const QString &err, const Next &next) {
        if (!imap) {
            setStatus(err);
            next();
            return;
        }
        Account *a = account(acct);
        setStatus(QStringLiteral("Checking %1...").arg(Imap::decodeFolderName(a->selected)));
        imap->fetchHeaders(a->exists, 500, [this, key, next](bool ok, const QList<Imap::Header> &headers, const QString &err) {
            if (!ok) {
                setStatus(err);
                next();
                return;
            }
            QVariantList list;
            int unread = 0;
            for (const Imap::Header &h : headers) {
                const QVariantMap e = headerEntry(h);
                if (!e.value(QStringLiteral("seen")).toBool())
                    ++unread;
                list << e;
            }
            std::sort(list.begin(), list.end(), [](const QVariant &x, const QVariant &y) {
                return x.toMap().value(QStringLiteral("date")).toLongLong() > y.toMap().value(QStringLiteral("date")).toLongLong();
            });
            if (key == m_folder) {
                m_messages = list;
                emit messagesChanged();
                cacheList(key);
            }
            if (headers.size() < 500)
                m_unread[key] = unread;
            rebuildFolders();
            setStatus(QString());
            next();
        });
    });
}

void MailStore::refresh()
{
    for (const QVariant &v : std::as_const(m_accounts)) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        syncFolders(id);
    }
    if (!m_folder.isEmpty())
        syncMessages(m_folder);
}

void MailStore::openMessage(quint32 uid)
{
    const QString key = m_folder;
    const QString cached = cacheDir(key) + QStringLiteral("/%1.eml").arg(uid);
    QFile f(cached);
    if (f.open(QIODevice::ReadOnly)) {
        showMessage(uid, f.readAll());
    } else {
        QString acct;
        QByteArray raw;
        splitKey(key, &acct, &raw);
        m_message = {{QStringLiteral("uid"), uid}, {QStringLiteral("loading"), true}};
        emit messageChanged();
        job(acct, raw, false, [this, key, uid, cached](Imap *imap, const QString &err, const Next &next) {
            if (!imap) {
                emit error(QStringLiteral("Email"), err);
                next();
                return;
            }
            imap->fetchMessage(uid, [this, key, uid, cached, next](bool ok, const QByteArray &rawMessage, const QString &err) {
                if (ok) {
                    writePrivate(cached, rawMessage);
                    if (key == m_folder && m_message.value(QStringLiteral("uid")).toUInt() == uid)
                        showMessage(uid, rawMessage);
                } else {
                    emit error(QStringLiteral("Email"), err);
                }
                next();
            });
        });
    }
    // read now
    for (const QVariant &v : std::as_const(m_messages))
        if (v.toMap().value(QStringLiteral("uid")).toUInt() == uid && !v.toMap().value(QStringLiteral("seen")).toBool())
            setSeen({uid}, true);
}

void MailStore::showMessage(quint32 uid, const QByteArray &raw)
{
    m_raw = raw;
    m_parsed = Mime::parse(raw);
    QVariantList atts;
    for (int i = 0; i < m_parsed.attachments.size(); ++i) {
        const Mime::Attachment &a = m_parsed.attachments.at(i);
        atts << QVariantMap{{QStringLiteral("index"), i}, {QStringLiteral("name"), a.name}, {QStringLiteral("size"), a.data.size()},
                            {QStringLiteral("type"), a.type}};
    }
    m_message = {
        {QStringLiteral("uid"), uid},
        {QStringLiteral("subject"), m_parsed.subject},
        {QStringLiteral("from"), Mime::formatAddresses(m_parsed.from)},
        {QStringLiteral("to"), Mime::formatAddresses(m_parsed.to)},
        {QStringLiteral("cc"), Mime::formatAddresses(m_parsed.cc)},
        {QStringLiteral("date"), m_parsed.date.isValid() ? m_parsed.date.toMSecsSinceEpoch() : 0},
        {QStringLiteral("text"), m_parsed.text},
        {QStringLiteral("html"), m_parsed.html},
        {QStringLiteral("remoteImages"), m_parsed.htmlHadRemoteImages},
        {QStringLiteral("attachments"), atts},
    };
    emit messageChanged();
}

void MailStore::closeMessage()
{
    m_message.clear();
    m_parsed = Mime::Message();
    m_raw.clear();
    emit messageChanged();
}

QList<quint32> MailStore::toUids(const QVariantList &list)
{
    QList<quint32> out;
    for (const QVariant &v : list)
        out << v.toUInt();
    return out;
}

void MailStore::setSeen(const QVariantList &uidList, bool seen)
{
    const QList<quint32> uids = toUids(uidList);
    int changed = 0;
    for (QVariant &v : m_messages) {
        QVariantMap m = v.toMap();
        if (uids.contains(m.value(QStringLiteral("uid")).toUInt()) && m.value(QStringLiteral("seen")).toBool() != seen) {
            m[QStringLiteral("seen")] = seen;
            v = m;
            ++changed;
        }
    }
    if (!changed)
        return;
    emit messagesChanged();
    m_unread[m_folder] = qMax(0, m_unread.value(m_folder) + (seen ? -changed : changed));
    rebuildFolders();
    cacheList(m_folder);
    QString acct;
    QByteArray raw;
    splitKey(m_folder, &acct, &raw);
    job(acct, raw, false, [uids, seen](Imap *imap, const QString &, const Next &next) {
        if (!imap) {
            next();
            return;
        }
        imap->store(uids, seen, "\\Seen", [next](bool, const QString &) { next(); });
    });
}

void MailStore::setFlagged(const QVariantList &uidList, bool flagged)
{
    const QList<quint32> uids = toUids(uidList);
    for (QVariant &v : m_messages) {
        QVariantMap m = v.toMap();
        if (uids.contains(m.value(QStringLiteral("uid")).toUInt())) {
            m[QStringLiteral("flagged")] = flagged;
            v = m;
        }
    }
    emit messagesChanged();
    cacheList(m_folder);
    QString acct;
    QByteArray raw;
    splitKey(m_folder, &acct, &raw);
    job(acct, raw, false, [uids, flagged](Imap *imap, const QString &, const Next &next) {
        if (!imap) {
            next();
            return;
        }
        imap->store(uids, flagged, "\\Flagged", [next](bool, const QString &) { next(); });
    });
}

void MailStore::deleteMessages(const QVariantList &uidList)
{
    QString acct;
    QByteArray raw;
    splitKey(m_folder, &acct, &raw);
    const QString trash = specialFolder(acct, QStringLiteral("trash"));
    const bool inTrash = !trash.isEmpty() && trash.toLatin1() == raw;
    if (!inTrash && !trash.isEmpty()) {
        moveMessages(uidList, keyFor(acct, trash.toLatin1()));
        return;
    }
    // in Deleted Items (or there's none): for good
    const QList<quint32> uids = toUids(uidList);
    for (int i = int(m_messages.size()) - 1; i >= 0; --i)
        if (uids.contains(m_messages.at(i).toMap().value(QStringLiteral("uid")).toUInt()))
            m_messages.removeAt(i);
    emit messagesChanged();
    cacheList(m_folder);
    if (uids.contains(m_message.value(QStringLiteral("uid")).toUInt()))
        closeMessage();
    job(acct, raw, false, [this, uids](Imap *imap, const QString &err, const Next &next) {
        if (!imap) {
            emit error(QStringLiteral("Email"), err);
            next();
            return;
        }
        imap->store(uids, true, "\\Deleted", [this, imap, uids, next](bool ok, const QString &err) {
            if (!ok) {
                emit error(QStringLiteral("Email"), err);
                next();
                return;
            }
            imap->expungeDeleted(uids, [next](bool, const QString &) { next(); });
        });
    });
}

void MailStore::moveMessages(const QVariantList &uidList, const QString &folderKey)
{
    QString acct, toAcct;
    QByteArray raw, toRaw;
    splitKey(m_folder, &acct, &raw);
    splitKey(folderKey, &toAcct, &toRaw);
    if (acct != toAcct) {
        emit error(QStringLiteral("Email"), QStringLiteral("Messages can only be moved within the same account."));
        return;
    }
    if (toRaw == raw || toRaw.isEmpty())
        return;
    const QList<quint32> uids = toUids(uidList);
    int unreadMoved = 0;
    for (int i = int(m_messages.size()) - 1; i >= 0; --i) {
        const QVariantMap m = m_messages.at(i).toMap();
        if (uids.contains(m.value(QStringLiteral("uid")).toUInt())) {
            if (!m.value(QStringLiteral("seen")).toBool())
                ++unreadMoved;
            m_messages.removeAt(i);
        }
    }
    m_unread[m_folder] = qMax(0, m_unread.value(m_folder) - unreadMoved);
    m_unread[folderKey] = m_unread.value(folderKey) + unreadMoved;
    rebuildFolders();
    emit messagesChanged();
    cacheList(m_folder);
    if (uids.contains(m_message.value(QStringLiteral("uid")).toUInt()))
        closeMessage();
    job(acct, raw, false, [this, uids, toRaw](Imap *imap, const QString &err, const Next &next) {
        if (!imap) {
            emit error(QStringLiteral("Email"), err);
            next();
            return;
        }
        imap->move(uids, toRaw, [this, next](bool ok, const QString &err) {
            if (!ok)
                emit error(QStringLiteral("Email"), QStringLiteral("The messages couldn't be moved: %1").arg(err));
            next();
        });
    });
}

QString MailStore::saveAttachment(int index, const QString &path) const
{
    if (index < 0 || index >= m_parsed.attachments.size())
        return QStringLiteral("There's no such attachment.");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1.").arg(path);
    f.write(m_parsed.attachments.at(index).data);
    return f.commit() ? QString() : QStringLiteral("Cannot write %1.").arg(path);
}

bool MailStore::openAttachment(int index) const
{
    if (index < 0 || index >= m_parsed.attachments.size())
        return false;
    // a private copy, opened with the program for its type
    QString name = QFileInfo(m_parsed.attachments.at(index).name).fileName();
    if (name.isEmpty())
        name = QStringLiteral("attachment");
    const QString dir = cacheRoot() + QStringLiteral("/open/") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QString path = dir + QLatin1Char('/') + name;
    if (!writePrivate(path, m_parsed.attachments.at(index).data))
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString MailStore::saveMessage(const QString &path) const
{
    if (m_raw.isEmpty())
        return QStringLiteral("No message is open.");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1.").arg(path);
    f.write(m_raw);
    return f.commit() ? QString() : QStringLiteral("Cannot write %1.").arg(path);
}

// ----------------------------------------------------------------- writing

QVariantMap MailStore::replyTo(bool all) const
{
    QVariantMap c;
    QString acct;
    splitKey(m_folder, &acct, nullptr);
    c[QStringLiteral("account")] = acct;
    if (m_raw.isEmpty())
        return c;
    const QString me = m_state.value(acct).settings.value(QStringLiteral("email")).toString().toLower();
    QList<Mime::Address> to = m_parsed.replyTo.isEmpty() ? m_parsed.from : m_parsed.replyTo;
    QList<Mime::Address> cc;
    if (all) {
        for (const Mime::Address &a : m_parsed.to + m_parsed.cc)
            if (a.email.toLower() != me && !std::any_of(to.cbegin(), to.cend(), [&](const Mime::Address &t) {
                    return t.email.compare(a.email, Qt::CaseInsensitive) == 0; }))
                cc << a;
    }
    c[QStringLiteral("to")] = Mime::formatAddresses(to);
    c[QStringLiteral("cc")] = Mime::formatAddresses(cc);
    c[QStringLiteral("subject")] = strippedSubject(m_parsed.subject, QStringLiteral("Re: "));
    const QString when = m_parsed.date.isValid() ? QLocale::system().toString(m_parsed.date.toLocalTime(), QLocale::LongFormat) : QString();
    c[QStringLiteral("text")] = QStringLiteral("\n\n----- Original Message -----\nFrom: %1\nTo: %2\nSent: %3\nSubject: %4\n\n%5")
                                    .arg(Mime::formatAddresses(m_parsed.from), Mime::formatAddresses(m_parsed.to), when,
                                         m_parsed.subject, quoteText(m_parsed.text.trimmed()));
    c[QStringLiteral("inReplyTo")] = m_parsed.messageId;
    c[QStringLiteral("references")] = (m_parsed.references + QLatin1Char(' ') + m_parsed.messageId).trimmed();
    c[QStringLiteral("replyUid")] = m_message.value(QStringLiteral("uid"));
    c[QStringLiteral("replyFolder")] = m_folder;
    return c;
}

QVariantMap MailStore::forward() const
{
    QVariantMap c;
    QString acct;
    splitKey(m_folder, &acct, nullptr);
    c[QStringLiteral("account")] = acct;
    if (m_raw.isEmpty())
        return c;
    c[QStringLiteral("subject")] = strippedSubject(m_parsed.subject, QStringLiteral("Fw: "));
    const QString when = m_parsed.date.isValid() ? QLocale::system().toString(m_parsed.date.toLocalTime(), QLocale::LongFormat) : QString();
    c[QStringLiteral("text")] = QStringLiteral("\n\n----- Original Message -----\nFrom: %1\nTo: %2\nSent: %3\nSubject: %4\n\n%5")
                                    .arg(Mime::formatAddresses(m_parsed.from), Mime::formatAddresses(m_parsed.to), when,
                                         m_parsed.subject, m_parsed.text.trimmed());
    // its attachments go too: private copies to attach
    QStringList paths;
    const QString dir = cacheRoot() + QStringLiteral("/forward/") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    for (const Mime::Attachment &a : m_parsed.attachments) {
        const QString path = dir + QLatin1Char('/') + (QFileInfo(a.name).fileName().isEmpty() ? QStringLiteral("attachment") : QFileInfo(a.name).fileName());
        if (writePrivate(path, a.data))
            paths << path;
    }
    c[QStringLiteral("attachments")] = paths;
    return c;
}

namespace {
Mime::Outgoing outgoing(const QVariantMap &settings, const QVariantMap &c, QString *problem)
{
    Mime::Outgoing m;
    m.from.name = settings.value(QStringLiteral("displayName")).toString();
    m.from.email = settings.value(QStringLiteral("email")).toString();
    m.to = Mime::parseAddresses(c.value(QStringLiteral("to")).toString());
    m.cc = Mime::parseAddresses(c.value(QStringLiteral("cc")).toString());
    m.bcc = Mime::parseAddresses(c.value(QStringLiteral("bcc")).toString());
    m.subject = c.value(QStringLiteral("subject")).toString();
    m.text = c.value(QStringLiteral("text")).toString();
    m.inReplyTo = c.value(QStringLiteral("inReplyTo")).toString();
    m.references = c.value(QStringLiteral("references")).toString();
    static const QMimeDatabase db;
    for (const QVariant &p : c.value(QStringLiteral("attachments")).toList()) {
        QFile f(p.toString());
        if (!f.open(QIODevice::ReadOnly)) {
            if (problem)
                *problem = QStringLiteral("The attachment %1 can't be read.").arg(p.toString());
            continue;
        }
        Mime::Attachment a;
        a.name = QFileInfo(p.toString()).fileName();
        a.type = db.mimeTypeForFile(p.toString()).name();
        a.data = f.readAll();
        m.attachments << a;
    }
    return m;
}
} // namespace

void MailStore::send(const QString &id, const QVariantMap &compose)
{
    const QString acct = compose.value(QStringLiteral("account")).toString().isEmpty() && !m_accounts.isEmpty()
        ? m_accounts.first().toMap().value(QStringLiteral("id")).toString() : compose.value(QStringLiteral("account")).toString();
    Account *a = account(acct);
    if (!a) {
        emit sendFinished(id, false, QStringLiteral("Set up an account first (Tools > Accounts)."));
        return;
    }
    QString problem;
    const Mime::Outgoing m = outgoing(a->settings, compose, &problem);
    if (!problem.isEmpty()) {
        emit sendFinished(id, false, problem);
        return;
    }
    QStringList recipients;
    for (const Mime::Address &r : m.to + m.cc + m.bcc)
        if (!r.email.isEmpty())
            recipients << r.email;
    if (recipients.isEmpty()) {
        emit sendFinished(id, false, QStringLiteral("Type who it's to."));
        return;
    }
    for (const QString &r : std::as_const(recipients)) {
        if (!r.contains(QLatin1Char('@')) || r.contains(QLatin1Char(' '))) {
            emit sendFinished(id, false, QStringLiteral("\"%1\" isn't an e-mail address.").arg(r));
            return;
        }
    }
    const QByteArray message = Mime::build(m);
    const QVariantMap s = a->settings;
    work(1);
    setStatus(QStringLiteral("Sending..."));
    withPassword(acct, [this, id, acct, s, recipients, message, compose](bool ok, const QString &password) {
        if (!ok) {
            work(-1);
            setStatus(QString());
            emit sendFinished(id, false, QStringLiteral("No password was given."));
            return;
        }
        Smtp::Server server;
        server.host = s.value(QStringLiteral("smtpHost")).toString();
        server.port = s.value(QStringLiteral("smtpPort")).toInt();
        server.security = s.value(QStringLiteral("smtpSecurity")).toString();
        server.user = s.value(QStringLiteral("user")).toString();
        server.password = password;
        Smtp::send(server, s.value(QStringLiteral("email")).toString(), recipients, message,
                   [this, id, acct, s, message, compose](bool ok, const QString &err) {
            work(-1);
            setStatus(QString());
            if (!ok) {
                emit sendFinished(id, false, err);
                return;
            }
            emit sendFinished(id, true, QString());
            // a copy in Sent (Gmail's server keeps one itself)
            const QString sent = specialFolder(acct, QStringLiteral("sent"));
            if (!sent.isEmpty() && !s.value(QStringLiteral("smtpHost")).toString().contains(QLatin1String("gmail.com"))) {
                job(acct, QByteArray(), false, [message, sent](Imap *imap, const QString &, const Next &next) {
                    if (!imap) {
                        next();
                        return;
                    }
                    imap->append(sent.toLatin1(), "\\Seen", message, [next](bool, const QString &) { next(); });
                });
            }
            // the message replied to: marked answered
            const quint32 replyUid = compose.value(QStringLiteral("replyUid")).toUInt();
            const QString replyFolder = compose.value(QStringLiteral("replyFolder")).toString();
            if (replyUid && !replyFolder.isEmpty()) {
                QString racct;
                QByteArray rraw;
                splitKey(replyFolder, &racct, &rraw);
                job(racct, rraw, false, [replyUid](Imap *imap, const QString &, const Next &next) {
                    if (!imap) {
                        next();
                        return;
                    }
                    imap->store({replyUid}, true, "\\Answered", [next](bool, const QString &) { next(); });
                });
                if (replyFolder == m_folder) {
                    for (QVariant &v : m_messages) {
                        QVariantMap mm = v.toMap();
                        if (mm.value(QStringLiteral("uid")).toUInt() == replyUid) {
                            mm[QStringLiteral("answered")] = true;
                            v = mm;
                        }
                    }
                    emit messagesChanged();
                }
            }
        });
    });
}

void MailStore::saveDraft(const QVariantMap &compose, const QJSValue &done)
{
    QJSValue cb = done;
    auto answer = [cb](bool ok, const QString &err) mutable {
        if (cb.isCallable())
            cb.call({ok, err});
    };
    const QString acct = compose.value(QStringLiteral("account")).toString().isEmpty() && !m_accounts.isEmpty()
        ? m_accounts.first().toMap().value(QStringLiteral("id")).toString() : compose.value(QStringLiteral("account")).toString();
    Account *a = account(acct);
    if (!a) {
        answer(false, QStringLiteral("Set up an account first (Tools > Accounts)."));
        return;
    }
    const QString drafts = specialFolder(acct, QStringLiteral("drafts"));
    if (drafts.isEmpty()) {
        answer(false, QStringLiteral("This account has no Drafts folder."));
        return;
    }
    QString problem;
    const QByteArray message = Mime::build(outgoing(a->settings, compose, &problem));
    job(acct, QByteArray(), false, [this, message, drafts, answer, acct](Imap *imap, const QString &err, const Next &next) mutable {
        if (!imap) {
            answer(false, err);
            next();
            return;
        }
        imap->append(drafts.toLatin1(), "\\Draft \\Seen", message, [this, answer, next, acct, drafts](bool ok, const QString &err) mutable {
            answer(ok, err);
            if (ok && m_folder == keyFor(acct, drafts.toLatin1()))
                syncMessages(m_folder);
            next();
        });
    });
}

// ----------------------------------------------------------------- status

void MailStore::setStatus(const QString &status)
{
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void MailStore::work(int delta)
{
    m_busy = qMax(0, m_busy + delta);
    emit statusChanged();
}

void MailStore::notifyNewMail(int count, const QString &, const QString &)
{
    // the desktop's notification (org.freedesktop.Notifications)
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                    QStringLiteral("/org/freedesktop/Notifications"),
                                                    QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    m << QStringLiteral("Email") << uint(0) << QStringLiteral("mail-unread")
      << (count == 1 ? QStringLiteral("1 new message") : QStringLiteral("%1 new messages").arg(count))
      << QStringLiteral("In your Inbox.") << QStringList() << QVariantMap() << -1;
    QDBusConnection::sessionBus().asyncCall(m);
}
