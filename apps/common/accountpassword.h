#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

// Your account's password: checking it (the password-protected screen
// saver) and changing it (Control Panel > Passwords).
//  - checking goes through PAM's own helper, unix_chkpwd, which only
//    answers for the user running it;
//  - changing runs passwd on a pseudo-terminal and answers its prompts, so
//    the system's own rules (minimum length and the like) apply.
//
// Exposed to QML as the singleton `AccountPassword`.
class AccountPassword : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AccountPassword)
    QML_SINGLETON

    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // passwords can be checked here (unix_chkpwd exists)
    Q_PROPERTY(bool canCheck READ canCheck CONSTANT)
    // who can approve changes to the computer with their own password
    // (the sudo, wheel and admin groups), and whether you're one of them
    Q_PROPERTY(QStringList administrators READ administrators CONSTANT)
    Q_PROPERTY(bool isAdministrator READ isAdministrator CONSTANT)

public:
    explicit AccountPassword(QObject *parent = nullptr);
    ~AccountPassword() override;

    QString userName() const;
    bool busy() const { return m_check || m_childPid > 0; }
    bool canCheck() const;
    QStringList administrators() const;
    bool isAdministrator() const { return administrators().contains(userName()); }

    // answers with checked(); a wrong password answers after a pause
    Q_INVOKABLE void check(const QString &password);
    // answers with changeFinished()
    Q_INVOKABLE void change(const QString &oldPassword, const QString &newPassword);

signals:
    void busyChanged();
    void checked(bool ok);
    void changeFinished(bool ok, const QString &message);

private:
    void onPtyReadable();
    void endChange(bool ok, const QString &message);

    QPointer<QProcess> m_check;
    // passwd
    int m_ptyFd = -1;
    qint64 m_childPid = 0;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_timeout;
    QByteArray m_output;
    QString m_old, m_new;
    int m_newPrompts = 0;
    bool m_oldSent = false;
};
