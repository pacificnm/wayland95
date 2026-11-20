#pragma once

#include <QAudioBufferOutput>
#include <QByteArray>
#include <QList>
#include <QPointer>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

class QAudioSink;
class QIODevice;

// What a MediaPlayer's sound is doing, for a Visualizer and for code
// (player1.levels, player1.level, player1.waveform), and its equalizer.
//
// The equalizer: with `equalizer` on, the player's own output is muted
// (the MediaPlayer does that) and the sound it hands over here goes
// through ten peaking filters (31 Hz to 16 kHz, `gains` in dB, -12 to
// +12, and `preamp`) to the speakers on a stream of this one's own, at
// `volume` (0 to 1; nothing while `audible` is false: muted, or a test).
//
// The decoded sound (as the player hands it over: stereo, 48 kHz; after
// the equalizer when it's on) is kept by its time, and
// update(position) looks at the bit being heard now: an FFT into `bands`
// bars (low to high, 0 to 1, falling slowly like a meter's), its loudness,
// and 64 points of its wave (-1 to 1).
//
// (The player may decode ahead of what's heard, so the sound is matched
// to the player's position, not to when it arrived.)
class AudioAnalyzer : public QAudioBufferOutput
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int bands READ bands WRITE setBands NOTIFY bandsChanged)
    Q_PROPERTY(QList<qreal> levels READ levels NOTIFY updated)
    Q_PROPERTY(QList<qreal> peaks READ peaks NOTIFY updated)
    Q_PROPERTY(QList<qreal> waveform READ waveform NOTIFY updated)
    Q_PROPERTY(qreal level READ level NOTIFY updated)
    Q_PROPERTY(bool equalizer READ equalizer WRITE setEqualizer NOTIFY equalizerChanged)
    Q_PROPERTY(qreal preamp READ preamp WRITE setPreamp NOTIFY equalizerChanged)
    Q_PROPERTY(QList<qreal> gains READ gains WRITE setGains NOTIFY equalizerChanged)
    Q_PROPERTY(QList<int> frequencies READ frequencies CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY outputChanged)
    Q_PROPERTY(bool audible READ audible WRITE setAudible NOTIFY outputChanged)
    // (how loud what the equalizer puts out is, 0 to 1: the tests')
    Q_PROPERTY(qreal outputLevel READ outputLevel NOTIFY updated)
    // how much of the equalized sound is waiting to be heard, in ms (its stream's and its own)
    Q_PROPERTY(int latency READ latency NOTIFY updated)
    // whether its own stream is open (the equalizer on, and heard)
    Q_PROPERTY(bool streaming READ streaming NOTIFY outputChanged)

public:
    explicit AudioAnalyzer(QObject *parent = nullptr);

    int bands() const { return m_bands; }
    void setBands(int n);
    QList<qreal> levels() const { return m_levels; }
    QList<qreal> peaks() const { return m_peaks; }
    QList<qreal> waveform() const { return m_wave; }
    qreal level() const { return m_level; }

    bool equalizer() const { return m_eqOn; }
    void setEqualizer(bool on);
    qreal preamp() const { return m_preamp; }
    void setPreamp(qreal db);
    QList<qreal> gains() const { return m_gains; }
    void setGains(const QList<qreal> &db);
    QList<int> frequencies() const;
    QStringList presetNames() const;
    // a preset's gains (dB, ten of them): Flat, Rock, Pop... ([] if there's none)
    Q_INVOKABLE QList<qreal> preset(const QString &name) const;
    // the preset these gains are ("" if none is: Custom)
    Q_INVOKABLE QString presetOf(const QList<qreal> &gains) const;
    qreal volume() const { return m_volume; }
    void setVolume(qreal v);
    bool audible() const { return m_audible; }
    void setAudible(bool on);
    qreal outputLevel() const { return m_outLevel; }
    int latency() const;
    bool streaming() const { return m_sink != nullptr; }
    // what's waiting to be heard goes (a seek, a stop, another file)
    Q_INVOKABLE void flush();

    // look at what's heard at `positionMs`; quiet (falling to nothing) if
    // nothing is
    Q_INVOKABLE void update(qint64 positionMs, bool playing);
    // forget the sound kept (a new file, or a jump)
    Q_INVOKABLE void clear();

signals:
    void bandsChanged();
    void updated();
    void equalizerChanged();
    void outputChanged();

private:
    void received(const QAudioBuffer &buffer);
    void analyze(const float *samples, int count);
    void fall();
    void design();                       // the filters' coefficients from the gains
    void equalize(float *frames, int count);   // stereo, in place
    void play(const float *frames, int count);
    void openSink();
    void closeSink();
    void pump();

    struct Chunk { qint64 startUs; QVector<float> samples; };
    QList<Chunk> m_chunks;      // in time order, about the last eight seconds
    int m_bands = 20;
    QList<qreal> m_levels, m_peaks, m_wave;
    QVector<qint64> m_peakHeld;  // when each peak was set (ms of the clock)
    qreal m_level = 0;
    qint64 m_clockMs = 0;

    // the equalizer: a biquad per band, its state per channel
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; bool flat = true; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    bool m_eqOn = false;
    qreal m_preamp = 0;
    QList<qreal> m_gains = QList<qreal>(10, 0.0);
    Biquad m_filters[10];
    State m_state[10][2];
    qreal m_volume = 1.0;
    bool m_audible = true;
    qreal m_outLevel = 0;
    QAudioSink *m_sink = nullptr;
    QPointer<QIODevice> m_out;
    QByteArray m_pending;               // what the stream hasn't taken yet
};
