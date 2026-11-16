#pragma once

// org.wayland95.Accessibility for win95shell: programs' playSound()
// (Wayland95Settings) says a sound played, for SoundSentry and ShowSounds.
// Exposed to QML as the singleton `AccessibilityService`.

#include <QObject>
#include <QtQml/qqmlregistration.h>

class AccessibilityService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_CLASSINFO("D-Bus Interface", "org.wayland95.Accessibility")

public:
    explicit AccessibilityService(QObject *parent = nullptr);

public Q_SLOTS:
    void SoundPlayed(const QString &event) { Q_EMIT soundPlayed(event); }

Q_SIGNALS:
    void soundPlayed(const QString &event);
};
