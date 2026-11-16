#include "audiomixer.h"
#include <csignal>
#include <sys/prctl.h>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtDebug>

#include <algorithm>
#include <cmath>

namespace {

// how long a requested volume overrides what PipeWire last reported
constexpr qint64 RequestGraceMs = 1500;

QString jsonString(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString();
}

} // namespace

AudioMixer::AudioMixer(QObject *parent)
    : QObject(parent)
{
    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(3000);
    connect(&m_restartTimer, &QTimer::timeout, this, &AudioMixer::startMonitor);

    // a burst of updates (a device appearing) becomes one rebuild
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(20);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &AudioMixer::rebuild);

    startMonitor();
}

AudioMixer::~AudioMixer()
{
    if (m_monitor) {
        m_monitor->disconnect(this);
        m_monitor->kill();
        m_monitor->waitForFinished(500);
    }
}

void AudioMixer::startMonitor()
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("pw-dump"));
    if (program.isEmpty()) {
        qInfo("Audio: pw-dump not found; volume controls are unavailable (needs PipeWire)");
        return;
    }
    if (m_monitor)
        m_monitor->deleteLater();
    m_buffer.clear();
    m_chunk.clear();
    m_inChunk = false;
    m_objects.clear();

    m_monitor = new QProcess(this);
    m_monitor->setProgram(program);
    m_monitor->setArguments({ QStringLiteral("--monitor"), QStringLiteral("--no-colors") });
    m_monitor->setProcessChannelMode(QProcess::SeparateChannels);
    // it ends with us, even if we crash (otherwise one more is left
    // running each time the desktop is started again)
    m_monitor->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_monitor->setStandardInputFile(QProcess::nullDevice());
    connect(m_monitor, &QProcess::readyReadStandardOutput, this, &AudioMixer::readMonitor);
    connect(m_monitor, &QProcess::finished, this, [this] {
        // PipeWire restarted or isn't running yet: try again in a moment
        m_objects.clear();
        rebuild();
        m_restartTimer.start();
    });
    m_monitor->start();
}

void AudioMixer::readMonitor()
{
    m_buffer += m_monitor->readAllStandardOutput();
    // pw-dump pretty-prints each update as a JSON array whose brackets sit
    // alone on their lines, so lines are enough to find where one ends
    qsizetype start = 0;
    for (;;) {
        const qsizetype nl = m_buffer.indexOf('\n', start);
        if (nl < 0)
            break;
        const QByteArray line = m_buffer.mid(start, nl - start);
        start = nl + 1;
        if (!m_inChunk) {
            if (line == "[") {
                m_inChunk = true;
                m_chunk = "[\n";
            }
            continue;
        }
        m_chunk += line;
        m_chunk += '\n';
        if (line == "]") {
            m_inChunk = false;
            applyChunk(m_chunk);
            m_chunk.clear();
        }
    }
    m_buffer.remove(0, start);
}

void AudioMixer::applyChunk(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (!doc.isArray()) {
        qWarning() << "Audio: couldn't parse pw-dump output:" << error.errorString();
        return;
    }
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const int id = o.value(QLatin1String("id")).toInt(-1);
        if (id < 0)
            continue;
        // updates carry the whole object; a null info means it's gone
        if (o.value(QLatin1String("info")).isNull() && !o.contains(QLatin1String("metadata")))
            m_objects.remove(id);
        else
            m_objects.insert(id, o);
    }
    m_rebuildTimer.start();
}

