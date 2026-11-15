#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QThread>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

// One PackageKit transaction (org.freedesktop.PackageKit.Transaction):
// created, given hints, then one method is called on it. Its D-Bus signals
// come back as Qt signals; it deletes itself once finished.
class PkTransaction : public QObject
{
    Q_OBJECT

public:
    explicit PkTransaction(QObject *parent = nullptr);
    // e.g. start("SearchNames", { quint64(filter), QStringList{"chess"} })
    // interactive: the user asked for it, so polkit may ask for a password
    void start(const QString &method, const QVariantList &args, bool interactive = false);
    void cancel();

signals:
    void package(uint info, const QString &id, const QString &summary);
    void files(const QString &id, const QStringList &files);
    void details(const QVariantMap &details);
    void errorCode(uint code, const QString &details);
    void progress(int percent, uint status); // percent -1: unknown
    void finished(bool ok, const QString &error);

private slots:
    void onPackage(uint info, const QString &id, const QString &summary);
    void onFiles(const QString &id, const QStringList &files);
    void onDetails(const QVariantMap &details);
    void onErrorCode(uint code, const QString &details);
    void onFinished(uint exit, uint runtime);
    void onProperties(const QString &interface, const QVariantMap &changed, const QStringList &);

private:
    void fail(const QString &error);

    QString m_path;
    QString m_error;
    int m_percent = -1;
    uint m_status = 0;
    bool m_done = false;
};

// Programs: what's installed, what can be installed, and updates, from
// two places:
//  - the distribution's packages, through PackageKit (apt on Debian, dnf,
//    zypper...). Programs to install come from the distribution's AppStream
//    catalog (names, summaries, icons, categories); "all packages" searches
//    PackageKit itself.
//  - Flatpak apps (Flathub and other remotes), through the flatpak command.
//    New apps go into the user's own installation: no password needed.
//
// Exposed to QML as the singleton `Software`.
class SoftwareCenter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Software)
    QML_SINGLETON

    Q_PROPERTY(bool packageKit READ packageKit CONSTANT)
    Q_PROPERTY(QString packageSource READ packageSource CONSTANT) // "Debian", "Fedora"...
    Q_PROPERTY(bool flatpak READ flatpak CONSTANT)
    Q_PROPERTY(QStringList flatpakRemotes READ flatpakRemotes NOTIFY catalogChanged)
    // installed programs: [{ key, source, id, name, summary, icon, version, installation }]
    Q_PROPERTY(QVariantList programs READ programs NOTIFY programsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY catalogChanged)
    Q_PROPERTY(int catalogSize READ catalogSize NOTIFY catalogChanged)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    // [{ key, source, id, name, version, kind, summary, installation }]
    Q_PROPERTY(QVariantList updates READ updates NOTIFY updatesChanged)
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY updatesChanged)
    Q_PROPERTY(bool updatesChecked READ updatesChecked NOTIFY updatesChanged)
    // the install/remove/update running now
    Q_PROPERTY(bool busy READ busy NOTIFY taskChanged)
    Q_PROPERTY(QString task READ task NOTIFY taskChanged)
    Q_PROPERTY(QString status READ status NOTIFY taskChanged)
    Q_PROPERTY(int percent READ percent NOTIFY taskChanged)

