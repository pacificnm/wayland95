#include "appaudio.h"

#include <QAudioBuffer>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QTimer>
#include <QMap>
#include <QDateTime>
#include <cmath>
#include <complex>

namespace {

constexpr int kRate = 48000;
// the equalizer's bands (Hz) and its presets (dB, from 31 Hz up)
constexpr int kFreq[10] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
const QList<QPair<QString, QList<qreal>>> &presets()
{
    static const QList<QPair<QString, QList<qreal>>> p = {
        { QStringLiteral("Flat"),         { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { QStringLiteral("Rock"),         { 5, 4, 3, 1, -1, -1, 1, 3, 4, 5 } },
        { QStringLiteral("Pop"),          { -1, 1, 3, 4, 4, 2, 0, -1, -1, -1 } },
        { QStringLiteral("Jazz"),         { 3, 2, 1, 2, -1, -1, 0, 1, 2, 3 } },
        { QStringLiteral("Classical"),    { 4, 3, 2, 1, 0, 0, 0, 1, 2, 3 } },
        { QStringLiteral("Dance"),        { 6, 5, 2, 0, 0, -2, -1, 0, 3, 4 } },
        { QStringLiteral("Metal"),        { 4, 3, 0, -2, -3, -1, 2, 4, 5, 4 } },
        { QStringLiteral("Bass Boost"),   { 7, 6, 5, 3, 1, 0, 0, 0, 0, 0 } },
        { QStringLiteral("Treble Boost"), { 0, 0, 0, 0, 0, 1, 3, 5, 6, 7 } },
        { QStringLiteral("Vocal"),        { -2, -3, -2, 1, 4, 4, 3, 1, 0, -2 } },
        { QStringLiteral("Loudness"),     { 6, 4, 0, 0, -2, 0, -1, -2, 4, 2 } },
        { QStringLiteral("Headphones"),   { 3, 5, 3, -1, -1, 1, 3, 5, 6, 7 } },
    };
    return p;
}
constexpr int kSize = 2048;          // the FFT's window (46 ms)
constexpr qint64 kKeepUs = 8000000;  // sound kept, behind the newest

QAudioFormat wanted()
{
    QAudioFormat f;
    f.setSampleRate(kRate);
    f.setChannelCount(2);
    f.setSampleFormat(QAudioFormat::Float);
    return f;
}

void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(M_PI) / float(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

AudioAnalyzer::AudioAnalyzer(QObject *parent)
    : QAudioBufferOutput(wanted(), parent)
{
    connect(this, &QAudioBufferOutput::audioBufferReceived, this, &AudioAnalyzer::received);
    setBands(20);
    design();
    m_wave = QList<qreal>(64, 0.0);
}

void AudioAnalyzer::setBands(int n)
{
    n = qBound(1, n, 64);
    if (n == m_bands && m_levels.size() == n)
        return;
    m_bands = n;
    m_levels = QList<qreal>(n, 0.0);
    m_peaks = QList<qreal>(n, 0.0);
    m_peakHeld = QVector<qint64>(n, 0);
    emit bandsChanged();
    emit updated();
}

void AudioAnalyzer::clear()
{
    m_chunks.clear();
    flush();
}

void AudioAnalyzer::received(const QAudioBuffer &buffer)
{
    if (!buffer.isValid() || buffer.format().sampleFormat() != QAudioFormat::Float)
        return;
    const int channels = qMax(1, buffer.format().channelCount());
    const int frames = int(buffer.frameCount());
    const float *data = buffer.constData<float>();
    // the equalizer: the sound through its filters, and out to the speakers
    std::vector<float> eq;
    if (m_eqOn && channels == 2) {
        eq.assign(data, data + frames * 2);
        equalize(eq.data(), frames);
        play(eq.data(), frames);
        data = eq.data();
    }
    Chunk chunk;
    chunk.startUs = buffer.startTime();
    chunk.samples.resize(frames);
    for (int i = 0; i < frames; ++i) {
        float sum = 0;
        for (int c = 0; c < channels; ++c)
            sum += data[i * channels + c];
        chunk.samples[i] = sum / channels;
    }
    // (a jump back: what was kept is from somewhere else)
    if (!m_chunks.isEmpty() && chunk.startUs < m_chunks.last().startUs)
        m_chunks.clear();
    m_chunks.append(chunk);
    while (m_chunks.size() > 1 && m_chunks.last().startUs - m_chunks.first().startUs > kKeepUs)
        m_chunks.removeFirst();
}

void AudioAnalyzer::update(qint64 positionMs, bool playing)
{
    m_clockMs = QDateTime::currentMSecsSinceEpoch();
    if (!playing || m_chunks.isEmpty()) {
        fall();
        return;
    }
    // the window that ends at what's heard now
    const qint64 endUs = positionMs * 1000;
    std::vector<float> window(kSize, 0.0f);
    int filled = 0;
    for (int ci = int(m_chunks.size()) - 1; ci >= 0 && filled < kSize; --ci) {
        const Chunk &c = m_chunks.at(ci);
        if (c.startUs >= endUs)
            continue;
        // samples of this chunk before endUs, newest first
        qint64 upto = qMin<qint64>(c.samples.size(), (endUs - c.startUs) * kRate / 1000000);
        for (qint64 i = upto - 1; i >= 0 && filled < kSize; --i)
            window[kSize - 1 - filled++] = c.samples.at(int(i));
    }
    if (filled < kSize / 4) {
        fall();
        return;
    }
    analyze(window.data(), kSize);
}

void AudioAnalyzer::analyze(const float *samples, int count)
{
    // loudness and the wave
    double sum = 0;
    for (int i = 0; i < count; ++i)
        sum += double(samples[i]) * samples[i];
    const qreal rms = std::sqrt(sum / count);
    // (as a meter shows it: -48 dB to 0)
    const qreal db = rms > 0 ? 20 * std::log10(rms) : -100;
    const qreal now = qBound<qreal>(0, (db + 48) / 48, 1);
    m_level = now > m_level ? now : qMax<qreal>(now, m_level - 0.04);
    const int points = int(m_wave.size());
    const int from = count - 1024;
    for (int i = 0; i < points; ++i)
        m_wave[i] = qBound<qreal>(-1, samples[from + i * 1024 / points], 1);

    // the bands: a Hann window, the FFT, and its bins summed into bands
    // spaced evenly by pitch (40 Hz to 16 kHz)
    std::vector<std::complex<float>> a(count);
    for (int i = 0; i < count; ++i)
        a[i] = samples[i] * (0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (count - 1)));
    fft(a);
    const double lo = 40, hi = 16000, binHz = double(kRate) / count;
    for (int b = 0; b < m_bands; ++b) {
        const double f0 = lo * std::pow(hi / lo, double(b) / m_bands);
        const double f1 = lo * std::pow(hi / lo, double(b + 1) / m_bands);
        const int k0 = qMax(1, int(f0 / binHz)), k1 = qMax(k0 + 1, int(f1 / binHz));
        double power = 0;
        for (int k = k0; k < k1 && k < count / 2; ++k)
            power = qMax(power, double(std::norm(a[k])));
        // (a full-scale sine is about count/4 in its bin)
        const double mag = std::sqrt(power) / (count / 4.0);
        const double bandDb = mag > 0 ? 20 * std::log10(mag) : -100;
        const qreal v = qBound<qreal>(0, (bandDb + 60) / 60, 1);
        m_levels[b] = v > m_levels[b] ? v : qMax<qreal>(v, m_levels[b] - 0.05);
        if (m_levels[b] >= m_peaks[b]) {
            m_peaks[b] = m_levels[b];
            m_peakHeld[b] = m_clockMs;
        } else if (m_clockMs - m_peakHeld[b] > 400) {
            m_peaks[b] = qMax<qreal>(m_levels[b], m_peaks[b] - 0.02);
        }
    }
    emit updated();
}

void AudioAnalyzer::fall()
{
    bool moving = m_level > 0;
    m_level = qMax<qreal>(0, m_level - 0.04);
    for (int b = 0; b < m_bands; ++b) {
        moving = moving || m_levels[b] > 0 || m_peaks[b] > 0;
        m_levels[b] = qMax<qreal>(0, m_levels[b] - 0.05);
        m_peaks[b] = qMax<qreal>(m_levels[b], m_peaks[b] - 0.02);
    }
    for (qreal &w : m_wave) {
        moving = moving || w != 0;
        w *= 0.7;
        if (std::abs(w) < 0.002)
            w = 0;
    }
    if (moving)
        emit updated();
}

// --- the equalizer ---------------------------------------------------------------

QList<int> AudioAnalyzer::frequencies() const
{
    return QList<int>(std::begin(kFreq), std::end(kFreq));
}

QStringList AudioAnalyzer::presetNames() const
{
    QStringList names;
    for (const auto &p : presets())
        names << p.first;
    return names;
}

QList<qreal> AudioAnalyzer::preset(const QString &name) const
{
    for (const auto &p : presets()) {
        if (p.first.compare(name, Qt::CaseInsensitive) == 0)
            return p.second;
    }
    return {};
}

QString AudioAnalyzer::presetOf(const QList<qreal> &gains) const
{
    for (const auto &p : presets()) {
        bool same = gains.size() == 10;
        for (int i = 0; same && i < 10; ++i)
            same = qAbs(gains.at(i) - p.second.at(i)) < 0.05;
        if (same)
            return p.first;
    }
    return QString();
}

void AudioAnalyzer::setEqualizer(bool on)
{
    if (on == m_eqOn)
        return;
    m_eqOn = on;
    // (from silence: what the filters held is from before)
    for (auto &band : m_state)
        for (auto &ch : band)
            ch = State();
    if (on)
        openSink();
    else
        closeSink();
    emit equalizerChanged();
}

void AudioAnalyzer::setPreamp(qreal db)
{
    db = qBound<qreal>(-12, db, 12);
    if (qFuzzyCompare(db + 100, m_preamp + 100))
        return;
    m_preamp = db;
    emit equalizerChanged();
}

void AudioAnalyzer::setGains(const QList<qreal> &db)
{
    QList<qreal> g = m_gains;
    for (int i = 0; i < 10 && i < db.size(); ++i)
        g[i] = qBound<qreal>(-12, db.at(i), 12);
    if (g == m_gains)
        return;
    m_gains = g;
    design();
    emit equalizerChanged();
}

// a peaking filter for each band (the Audio EQ Cookbook's), Q 1.41: an
// octave wide, as the bands are an octave apart
void AudioAnalyzer::design()
{
    const double q = 1.41;
    for (int i = 0; i < 10; ++i) {
        Biquad &f = m_filters[i];
        const double gain = m_gains.at(i);
        f.flat = qAbs(gain) < 0.01;
        if (f.flat)
            continue;
        const double A = std::pow(10.0, gain / 40.0);
        const double w0 = 2 * M_PI * kFreq[i] / kRate;
        const double alpha = std::sin(w0) / (2 * q);
        const double a0 = 1 + alpha / A;
        f.b0 = (1 + alpha * A) / a0;
        f.b1 = (-2 * std::cos(w0)) / a0;
        f.b2 = (1 - alpha * A) / a0;
        f.a1 = (-2 * std::cos(w0)) / a0;
        f.a2 = (1 - alpha / A) / a0;
    }
}

void AudioAnalyzer::equalize(float *frames, int count)
{
    const double pre = std::pow(10.0, m_preamp / 20.0);
    double sum = 0;
    for (int n = 0; n < count; ++n) {
        for (int ch = 0; ch < 2; ++ch) {
            double x = frames[n * 2 + ch] * pre;
            for (int b = 0; b < 10; ++b) {
                const Biquad &f = m_filters[b];
                if (f.flat)
                    continue;
                State &s = m_state[b][ch];
                const double y = f.b0 * x + f.b1 * s.x1 + f.b2 * s.x2 - f.a1 * s.y1 - f.a2 * s.y2;
                s.x2 = s.x1; s.x1 = x;
                s.y2 = s.y1; s.y1 = y;
                x = y;
            }
            // (over full scale: rounded off rather than cut, so it doesn't crackle)
            if (x > 0.9)
                x = 0.9 + 0.1 * std::tanh((x - 0.9) / 0.1);
            else if (x < -0.9)
                x = -0.9 + 0.1 * std::tanh((x + 0.9) / 0.1);
            frames[n * 2 + ch] = float(x);
            sum += x * x;
        }
    }
    const qreal rms = count > 0 ? std::sqrt(sum / (count * 2)) : 0;
    m_outLevel = qBound<qreal>(0, rms > 0 ? (20 * std::log10(rms) + 48) / 48 : 0, 1);
}

void AudioAnalyzer::setVolume(qreal v)
{
    v = qBound<qreal>(0, v, 1);
    if (qFuzzyCompare(v + 1, m_volume + 1))
        return;
    m_volume = v;
    if (m_sink)
        m_sink->setVolume(v);
    emit outputChanged();
}

void AudioAnalyzer::setAudible(bool on)
{
    if (on == m_audible)
        return;
    m_audible = on;
    if (on && m_eqOn)
        openSink();
    else if (!on)
        closeSink();
    emit outputChanged();
}

void AudioAnalyzer::openSink()
{
    if (m_sink || !m_audible || !m_eqOn)
        return;
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return;
    m_sink = new QAudioSink(device, wanted(), this);
    // (about a fifth of a second: enough not to run dry, little enough to keep up)
    m_sink->setBufferSize(kRate * 2 * int(sizeof(float)) / 5);
    m_sink->setVolume(m_volume);
    m_out = m_sink->start();
    m_pending.clear();
    emit outputChanged();
    // what didn't fit goes in as there's room
    auto *timer = new QTimer(m_sink);
    timer->setInterval(20);
    connect(timer, &QTimer::timeout, this, &AudioAnalyzer::pump);
    timer->start();
}

void AudioAnalyzer::closeSink()
{
    if (!m_sink)
        return;
    m_sink->stop();
    delete m_sink;
    m_sink = nullptr;
    m_out = nullptr;
    m_pending.clear();
    emit outputChanged();
}

void AudioAnalyzer::flush()
{
    m_pending.clear();
    if (m_sink && m_eqOn) {
        // (what the stream holds goes too)
        closeSink();
        openSink();
    }
}

void AudioAnalyzer::play(const float *frames, int count)
{
    if (!m_sink || !m_out)
        return;
    m_pending.append(reinterpret_cast<const char *>(frames), qsizetype(count) * 2 * sizeof(float));
    // (more than two seconds behind: the stream's stuck; start again from now)
    if (m_pending.size() > kRate * 2 * qsizetype(sizeof(float)) * 2)
        m_pending = m_pending.right(kRate * 2 * qsizetype(sizeof(float)) / 5);
    pump();
}

void AudioAnalyzer::pump()
{
    if (!m_out || m_pending.isEmpty())
        return;
    const qint64 room = m_sink->bytesFree();
    const qint64 frameBytes = 2 * qint64(sizeof(float));
    const qint64 n = qMin<qint64>(room, m_pending.size()) / frameBytes * frameBytes;
    if (n <= 0)
        return;
    const qint64 wrote = m_out->write(m_pending.constData(), n);
    if (wrote > 0)
        m_pending.remove(0, wrote);
}

int AudioAnalyzer::latency() const
{
    if (!m_sink)
        return 0;
    const qint64 bytes = m_pending.size() + (m_sink->bufferSize() - m_sink->bytesFree());
    return int(bytes * 1000 / (kRate * 2 * qint64(sizeof(float))));
}
