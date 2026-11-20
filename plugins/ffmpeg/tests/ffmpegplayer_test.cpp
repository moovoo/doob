#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>

#include <doob/dspregistry.h>
#include <doob/eq.h>
#include <doob/settingsmanager.h>

#include "equalizerprocessor.h"
#include "ffmpegplayer.h"

#include <doob/mediaitem.h>

namespace {

// Scans the tap spy (the queued deliveries land while the loop spins)
// for a peak at or above the threshold; 0.0 when the timeout expires.
float tapLevelAbove(QSignalSpy *spy, float threshold, int timeoutMs)
{
    int waited = 0;
    while (waited < timeoutMs) {
        for (int i = 0; i < spy->count(); ++i)
            for (const QVariant &arg : spy->at(i))
                if (arg.toFloat() >= threshold)
                    return arg.toFloat();
        QTest::qWait(25);
        waited += 25;
    }
    return 0.0F;
}

// The inverse: the first tap strictly below the threshold (the taps
// in flight behind the stream's latency are above it and are skipped).
float tapLevelBelow(QSignalSpy *spy, float threshold, int timeoutMs)
{
    int waited = 0;
    while (waited < timeoutMs) {
        for (int i = 0; i < spy->count(); ++i)
            for (const QVariant &arg : spy->at(i))
                if (arg.toFloat() < threshold)
                    return arg.toFloat();
        QTest::qWait(25);
        waited += 25;
    }
    return 0.0F;
}

// The gapless contract, read off the raw-frames tap: between the first
// and the last tone-carrying slice, no sizeable slice is silent. A
// drained ring pads a full pass with silence (the gap the handoff
// removes) and surfaces as an all-zero slice; a sizeable slice (many
// periods of the tone) always peaks near the tone's amplitude. A short
// pass (the ring briefly near-empty, the server buffer absorbs it) is
// a few dozen frames — too few periods to peak reliably, and it is not
// a gap.
bool noSilentSliceBetweenTones(QSignalSpy *spy, float threshold,
                               int minSliceFrames = 50)
{
    int first = -1;
    int last = -1;
    for (int i = 0; i < spy->count(); ++i) {
        const QVector<float> frames =
            spy->at(i).at(0).value<QVector<float>>();
        float peak = 0.0F;
        for (const float v : frames)
            peak = qMax(peak, qAbs(v));
        if (peak >= threshold) {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0)
        return false;
    for (int i = first; i <= last; ++i) {
        const QVector<float> frames =
            spy->at(i).at(0).value<QVector<float>>();
        if (frames.size() < minSliceFrames)
            continue;  // a short pass, not a gap
        float peak = 0.0F;
        for (const float v : frames)
            peak = qMax(peak, qAbs(v));
        if (peak < threshold)
            return false;
    }
    return true;
}

void appendU32(QByteArray &a, quint32 v)
{
    for (unsigned i = 0; i < 4; ++i)
        a.append(static_cast<char>((v >> (8U * i)) & 0xffU));
}

void appendU16(QByteArray &a, quint16 v)
{
    // Promote first: a quint16 operand of a shift decays to a signed int.
    for (unsigned i = 0; i < 2; ++i)
        a.append(static_cast<char>(
            (static_cast<quint32>(v) >> (8U * i)) & 0xffU));
}

// A canonical 44-byte RIFF/WAVE header + a 16-bit stereo PCM payload
// (a sine tone at ~0.6% amplitude by default — quiet, so a run that
// ever leaks to the real output stays inaudible; the interleave
// regression uses a second, louder tone to tell the tracks apart in
// the tap).
QByteArray makeWav(int rate, int seconds, double hz, double amplitude = 200.0)
{
    const int channels = 2;
    const int sampleCount = rate * seconds;
    const int dataSize = sampleCount * channels * 2;

    QByteArray data;
    data.append("RIFF");
    appendU32(data, static_cast<quint32>(36 + dataSize));
    data.append("WAVE");
    data.append("fmt ");
    appendU32(data, 16);
    appendU16(data, 1);  // PCM
    appendU16(data, static_cast<quint16>(channels));
    appendU32(data, static_cast<quint32>(rate));
    appendU32(data, static_cast<quint32>(rate * channels * 2));
    appendU16(data, static_cast<quint16>(channels * 2));
    appendU16(data, 16);
    data.append("data");
    appendU32(data, static_cast<quint32>(dataSize));
    for (int i = 0; i < sampleCount; ++i) {
        const short s = static_cast<short>(
            amplitude * std::sin(2.0 * M_PI * hz * i / rate));
        appendU16(data, static_cast<quint16>(s));
        appendU16(data, static_cast<quint16>(s));
    }
    return data;
}

// The test double for the PulseAudio output: it consumes the ring on
// the GUI thread (a 20 ms timer, one pass's worth per tick — the real
// pump's cadence) and mirrors the real pump's semantics: the ring pads
// an underrun with silence, the DSP chain runs over the real audio,
// the boost scales it, and only the kept frames are counted and
// tapped. It re-emits the same signals (ready, the tap, the sink's
// read-back) and keeps the sink's volume/mute itself (it is its own
// server). No server, no thread: the player under test behaves
// exactly as it does with the real output, and the test never touches
// the audio server.
class FakeOutput : public PulseOut
{
    Q_OBJECT
public:
    explicit FakeOutput(QObject *parent = nullptr)
        : PulseOut(parent)
    {
        connect(&m_timer, &QTimer::timeout, this, &FakeOutput::pumpPass);
    }

    bool start(RingBuffer *ring, int sampleRate) override;
    void stop() override;
    void setSinkVolume(float volume) override;
    void setSinkMute(bool muted) override;

private slots:
    void pumpPass();

private:
    DspChain m_chain{this};  // its own: the base's is private
    QTimer m_timer{this};
    RingBuffer *m_ring = nullptr;
    int m_rate = 0;
    QVector<float> m_buf;
    bool m_running = false;
    // The sink's read-back state (the real output reads it from the
    // server; the fake is its own).
    float m_sinkVolume = 1.0F;
    bool m_sinkMute = false;
};

bool FakeOutput::start(RingBuffer *ring, int sampleRate)
{
    if (m_running)
        return false;
    m_running = true;
    m_ring = ring;
    m_rate = sampleRate;
    // One pass's worth of frames (the real pump's per-pass size).
    m_buf.resize(2 * static_cast<qsizetype>(m_rate) / 50);
    m_chain.start(m_rate, 2);
    setPlayedFrames(0);
    m_timer.start(20);
    // The initial sink sync (the real output's stream-ready query),
    // then the stream's ready. The player's connections are in place
    // by now (onOpened connects before it starts), so the emissions
    // land.
    emit sinkVolumeChanged(m_sinkVolume);
    emit sinkMuteChanged(m_sinkMute);
    emit ready();
    return true;
}

void FakeOutput::pumpPass()
{
    if (m_ring == nullptr)
        return;
    // One pass, the real pump's size: the ring pads an underrun with
    // silence, the chain runs over the real audio (it can shorten the
    // chunk), the boost scales it.
    const size_t frames = static_cast<size_t>(m_rate) / 50;
    const size_t real = m_ring->read(m_buf.data(), frames);
    const size_t kept =
        real > 0 ? m_chain.process(m_buf.data(), real, 2) : 0;
    if (kept == 0)
        return;  // silence is never counted (nor tapped)
    const float gain = m_boost.load();
    if (gain != 1.0F) {
        for (qsizetype i = 0; i < static_cast<qsizetype>(kept) * 2; ++i)
            m_buf[i] *= gain;
    }
    setPlayedFrames(playedFrames() + static_cast<qint64>(kept));
    // The tap, unshifted (there is no server buffer to age behind):
    // the per-channel peaks and the raw frames of the pass.
    float leftPeak = 0.0F;
    float rightPeak = 0.0F;
    for (qsizetype i = 0; i < static_cast<qsizetype>(kept); ++i) {
        leftPeak = qMax(leftPeak, qAbs(m_buf[2 * i]));
        rightPeak = qMax(rightPeak, qAbs(m_buf[(2 * i) + 1]));
    }
    emit audioLevels(leftPeak, rightPeak);
    emit audioFrames(m_buf.mid(0, static_cast<int>(kept * 2)),
                     static_cast<quint32>(m_rate));
}

void FakeOutput::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_timer.stop();
    m_chain.stop();
    m_ring = nullptr;
    m_rate = 0;
}

void FakeOutput::setSinkVolume(float volume)
{
    m_sinkVolume = qBound(0.0F, volume, 1.0F);
    emit sinkVolumeChanged(m_sinkVolume);
}

void FakeOutput::setSinkMute(bool muted)
{
    m_sinkMute = muted;
    emit sinkMuteChanged(m_sinkMute);
}

// The player under test: the real driver, its output replaced by the
// fake (the seam: createOutput).
class TestPlayer : public FFmpegPlayer
{
    Q_OBJECT
protected:
    PulseOut *createOutput() override
    {
        return new FakeOutput(this);
    }
};

class FFmpegPlayerTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void playsTheNextTrackAfterEos();
    void reportsStreamInfo();
    void pauseFreezesThePosition();
    void playsAFileWithEscapedPathCharacters();
    void appliesTheEqualizerToTheOutput();
    void boostScalesTheOutput();
    void serverVolumeAndMuteEchoBack();
    void relaysTheFramesTap();
    void gaplessHandoffPlaysThroughTheBoundary();
    void gaplessShortTrackChain();
    void gaplessPauseDuringQueue();
    void gaplessSeekDuringQueue();
    void gaplessQueueNextWithUnsupportedSchemeIsAnError();
    void gaplessInterleaveRegression();
    void gaplessLivePromotionPlaysThroughTheBoundary();
    void gaplessSeekDuringPlayOut();
    void gaplessPauseDuringPlayOut();
    void gaplessQueueNextReportsItsAnswer();

private:
    QString makeToneFile(const QString &name, int seconds,
                         double hz = 440.0, double amplitude = 200.0);

