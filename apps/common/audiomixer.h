#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// Volume and device control through PipeWire, for the taskbar's volume
// control and the Volume Control program.
//
// State comes from one long-running `pw-dump --monitor`, which prints the
// whole audio graph and then every change as it happens. Changes are made
// with WirePlumber's `wpctl`. Volumes are 0..1 on the same perceptual
// ("cubic") scale wpctl and most desktops show; PipeWire stores them cubed.
//
// Exposed to QML as the singleton `Audio`.
class AudioMixer : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Audio)
    QML_SINGLETON

    // PipeWire is running and there is somewhere to play sound
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // [{ id, name, description, volume, muted, isDefault }]
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
    // programs playing sound: [{ id, name, description, volume, muted }]
    Q_PROPERTY(QVariantList streams READ streams NOTIFY changed)

    // the default output (speakers/headphones); id -1 if there is none
    Q_PROPERTY(int outputId READ outputId NOTIFY changed)
    Q_PROPERTY(QString outputDescription READ outputDescription NOTIFY changed)
    Q_PROPERTY(qreal volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    // the default input (microphone)
    Q_PROPERTY(int inputId READ inputId NOTIFY changed)
    Q_PROPERTY(QString inputDescription READ inputDescription NOTIFY changed)
    Q_PROPERTY(qreal inputVolume READ inputVolume NOTIFY changed)
    Q_PROPERTY(bool inputMuted READ inputMuted NOTIFY changed)
    // sound cards and their profiles (which outputs and inputs they offer:
    // "Speaker", "HDMI", "Off"...): [{ id, description, profile (index),
    // profiles: [{ index, description, available }] }]
    Q_PROPERTY(QVariantList cards READ cards NOTIFY changed)

public:
    explicit AudioMixer(QObject *parent = nullptr);
    ~AudioMixer() override;

    bool available() const { return m_available; }
    QVariantList outputs() const { return m_outputs; }
    QVariantList inputs() const { return m_inputs; }
    QVariantList streams() const { return m_streams; }
    QVariantList cards() const { return m_cards; }
    int outputId() const { return m_output.value(QStringLiteral("id"), -1).toInt(); }
    QString outputDescription() const { return m_output.value(QStringLiteral("description")).toString(); }
    qreal volume() const { return m_output.value(QStringLiteral("volume")).toReal(); }
    bool muted() const { return m_output.value(QStringLiteral("muted")).toBool(); }
    int inputId() const { return m_input.value(QStringLiteral("id"), -1).toInt(); }
    QString inputDescription() const { return m_input.value(QStringLiteral("description")).toString(); }
    qreal inputVolume() const { return m_input.value(QStringLiteral("volume")).toReal(); }
    bool inputMuted() const { return m_input.value(QStringLiteral("muted")).toBool(); }

    // `id` is any output, input or stream id from the lists above
    Q_INVOKABLE void setVolume(int id, qreal volume);
    Q_INVOKABLE void setMuted(int id, bool muted);
    // make an output or input the default one
    Q_INVOKABLE void setDefault(int id);
    // switch a sound card to another profile
    Q_INVOKABLE void setProfile(int cardId, int profileIndex);

signals:
    void changed();

private:
    void startMonitor();
    void readMonitor();
    void applyChunk(const QByteArray &json);
    void rebuild();
    void runWpctl(const QStringList &args);
    void runVolume(int id);
    QVariantMap nodeEntry(const QJsonObject &object) const;

    QProcess *m_monitor = nullptr;
    QByteArray m_buffer;
    QByteArray m_chunk;
    bool m_inChunk = false;
    QTimer m_restartTimer;
    QTimer m_rebuildTimer;

    QHash<int, QJsonObject> m_objects;
    bool m_available = false;
    QVariantList m_outputs;
    QVariantList m_inputs;
    QVariantList m_streams;
    QVariantList m_cards;
    QVariantMap m_output;
    QVariantMap m_input;

    // Slider drags produce many changes; run one wpctl per node at a time
    // and remember only the latest value meanwhile. Until PipeWire reports
    // back, show what was asked for so sliders don't jump.
    QHash<int, qreal> m_pendingVolume;
    QHash<int, QProcess *> m_volumeProcess;
    QHash<int, qreal> m_requestedVolume;
    QHash<int, qint64> m_requestedAt;
};