public:
    explicit SoftwareCenter(QObject *parent = nullptr);
    ~SoftwareCenter() override;

    bool packageKit() const { return m_packageKit; }
    QString packageSource() const { return m_packageSource; }
    bool flatpak() const { return !m_flatpakBin.isEmpty(); }
    QStringList flatpakRemotes() const { return m_remotes; }
    QVariantList programs() const { return m_programs; }
    bool loading() const { return m_loading > 0; }
    bool catalogLoading() const { return m_catalogLoading; }
    int catalogSize() const { return m_catalog.size(); }
    QStringList categories() const;
    QVariantList updates() const { return m_updates; }
    bool checkingUpdates() const { return m_checking > 0; }
    bool updatesChecked() const { return m_updatesChecked; }
    bool busy() const { return m_busy; }
    QString task() const { return m_task; }
    QString status() const { return m_status; }
    int percent() const { return m_percent; }

    // re-reads the installed programs
    Q_INVOKABLE void reload();
    // programs from the catalogs: text matches names, summaries and
    // keywords; category from `categories` ("" for all); source "deb",
    // "flatpak" or "" for both. At most `limit` results, best first:
    // [{ key, source, id, remote, name, summary, description, icon, installed, category }]
    Q_INVOKABLE QVariantList search(const QString &text, const QString &category, const QString &source, int limit = 400) const;
    // every package whose name or description matches, programs or not;
    // answers with packagesFound
    Q_INVOKABLE void searchPackages(const QString &text);

    // Installing and removing go in two steps: prepare...() works out what
    // else changes (dependencies) and answers with `plan`; commit() then
    // does it, or the plan is dropped by calling neither.
    Q_INVOKABLE void prepareInstall(const QVariantMap &item);
    Q_INVOKABLE void prepareRemove(const QVariantMap &program);
    Q_INVOKABLE void prepareUpdates(const QVariantList &updates);
    Q_INVOKABLE void commit();
    Q_INVOKABLE void cancel();

    // refresh: download the latest lists of software first
    Q_INVOKABLE void checkUpdates(bool refresh = true);
    // adds Flathub to the user's installation and downloads its catalog
    Q_INVOKABLE void setUpFlathub();

signals:
    void programsChanged();
    void loadingChanged();
    void catalogChanged();
    void updatesChanged();
    void taskChanged();
    void packagesFound(const QVariantList &packages);
    // what will happen: action "install"/"remove"/"update", the programs
    // asked for, other packages that come along (names), download/disk
    // sizes when known, and an error if it can't be done
    void plan(const QString &action, const QVariantList &items, const QStringList &others, const QString &error);
    void taskFinished(bool ok, const QString &message);

private:
    struct Program {
        QString source;     // "deb" or "flatpak"
        QString id;         // package name or Flatpak app ID
        QString remote;     // Flatpak remote
        QString installation; // Flatpak: "user" or "system"
        QString name, summary, description, icon, category;
        QStringList keywords;
    };
    struct Catalog {
        QList<Program> programs;
        QStringList remotes;
    };
    static Catalog loadCatalog(const QString &flatpakBin);
    static void loadDebCatalog(QList<Program> &out);
    static void loadFlatpakCatalog(QList<Program> &out, QStringList &remotes);
    static QVariantMap toVariant(const Program &p);

    void loadCatalogAsync();
    void loadDebPrograms();
    void loadFlatpakPrograms();
    void finishLoad();

    // one step after another; a failing step ends the task
    using Step = std::function<void()>;
    void runTask(const QString &title, const QList<Step> &steps);
    void nextStep();
    void taskFailed(const QString &error);
    void setStatus(const QString &status, int percent);
    PkTransaction *pk(const QString &method, const QVariantList &args, bool forTask);
    void runFlatpak(const QStringList &args, bool forTask, std::function<void(int, const QString &)> done);

    bool m_packageKit = false;
    QString m_packageSource;
    QString m_flatpakBin;
    QStringList m_remotes;

    QList<Program> m_catalog;
    bool m_catalogLoading = false;
    bool m_catalogAgain = false;
    QThread *m_catalogThread = nullptr;
    QSet<QString> m_installedDeb;      // package names
    QHash<QString, QString> m_installedFlatpak; // app ID -> "user"/"system"

    QVariantList m_programs;
    QVariantList m_debPrograms, m_flatpakPrograms;
    int m_loading = 0;

    QVariantList m_updates, m_debUpdates, m_flatpakUpdates;
    int m_checking = 0;
    bool m_updatesChecked = false;

    // the prepared plan, run by commit()
    QString m_planTitle;
    QList<Step> m_planSteps;

    bool m_busy = false;
    QString m_task, m_status;
    int m_percent = -1;
    QList<Step> m_steps;
    QPointer<PkTransaction> m_current;
    QPointer<QProcess> m_process;
};