    QTemporaryDir m_dir{QDir::tempPath() + "/doob_ffmpegplayer_test"};
};

QString FFmpegPlayerTest::makeToneFile(const QString &name, int seconds,
                                       double hz, double amplitude)
{
    QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(makeWav(48000, seconds, hz, amplitude));
    f.close();
    return path;
}

void FFmpegPlayerTest::init()
{
    // Mimic the plugin's initialize(): the EQ's setting exists and
    // its DSP is registered (the chain the output runs), the SettingsManager
    // here memory-only — nothing is written to disk.
    SettingsManager *settings = SettingsManager::getInstance();
    QVariantList flat;
    for (int i = 0; i < eq::BandCount; ++i)
        flat << 0.0;
    settings->registerSetting(
        { QString::fromLatin1(eq::GainsKey),
          QStringLiteral("Equalizer gains (dB)"), QString(), flat,
          QStringLiteral("ffmpeg"), });
    DspRegistry *registry = DspRegistry::getInstance();
    registry->unregisterDsp(QString::fromLatin1(eq::Id));  // idempotent
    registry->registerDsp(new EqualizerProcessor(this),
                          QStringLiteral("ffmpeg"));
    // The tone these tests feed is quiet (0.006, a -44 dBFS sine),
    // under the built-in silence skip's -40 dBFS threshold — the skip
    // would eat the whole track. It is covered by its own test, so it
    // stays out of the player's way here.
    registry->setEnabled("silenceskip", false);
}