QVariantMap AudioMixer::nodeEntry(const QJsonObject &object) const
{
    const int id = object.value(QLatin1String("id")).toInt();
    const QJsonObject info = object.value(QLatin1String("info")).toObject();
    const QJsonObject props = info.value(QLatin1String("props")).toObject();

    qreal volume = 1;
    bool muted = false;
    for (const QJsonValue &p : info.value(QLatin1String("params")).toObject().value(QLatin1String("Props")).toArray()) {
        const QJsonObject po = p.toObject();
        const QJsonArray channels = po.value(QLatin1String("channelVolumes")).toArray();
        if (channels.isEmpty())
            continue;
        qreal sum = 0;
        for (const QJsonValue &c : channels)
            sum += c.toDouble();
        volume = std::cbrt(sum / channels.size()); // linear -> perceptual
        muted = po.value(QLatin1String("mute")).toBool();
        break;
    }
    const auto requested = m_requestedVolume.constFind(id);
    if (requested != m_requestedVolume.constEnd()
        && QDateTime::currentMSecsSinceEpoch() - m_requestedAt.value(id) < RequestGraceMs)
        volume = requested.value();

    QString name = jsonString(props, "application.name");
    if (name.isEmpty())
        name = jsonString(props, "node.description");
    if (name.isEmpty())
        name = jsonString(props, "node.nick");
    if (name.isEmpty())
        name = jsonString(props, "node.name");
    QString description = jsonString(props, "node.description");
    if (description.isEmpty())
        description = name;

    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), name },
        { QStringLiteral("nodeName"), jsonString(props, "node.name") },
        { QStringLiteral("description"), description },
        { QStringLiteral("mediaName"), jsonString(props, "media.name") },
        { QStringLiteral("volume"), qBound<qreal>(0, volume, 1.5) },
        { QStringLiteral("muted"), muted },
    };
}

void AudioMixer::rebuild()
{
    QString defaultSink, defaultSource;
    for (const QJsonObject &o : std::as_const(m_objects)) {
        if (jsonString(o, "type") != QLatin1String("PipeWire:Interface:Metadata")
            || o.value(QLatin1String("props")).toObject().value(QLatin1String("metadata.name")).toString()
                   != QLatin1String("default"))
            continue;
        for (const QJsonValue &m : o.value(QLatin1String("metadata")).toArray()) {
            const QJsonObject entry = m.toObject();
            const QString key = jsonString(entry, "key");
            const QString value = entry.value(QLatin1String("value")).toObject().value(QLatin1String("name")).toString();
            if (key == QLatin1String("default.audio.sink"))
                defaultSink = value;
            else if (key == QLatin1String("default.audio.source"))
                defaultSource = value;
        }
    }

    QVariantList outputs, inputs, streams;
    QVariantMap output, input;
    for (const QJsonObject &o : std::as_const(m_objects)) {
        if (jsonString(o, "type") != QLatin1String("PipeWire:Interface:Node"))
            continue;
        const QString mediaClass = o.value(QLatin1String("info")).toObject().value(QLatin1String("props"))
                                       .toObject().value(QLatin1String("media.class")).toString();
        if (mediaClass == QLatin1String("Audio/Sink")) {
            QVariantMap e = nodeEntry(o);
            const bool isDefault = e.value(QStringLiteral("nodeName")).toString() == defaultSink;
            e.insert(QStringLiteral("isDefault"), isDefault);
            if (isDefault)
                output = e;
            outputs.append(e);
        } else if (mediaClass == QLatin1String("Audio/Source")) {
            QVariantMap e = nodeEntry(o);
            const bool isDefault = e.value(QStringLiteral("nodeName")).toString() == defaultSource;
            e.insert(QStringLiteral("isDefault"), isDefault);
            if (isDefault)
                input = e;
            inputs.append(e);
        } else if (mediaClass == QLatin1String("Stream/Output/Audio")) {
            streams.append(nodeEntry(o));
        }
    }
    // sound cards (PipeWire devices) and their profiles
    QVariantList cards;
    for (const QJsonObject &o : std::as_const(m_objects)) {
        if (jsonString(o, "type") != QLatin1String("PipeWire:Interface:Device"))
            continue;
        const QJsonObject info = o.value(QLatin1String("info")).toObject();
        const QJsonObject props = info.value(QLatin1String("props")).toObject();
        if (props.value(QLatin1String("media.class")).toString() != QLatin1String("Audio/Device"))
            continue;
        const QJsonObject params = info.value(QLatin1String("params")).toObject();
        QVariantList profiles;
        for (const QJsonValue &v : params.value(QLatin1String("EnumProfile")).toArray()) {
            const QJsonObject pr = v.toObject();
            profiles << QVariantMap{
                { QStringLiteral("index"), pr.value(QLatin1String("index")).toInt() },
                { QStringLiteral("name"), pr.value(QLatin1String("name")).toString() },
                { QStringLiteral("description"), pr.value(QLatin1String("description")).toString() },
                { QStringLiteral("available"), pr.value(QLatin1String("available")).toString() != QLatin1String("no") },
            };
        }
        int active = -1;
        const QJsonArray current = params.value(QLatin1String("Profile")).toArray();
        if (!current.isEmpty())
            active = current.first().toObject().value(QLatin1String("index")).toInt(-1);
        QString description = jsonString(props, "device.description");
        if (description.isEmpty())
            description = jsonString(props, "device.name");
        cards << QVariantMap{
            { QStringLiteral("id"), o.value(QLatin1String("id")).toInt() },
            { QStringLiteral("description"), description },
            { QStringLiteral("profile"), active },
            { QStringLiteral("profiles"), profiles },
        };
    }

    auto byDescription = [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("description")).toString().compare(
                   b.toMap().value(QStringLiteral("description")).toString(), Qt::CaseInsensitive) < 0;
    };
    std::sort(outputs.begin(), outputs.end(), byDescription);
    std::sort(inputs.begin(), inputs.end(), byDescription);
    // streams in the order they appeared (ids grow), so columns don't shuffle
    std::sort(streams.begin(), streams.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("id")).toInt() < b.toMap().value(QStringLiteral("id")).toInt();
    });
    // no default set yet: PipeWire would use the first one
    if (output.isEmpty() && !outputs.isEmpty())
        output = outputs.constFirst().toMap();
    if (input.isEmpty() && !inputs.isEmpty())
        input = inputs.constFirst().toMap();

    const bool available = !outputs.isEmpty();
    std::sort(cards.begin(), cards.end(), byDescription);
    if (available == m_available && outputs == m_outputs && inputs == m_inputs && streams == m_streams
        && output == m_output && input == m_input && cards == m_cards)
        return;
    m_available = available;
    m_cards = cards;
    m_outputs = outputs;
    m_inputs = inputs;
    m_streams = streams;
    m_output = output;
    m_input = input;
    emit changed();
}

