#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

class KeyringPrompter;

// org.wayland95.KeyringPrompter, which the keyring (win95keyring) calls
// when it needs your password
class KeyringPrompterAdaptor : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wayland95.KeyringPrompter")

public:
    explicit KeyringPrompterAdaptor(KeyringPrompter *prompter);

public slots:
    // mode: "unlock" (the list's password) or "create" (a new list's);
    // retry: the last one was wrong. Answers with the password, or the
    // error org.wayland95.KeyringPrompter.Canceled.
    QString AskPassword(const QString &mode, bool retry);
    // the program stopped waiting
    void Cancel();

private:
    KeyringPrompter *m_prompter;
};

// The desktop's half of the keyring: Wayland 95's password list is unlocked
// by your login password (pam_wayland95_keyring); when it wasn't (a
// different password, automatic login), or there's no list yet and no
// login password to make it with, a program wanting a saved password gets
// "Enter Password" here. Only the keyring's own calls are answered.
//
// Exposed to QML as the singleton `KeyringPrompts`.
class KeyringPrompter : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(KeyringPrompts)
    QML_SINGLETON

    Q_PROPERTY(bool pending READ pending NOTIFY requestChanged)
    // "unlock" or "create"
    Q_PROPERTY(QString mode READ mode NOTIFY requestChanged)
    // the last password was wrong
    Q_PROPERTY(bool retry READ retry NOTIFY requestChanged)

public:
    explicit KeyringPrompter(QObject *parent = nullptr);

    bool pending() const { return m_pending; }
    QString mode() const { return m_mode; }
    bool retry() const { return m_retry; }

    Q_INVOKABLE void provide(const QString &password);
    Q_INVOKABLE void cancel();

    // from the adaptor
    void request(const QDBusMessage &call, const QString &mode, bool retry);
    void cancelRequest(const QString &caller);

signals:
    void requestChanged();
    // show the dialog
    void requested();

private:
    void finish();

    QDBusConnection m_bus;
    bool m_pending = false;
    QString m_mode;
    bool m_retry = false;
    QDBusMessage m_call;
};