void FFmpegPlayerTest::cleanup()
{
    SettingsManager *settings = SettingsManager::getInstance();
    DspRegistry::getInstance()
        ->unregisterDsp(QString::fromLatin1(eq::Id));
    settings->unregisterSetting(QString::fromLatin1(eq::GainsKey));
}

void FFmpegPlayerTest::playsTheNextTrackAfterEos()
{
    const QString path1 = makeToneFile("tone1.wav", 1);
    const QString path2 = makeToneFile("tone2.wav", 1);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path1);
    player.play(item);
    int waited = 0;
    while (finishedSpy.count() < 1 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(finishedSpy.count(), 1);

    // The regression this guards: the EOS teardown used to fire a
    // stale "connection failed" that queued across threads and landed
    // on the next track's output, killing it — and every track after
    // it. Track 2 must come up and finish too.
    item.source = QUrl::fromLocalFile(path2);
    player.play(item);
    waited = 0;
    while (finishedSpy.count() < 2 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);
}

// The driver reports the decoded stream's info (the codec, container
// and the actual parameters) when the source opens — the player's own
// streamInfo signal.
void FFmpegPlayerTest::reportsStreamInfo()
{
    const QString path = makeToneFile("tone_info.wav", 1);
    TestPlayer player;
    QSignalSpy infoSpy(&player, &FFmpegPlayer::streamInfo);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);
    int waited = 0;
    while (infoSpy.count() < 1 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(infoSpy.count(), 1);
    const QVariantMap info = infoSpy.first().first().toMap();
    QCOMPARE(info.value("codec").toString(), QStringLiteral("pcm_s16le"));
    QCOMPARE(info.value("format").toString(), QStringLiteral("wav"));
    QCOMPARE(info.value("sampleRate").toInt(), 48000);
    QCOMPARE(info.value("channels").toInt(), 2);
    QCOMPARE(info.value("channelLayout").toString(),
             QStringLiteral("stereo"));
    player.stop();
}

void FFmpegPlayerTest::pauseFreezesThePosition()
{
    // Longer than the ring (2 s), so the decode is paced by the output
    // and the track does not eos before the pause.
    const QString path = makeToneFile("tone_pause.wav", 4);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);
    int waited = 0;
    while (player.positionMs() < 300 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    player.pause();
    const qint64 atPause = player.positionMs();

    // While paused the output keeps pulling silence: the position must
    // not roll on it (and the queued audio must not keep playing).
    QTest::qWait(500);
    const qint64 afterPause = player.positionMs();
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QVERIFY2(afterPause - atPause <= 50,
             qPrintable(QStringLiteral("position rolled %1 ms while paused")
                            .arg(afterPause - atPause)));

    // Resume: playback continues to the end.
    player.resume();
    waited = 0;
    while (finishedSpy.count() < 1 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
}

void FFmpegPlayerTest::playsAFileWithEscapedPathCharacters()
{
    // The directory and file name carry the characters QUrl
    // percent-encodes in toString() (brackets, an accent) — as real
    // library folders have them ("… [CBR-320kbps]", "Café"). The
    // file: protocol looks the path up literally, so the worker must
    // hand it the decoded path, not the URL string, or the open fails.
    const QString subdir =
        QStringLiteral("Coldplay Parachutes - Rock 2000 [CBR-320kbps]");
    QVERIFY(QDir(m_dir.path()).mkpath(subdir));
    const QString path =
        makeToneFile(subdir + QStringLiteral("/01 Yellow - Café.wav"), 1);
    QVERIFY(!path.isEmpty());
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);
    int waited = 0;
    while (finishedSpy.count() < 1 && errorSpy.count() < 1
            && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0) {
        const QString message = errorSpy.first().first().toString();
        if (message.startsWith(QLatin1String("Cannot open")))
            QFAIL(qPrintable(
                QStringLiteral("the open failed on the escaped path: %1")
                    .arg(message)));
        QSKIP("no PulseAudio server in this environment");
    }
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
}