void AudioMixer::runWpctl(const QStringList &args)
{
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("wpctl"));
    p->setArguments(args);
    p->setStandardInputFile(QProcess::nullDevice());
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            qWarning("Audio: couldn't run wpctl (WirePlumber)");
            p->deleteLater();
        }
    });
    p->start();
}

void AudioMixer::setVolume(int id, qreal volume)
{
    if (id < 0)
        return;
    volume = qBound<qreal>(0, volume, 1);
    m_requestedVolume.insert(id, volume);
    m_requestedAt.insert(id, QDateTime::currentMSecsSinceEpoch());
    m_pendingVolume.insert(id, volume);
    if (!m_volumeProcess.contains(id))
        runVolume(id);
    m_rebuildTimer.start(); // show it straight away
}

void AudioMixer::runVolume(int id)
{
    const qreal volume = m_pendingVolume.take(id);
    auto *p = new QProcess(this);
    m_volumeProcess.insert(id, p);
    p->setProgram(QStringLiteral("wpctl"));
    p->setArguments({ QStringLiteral("set-volume"), QString::number(id), QString::number(volume, 'f', 3) });
    p->setStandardInputFile(QProcess::nullDevice());
    auto done = [this, id, p] {
        p->deleteLater();
        m_volumeProcess.remove(id);
        if (m_pendingVolume.contains(id))
            runVolume(id);
    };
    connect(p, &QProcess::finished, this, done);
    connect(p, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done();
    });
    p->start();
}

void AudioMixer::setMuted(int id, bool muted)
{
    if (id >= 0)
        runWpctl({ QStringLiteral("set-mute"), QString::number(id), muted ? QStringLiteral("1") : QStringLiteral("0") });
}

void AudioMixer::setDefault(int id)
{
    if (id >= 0)
        runWpctl({ QStringLiteral("set-default"), QString::number(id) });
}

void AudioMixer::setProfile(int cardId, int profileIndex)
{
    runWpctl({ QStringLiteral("set-profile"), QString::number(cardId), QString::number(profileIndex) });
}