// The full chain: the SettingsManager's changed() reaches the player,
// the player pushes the gains onto the output, and the output shapes
// the frames before the tap — so the tap (what the meter shows) rises
// with a boost and falls back with a bypass.
void FFmpegPlayerTest::appliesTheEqualizerToTheOutput()
{
    // Longer than the ring (2 s), so the decode is paced by the output
    // and the track outlives the setting changes.
    const QString path = makeToneFile("tone_eq.wav", 4);
    SettingsManager *settings = SettingsManager::getInstance();
    TestPlayer player;
    QSignalSpy levelsSpy(&player, &FFmpegPlayer::audioLevels);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);

    // 1. The flat baseline (the default gains): wait for a tap that
    //    carries the tone (~0.6% amplitude).
    const float flat = tapLevelAbove(&levelsSpy, 0.003F, 5000);
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QVERIFY2(flat >= 0.003F,
             qPrintable(QStringLiteral("the tone never reached the tap "
                                       "(flat peak %1)")
                            .arg(flat)));

    // 2. Boost the 500 Hz band (index 4) by +12 dB: the 440 Hz tone
    //    sits inside its -3 dB bandwidth, so the tap must rise.
    QVariantList list =
        settings->value(QString::fromLatin1(eq::GainsKey)).toList();
    list[4] = 12.0;
    settings->setValue(QString::fromLatin1(eq::GainsKey), list);
    const float boosted = tapLevelAbove(&levelsSpy, 2.0F * flat, 5000);
    QVERIFY2(boosted >= 2.0F * flat,
             qPrintable(QStringLiteral("the boost never reached the tap "
                                       "(flat %1, boosted %2)")
                            .arg(flat).arg(boosted)));

    // 3. Disable the DSP (the registry's entry — the preferences'
    //    switch): the tap falls back to the flat level (the boosted
    //    taps in flight behind the stream's latency are skipped by
    //    the threshold).
    DspRegistry::getInstance()->setEnabled(QString::fromLatin1(eq::Id),
                                           false);
    const float bypassed = tapLevelBelow(&levelsSpy, 1.5F * flat, 5000);
    QVERIFY2(bypassed > 0.0F && bypassed < 1.5F * flat,
             qPrintable(QStringLiteral("the bypass never reached the tap "
                                       "(flat %1, bypassed %2)")
                            .arg(flat).arg(bypassed)));

    player.stop();
}

// The driver's boost: a linear scale on the frames as they leave the
// output, visible at the tap (what the meter shows) — half gain
// halves the tone's envelope, full gain restores it.
void FFmpegPlayerTest::boostScalesTheOutput()
{
    // Longer than the ring (2 s), so the decode is paced by the output
    // and the track outlives the gain changes.
    const QString path = makeToneFile("tone_boost.wav", 4);
    TestPlayer player;
    QSignalSpy levelsSpy(&player, &FFmpegPlayer::audioLevels);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);

    // 1. The flat baseline (full gain): wait for a tap that carries
    //    the tone (~0.6% amplitude).
    const float full = tapLevelAbove(&levelsSpy, 0.003F, 5000);
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QVERIFY2(full >= 0.003F,
             qPrintable(QStringLiteral("the tone never reached the tap "
                                       "(full peak %1)")
                            .arg(full)));

    // 2. Half gain: the tap falls to ~half the flat level (the full
    //    taps in flight behind the stream's latency are above the
    //    threshold and are skipped).
    player.setBoost(0.5F);
    const float half = tapLevelBelow(&levelsSpy, 0.75F * full, 5000);
    QVERIFY2(half > 0.0F && half <= 0.75F * full,
             qPrintable(QStringLiteral("half gain never reached the tap "
                                       "(full %1, half %2)")
                            .arg(full).arg(half)));

    // 3. Full gain again: the tap rises back to the flat level.
    player.setBoost(1.0F);
    const float restored = tapLevelAbove(&levelsSpy, 0.75F * full, 5000);
    QVERIFY2(restored >= 0.75F * full,
             qPrintable(QStringLiteral("the restored gain never reached "
                                       "the tap (full %1, restored %2)")
                            .arg(full).arg(restored)));

    player.stop();
}

// The driver's volume and mute go to the server side of its output
// (the sink input) and come back through the sink signals: a set
// lands on the server and is reported (the read-back the
// PlaybackManager trusts — the server is the source of truth).
void FFmpegPlayerTest::serverVolumeAndMuteEchoBack()
{
    // Longer than the ring (2 s), so the decode is paced by the output
    // and the track outlives the server changes.
    const QString path = makeToneFile("tone_sink.wav", 4);
    TestPlayer player;
    QSignalSpy volumeSpy(&player, &FFmpegPlayer::sinkVolumeChanged);
    QSignalSpy muteSpy(&player, &FFmpegPlayer::sinkMuteChanged);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);
    int waited = 0;
    while (player.positionMs() < 300 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");

    // The initial sync reported the server's default (full volume,
    // unmuted) when the stream came up.
    QTRY_VERIFY(volumeSpy.count() >= 1);
    QTRY_VERIFY(muteSpy.count() >= 1);

    // The volume set: the server answers, and the report carries the
    // level that landed.
    player.setVolume(0.4F);
    waited = 0;
    while (true) {
        bool seen = false;
        for (int i = 0; i < volumeSpy.count(); ++i)
            if (qAbs(volumeSpy.at(i).first().toFloat() - 0.4F) < 0.02F) {
                seen = true;
                break;
            }
        if (seen)
            break;
        if (waited >= 5000)
            QFAIL("no sink volume report for the set level");
        QTest::qWait(10);
        waited += 10;
    }

    // The mute set: same, for the flag.
    player.setMute(true);
    waited = 0;
    while (true) {
        bool seen = false;
        for (int i = 0; i < muteSpy.count(); ++i)
            if (muteSpy.at(i).first().toBool()) {
                seen = true;
                break;
            }
        if (seen)
            break;
        if (waited >= 5000)
            QFAIL("no sink mute report for the set mute");
        QTest::qWait(10);
        waited += 10;
    }

    player.stop();
    QCOMPARE(errorSpy.count(), 0);
}

// The output's raw-frames tap (the spectrum's feed) is re-emitted as
// the driver's audioFrames: it arrives together with the levels, and
// it carries the tone (interleaved stereo, both channels).
void FFmpegPlayerTest::relaysTheFramesTap()
{
    const QString path = makeToneFile("tone_frames.wav", 4);
    TestPlayer player;
    QSignalSpy levelsSpy(&player, &FFmpegPlayer::audioLevels);
    QSignalSpy framesSpy(&player, &FFmpegPlayer::audioFrames);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem item;
    item.source = QUrl::fromLocalFile(path);
    player.play(item);

    // A frames tap carrying the tone (a non-zero sample).
    int index = -1;
    int waited = 0;
    while (index < 0 && waited < 5000) {
        for (int i = 0; i < framesSpy.count(); ++i) {
            const QVector<float> frames =
                framesSpy.at(i).at(0).value<QVector<float>>();
            for (const float v : frames)
                if (qAbs(v) > 0.001F) {
                    index = i;
                    break;
                }
        }
        if (index < 0) {
            QTest::qWait(25);
            waited += 25;
        }
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QVERIFY2(index >= 0, "no frames tap carrying the tone");
    QCOMPARE(framesSpy.count(), levelsSpy.count());  // emitted together
    QCOMPARE(framesSpy.at(index).at(1).toUInt(), 48000U);

    // The tone is in the raw frames (interleaved stereo): the slice's
    // peak is at the tone's amplitude (the sine crosses zero, so not
    // every sample is above the threshold).
    const QVector<float> frames =
        framesSpy.at(index).at(0).value<QVector<float>>();
    QVERIFY(frames.size() >= 2);
    QVERIFY(frames.size() % 2 == 0);
    float peak = 0.0F;
    for (const float v : frames)
        peak = qMax(peak, qAbs(v));
    QVERIFY(peak > 0.001F);

    player.stop();
}

// The gapless handoff: the next item is queued while the current one
// is in its final seconds, and when the current one's last frame is
// played the queued one takes over — finished fires at the real
// boundary (not the container end), the duration switches, the
// position restarts, and the tap shows no silence in between (the
// ring never drained across the boundary).
void FFmpegPlayerTest::gaplessHandoffPlaysThroughTheBoundary()
{
    const QString pathA = makeToneFile("tone_gapless_a.wav", 1, 440.0);
    const QString pathB = makeToneFile("tone_gapless_b.wav", 1, 880.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy durationSpy(&player, &FFmpegPlayer::durationChanged);
    QSignalSpy positionSpy(&player, &FFmpegPlayer::positionChanged);
    QSignalSpy framesSpy(&player, &FFmpegPlayer::audioFrames);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);
    QElapsedTimer clock;

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    clock.start();
    player.play(itemA);
    // The pre-roll: the manager queues the next item in the final
    // seconds (here, right away — the 1 s track is inside its lead).
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    player.queueNext(itemB);
    int waited = 0;
    while (finishedSpy.count() < 1 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(finishedSpy.count(), 1);

    // The boundary landed at track A's end, not immediately: its last
    // frame is what triggered the handoff (the position approached A's
    // length, and the boundary's finished is at least half a second in).
    const qint64 elapsed = clock.elapsed();
    QVERIFY2(elapsed >= 500,
             qPrintable(QStringLiteral("the boundary came too early: %1 ms")
                            .arg(elapsed)));
    qint64 maxBefore = 0;
    for (int i = 0; i < positionSpy.count(); ++i)
        maxBefore = qMax(maxBefore, positionSpy.at(i).first().toLongLong());
    QVERIFY2(maxBefore >= 700,
             qPrintable(QStringLiteral("the position never approached A's "
                                       "end (max %1 ms)")
                            .arg(maxBefore)));

    // The handoff: the duration is B's, and the position restarted
    // near zero (the offset re-anchored at the boundary).
    QCOMPARE(durationSpy.last().first().toLongLong(), 1000LL);
    QVERIFY2(player.positionMs() < 300,
             qPrintable(QStringLiteral("the position did not restart: %1 ms")
                            .arg(player.positionMs())));

    // Let B play out (its EOS, unqueued, ends the session) and check
    // the tap: from the first tone to the last, no silence in between.
    waited = 0;
    while (finishedSpy.count() < 2 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY2(noSilentSliceBetweenTones(&framesSpy, 0.003F),
             "a silent slice in the tap: the ring drained across the "
             "boundary (the gap the handoff removes)");
}

// The corpse-promotion path: the queued item decodes to its end before
// the boundary (its frames just sit in the ring), and a second queued
// item is armed for it. Two handoffs in a row, no gaps.
void FFmpegPlayerTest::gaplessShortTrackChain()
{
    const QString pathA = makeToneFile("tone_chain_a.wav", 1, 440.0);
    const QString pathB = makeToneFile("tone_chain_b.wav", 1, 880.0);
    const QString pathC = makeToneFile("tone_chain_c.wav", 1, 660.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy durationSpy(&player, &FFmpegPlayer::durationChanged);
    QSignalSpy framesSpy(&player, &FFmpegPlayer::audioFrames);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    MediaItem itemC;
    itemC.source = QUrl::fromLocalFile(pathC);
    player.play(itemA);
    player.queueNext(itemB);  // 1 s of B decodes in ms: a corpse

    int waited = 0;
    while (finishedSpy.count() < 1 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(durationSpy.last().first().toLongLong(), 1000LL);

    // The manager's move at the boundary: arm the next item for the
    // promoted one (its corpse frames are already in the ring).
    player.queueNext(itemC);
    waited = 0;
    while (finishedSpy.count() < 3 && waited < 20000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 3);
    QCOMPARE(durationSpy.last().first().toLongLong(), 1000LL);
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY2(noSilentSliceBetweenTones(&framesSpy, 0.003F),
             "a silent slice in the tap: the chain gapless-played");
}

// A pause with a queued item kills the queued decode (its buffer goes
// with the ring's drain); the resume re-decodes it and the handoff
// still lands at the current item's end.
void FFmpegPlayerTest::gaplessPauseDuringQueue()
{
    // Longer than the ring (2 s), so the decode is paced by the output
    // and the track outlives the pause.
    const QString pathA = makeToneFile("tone_gapless_pause_a.wav", 4, 440.0);
    const QString pathB = makeToneFile("tone_gapless_pause_b.wav", 1, 880.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy durationSpy(&player, &FFmpegPlayer::durationChanged);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 300 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    player.queueNext(itemB);

    player.pause();
    const qint64 atPause = player.positionMs();
    QTest::qWait(500);
    QVERIFY2(player.positionMs() - atPause <= 50,
             qPrintable(QStringLiteral("position rolled %1 ms while paused")
                            .arg(player.positionMs() - atPause)));

    player.resume();
    waited = 0;
    while (finishedSpy.count() < 1 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(finishedSpy.count(), 1);
    // The handoff landed (the re-decoded queued item took over).
    QCOMPARE(durationSpy.last().first().toLongLong(), 1000LL);
    QVERIFY2(player.positionMs() < 300,
             qPrintable(QStringLiteral("the position did not restart: %1 ms")
                            .arg(player.positionMs())));

    // B plays out to its end.
    waited = 0;
    while (finishedSpy.count() < 2 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);
}

// A seek with a queued item kills the queued decode (the ring's drain
// takes its buffered frames) and the pending handoff: the current
// track plays on from the seek to its own end (the plain finished).
void FFmpegPlayerTest::gaplessSeekDuringQueue()
{
    const QString pathA = makeToneFile("tone_gapless_seek_a.wav", 4, 440.0);
    const QString pathB = makeToneFile("tone_gapless_seek_b.wav", 1, 880.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 300 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    player.queueNext(itemB);

    player.setPositionMs(2000);
    waited = 0;
    while (qAbs(player.positionMs() - 2000) > 400 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QVERIFY2(qAbs(player.positionMs() - 2000) <= 400,
             qPrintable(QStringLiteral("the seek did not land: %1 ms")
                            .arg(player.positionMs())));

    // A plays on to its end (the queued B is gone with the drain): one
    // finished, the plain one.
    waited = 0;
    while (finishedSpy.count() < 1 && waited < 15000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
}

// queueNext with a source the driver cannot decode reports an error
// (the manager marks the armed item and stops) — it does not take
// down the current track.
void FFmpegPlayerTest::gaplessQueueNextWithUnsupportedSchemeIsAnError()
{
    const QString pathA = makeToneFile("tone_gapless_scheme_a.wav", 4, 440.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 300 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");

    MediaItem itemB;
    itemB.source = QUrl(QStringLiteral("doobtest:xyz"));
    player.queueNext(itemB);

    waited = 0;
    while (errorSpy.count() < 1 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.first().first().toString().contains("scheme"));

    // The current track kept playing (the error is the manager's to
    // act on, not the driver's to enforce): the position keeps
    // advancing past the one it had when the error landed.
    const qint64 atError = player.positionMs();
    waited = 0;
    while (player.positionMs() <= atError && waited < 2000) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY2(player.positionMs() > atError, "the current track stopped");
    player.stop();
    QCOMPARE(finishedSpy.count(), 0);
}

// The two-writer interleave regression (the Phase 6a bug): with the
// queued decode writing into the shared ring while the current decode
// was still running, the ring's FIFO order was the write order — the
// two tracks played interleaved, a chunk of one after a chunk of the
// other. The gate keeps the queued frames out of the ring (its private
// staging) until the current's eos, so after the first B-tone slice
// the tap carries no A-tone slice. The two tones differ in amplitude
// (A ≈ 0.006, B ≈ 0.030): an A chunk mid-B is a slice peaking between
// them, and a sizeable slice of a pure tone peaks near its amplitude.
void FFmpegPlayerTest::gaplessInterleaveRegression()
{
    const float kLow = 0.002F;   // under A's smallest sizeable slice
    const float kMid = 0.015F;   // between the tones' peaks
    const float kHigh = 0.02F;   // above A's peak, under B's

    // A's decode outlives the queue (5 s of audio against the 2 s
    // ring — its last two seconds are still being written when B
    // queues), so the two writers would race the ring in the old
    // code.
    const QString pathA = makeToneFile("tone_interleave_a.wav", 5, 440.0,
                                       200.0);
    const QString pathB = makeToneFile("tone_interleave_b.wav", 4, 880.0,
                                       1000.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy framesSpy(&player, &FFmpegPlayer::audioFrames);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 1000 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    QVERIFY(player.queueNext(itemB));

    // Both boundaries (A's tail plays out, then B to its end).
    waited = 0;
    while (finishedSpy.count() < 2 && waited < 20000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);

    // After the first B-tone slice, no sizeable slice carries A's
    // amplitude (an interleaved A chunk) — and no drained-ring gap
    // anywhere between the tones.
    int firstHigh = -1;
    for (int i = 0; i < framesSpy.count(); ++i) {
        const QVector<float> frames =
            framesSpy.at(i).at(0).value<QVector<float>>();
        float peak = 0.0F;
        for (const float v : frames)
            peak = qMax(peak, qAbs(v));
        if (peak >= kHigh) {
            firstHigh = i;
            break;
        }
    }
    QVERIFY2(firstHigh >= 0, "the tap never carried B's tone");
    for (int i = firstHigh; i < framesSpy.count(); ++i) {
        const QVector<float> frames =
            framesSpy.at(i).at(0).value<QVector<float>>();
        if (frames.size() < 50)
            continue;  // a short pass, not a chunk
        float peak = 0.0F;
        for (const float v : frames)
            peak = qMax(peak, qAbs(v));
        QVERIFY2(peak < kLow || peak >= kMid,
                 qPrintable(
                     QStringLiteral("an A-tone slice at tap %1 after B "
                                    "started (peak %2): the tracks played "
                                    "interleaved")
                         .arg(i)
                         .arg(peak)));
    }
    QVERIFY2(noSilentSliceBetweenTones(&framesSpy, 0.003F),
             "a silent slice in the tap: the ring drained (a gap)");
}

// The live-promotion case: the promoted worker is still decoding at
// the boundary (B outlives the 2 s ring) — the handoff must ungate it
// (a gate that closed on the promotion would detour its mid-track
// frames to the staging: silence mid-track, a burst at the end).
void FFmpegPlayerTest::gaplessLivePromotionPlaysThroughTheBoundary()
{
    const QString pathA = makeToneFile("tone_livepromote_a.wav", 1, 440.0);
    const QString pathB =
        makeToneFile("tone_livepromote_b.wav", 8, 880.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy framesSpy(&player, &FFmpegPlayer::audioFrames);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    QVERIFY(player.queueNext(itemB));

    // The boundary (A's end), then B's own end (~8 s later) — B's
    // decode is live throughout its first seconds.
    int waited = 0;
    while (finishedSpy.count() < 2 && waited < 20000) {
        QTest::qWait(25);
        waited += 25;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);

    // B's full play-out carries no drained-ring gap (the mid-track
    // silence a gate-closed-on-promotion would leave).
    QVERIFY2(noSilentSliceBetweenTones(&framesSpy, 0.003F),
             "a silent slice in B's play-out: the promoted worker's "
             "frames detoured to the staging (the gate closed on the "
             "promotion)");
}

// A seek in the play-out window (the current a corpse — its tail
// plays out of the ring, the display already shows the armed item):
// the boundary resolves at the seek (the handoff), the seek lands on
// the promoted track, and the playback runs to its end — no silent
// stop, no frozen position (the reported "slider stuck at the end").
void FFmpegPlayerTest::gaplessSeekDuringPlayOut()
{
    const QString pathA = makeToneFile("tone_playoutseek_a.wav", 5, 440.0);
    const QString pathB = makeToneFile("tone_playoutseek_b.wav", 4, 880.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy durationSpy(&player, &FFmpegPlayer::durationChanged);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 1000 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    QVERIFY(player.queueNext(itemB));

    // Into the play-out window: A's decode is done (its tail is in
    // the ring), B's frames are in the ring too (a corpse at the
    // boundary).
    waited = 0;
    while (player.positionMs() < 4500 && waited < 8000) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY2(player.positionMs() >= 4500,
             qPrintable(QStringLiteral("A's tail never reached 4.5 s: %1 ms")
                            .arg(player.positionMs())));

    // The seek (the display is on B — the seek is B's).
    player.setPositionMs(1000);
    waited = 0;
    while (qAbs(player.positionMs() - 1000) > 400 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (errorSpy.count() > 0)
        QSKIP("no PulseAudio server in this environment");
    QVERIFY2(qAbs(player.positionMs() - 1000) <= 400,
             qPrintable(QStringLiteral("the seek did not land: %1 ms")
                            .arg(player.positionMs())));
    // The boundary resolved at the seek (the handoff fired).
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(durationSpy.last().first().toLongLong(), 4000LL);

    // B runs from the seek to its end — no silent stop.
    waited = 0;
    while (finishedSpy.count() < 2 && waited < 10000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);
}

// A pause in the play-out window (the current a corpse): the boundary
// resolves at the pause (the handoff — finished fires there, not up
// to 2 s into the promoted track), the promoted track is re-decoded
// from the pause point, and the resume continues it to its end.
void FFmpegPlayerTest::gaplessPauseDuringPlayOut()
{
    const QString pathA = makeToneFile("tone_playoutpause_a.wav", 5, 440.0);
    const QString pathB = makeToneFile("tone_playoutpause_b.wav", 4, 880.0);
    TestPlayer player;
    QSignalSpy finishedSpy(&player, &FFmpegPlayer::finished);
    QSignalSpy durationSpy(&player, &FFmpegPlayer::durationChanged);
    QSignalSpy errorSpy(&player, &FFmpegPlayer::errorOccurred);

    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);
    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 1000 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    MediaItem itemB;
    itemB.source = QUrl::fromLocalFile(pathB);
    QVERIFY(player.queueNext(itemB));

    // Into the play-out window (A's tail, B a corpse in the ring).
    waited = 0;
    while (player.positionMs() < 4500 && waited < 8000) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY2(player.positionMs() >= 4500,
             qPrintable(QStringLiteral("A's tail never reached 4.5 s: %1 ms")
                            .arg(player.positionMs())));

    // The pause: the boundary resolves with it (the handoff — B's
    // duration, finished#1 — not a stale boundary firing later).
    player.pause();
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(durationSpy.last().first().toLongLong(), 4000LL);
    QCOMPARE(errorSpy.count(), 0);

    // The resume: B plays from the pause point (near its top) to its
    // end.
    player.resume();
    waited = 0;
    while (finishedSpy.count() < 2 && waited < 10000) {
        QTest::qWait(25);
        waited += 25;
    }
    QCOMPARE(finishedSpy.count(), 2);
    QCOMPARE(errorSpy.count(), 0);
}

// The contract's answer: queueNext reports refused when there is no
// current item (or it is stopped), queued when it queued — and the
// idempotent same-source re-queue reports queued too (the manager's
// re-queue after a seek must not clear its latch).
void FFmpegPlayerTest::gaplessQueueNextReportsItsAnswer()
{
    const QString pathA = makeToneFile("tone_qnreport_a.wav", 1, 440.0);
    TestPlayer player;
    MediaItem itemA;
    itemA.source = QUrl::fromLocalFile(pathA);

    QCOMPARE(player.queueNext(itemA), false);  // nothing playing

    player.play(itemA);
    int waited = 0;
    while (player.positionMs() < 300 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    if (player.positionMs() == 0)
        QSKIP("no PulseAudio server in this environment");
    QCOMPARE(player.queueNext(itemA), true);   // a fresh queue
    QCOMPARE(player.queueNext(itemA), true);   // idempotent

    player.stop();
    QCOMPARE(player.queueNext(itemA), false);  // stopped
}

} // namespace

QTEST_MAIN(FFmpegPlayerTest)
#include "ffmpegplayer_test.moc"
