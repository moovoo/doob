#include <QtTest>

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <unistd.h>

extern "C" {
#include <pulse/context.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pulse/stream.h>
}

#include "capturepump.h"
#include "privatepulse.h"

namespace {

// The lead probe's stream state (the mainloop's thread).
void leadStreamState(pa_stream *st, void *userdata)
{
    *static_cast<std::atomic<int> *>(userdata) =
        static_cast<int>(pa_stream_get_state(st));
}

class PrivatePulseTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void daemonStartsAndConfigures();
    void staleDirsAreSwept();
    void captureGetsTheSink();
    void aStalledConsumerLosesNoAudio();
    void theClientKeepsALead();

private:
    // A probe stream on the named sink (the server's sink is up when
    // a stream lands on it).
    static bool sinkIsUp(const QString &server, const QString &sink)
    {
        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_S16LE;
        spec.rate = 44100;
        spec.channels = 2;
        pa_channel_map map{};
        pa_channel_map_init_stereo(&map);
        pa_simple *simple = pa_simple_new(
            server.toUtf8().constData(), "test-probe", PA_STREAM_PLAYBACK,
            sink.toUtf8().constData(), "test-probe", &spec, &map, nullptr,
            nullptr);
        if (simple == nullptr)
            return false;
        pa_simple_free(simple);
        return true;
    }

    // A dead pid (a spawned-and-finished child).
    static qint64 deadPid()
    {
        QProcess proc;
        proc.start(QStringLiteral("true"));
        proc.waitForFinished(5000);
        return proc.processId();
    }

    // The test's sine writer: a 440 Hz tone, seconds long, paced at
    // real time (pa_simple_write blocks once the server buffer is
    // full; the sleep keeps the rest at the wall clock).
    static void writeTone(const QString &server, const QString &sink,
                          const std::atomic<bool> &stopFlag,
                          int seconds = 2)
    {
        pa_sample_spec spec{};
        spec.format = PA_SAMPLE_S16LE;
        spec.rate = 44100;
        spec.channels = 2;
        pa_channel_map map{};
        pa_channel_map_init_stereo(&map);
        pa_simple *simple = pa_simple_new(
            server.toUtf8().constData(), "test-writer", PA_STREAM_PLAYBACK,
            sink.toUtf8().constData(), "test-writer", &spec, &map, nullptr,
            nullptr);
        if (simple == nullptr)
            return;
        constexpr qsizetype kChunkFrames = 4410; // 100 ms at 44100
        QVector<int16_t> buffer(2 * kChunkFrames);
        const qsizetype kTotalFrames =
            static_cast<qsizetype>(seconds) * 44100;
        qsizetype written = 0;
        while (written < kTotalFrames && !stopFlag.load()) {
            const qsizetype n = qMin(kChunkFrames, kTotalFrames - written);
            for (qsizetype i = 0; i < n; ++i) {
                const double t = static_cast<double>(written + i) / 44100.0;
                buffer[i * 2] = buffer[(i * 2) + 1] = static_cast<int16_t>(
                    16000.0 * std::sin(2.0 * M_PI * 440.0 * t));
            }
            pa_simple_write(simple, buffer.constData(), n * 4, nullptr);
            written += n;
            QThread::msleep(100);
        }
        pa_simple_free(simple);
    }

    QTemporaryDir m_data; // the base dir for the per-launch directories
};

void PrivatePulseTest::init()
{
}

void PrivatePulseTest::cleanup()
{
}

void PrivatePulseTest::daemonStartsAndConfigures()
{
    PrivatePulse pulse;
    pulse.setBaseDir(m_data.path());
    QVERIFY(pulse.start());
    QVERIFY(pulse.running());

    // The runtime files landed in the per-launch directory.
    QCOMPARE(pulse.serverString(),
             QStringLiteral("unix:%1/native").arg(pulse.socketDir()));
    QVERIFY(QFileInfo(pulse.socketDir() + "/native").exists());
    QVERIFY(QFileInfo(pulse.socketDir() + "/pid").exists());

    // The null sink is up: a probe stream lands on it.
    QVERIFY2(sinkIsUp(pulse.serverString(),
                      QString::fromLatin1(PrivatePulse::sinkName())),
             "the server's sink is not up");

    // A stop is clean (the server's thread is joined, the socket
    // unlinked) and idempotent.
    pulse.stop();
    pulse.stop();
    QVERIFY(!pulse.running());
    QVERIFY(!QFileInfo::exists(pulse.socketDir() + "/native"));
}

void PrivatePulseTest::staleDirsAreSwept()
{
    // A stale dir: a dead process' pid file.
    const QString stale = m_data.path() + "/pulse-"
        + QByteArray::number(deadPid()) + "-0";
    QVERIFY(QDir().mkpath(stale));
    {
        QFile file(stale + "/pid");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::number(deadPid()));
    }
    // A stale dir without a pid file at all.
    const QString orphan = m_data.path() + "/pulse-1-9";
    QVERIFY(QDir().mkpath(orphan));
    // A live dir: this test process' pid (alive) — it must be kept.
    const QString live = m_data.path() + "/pulse-"
        + QByteArray::number(static_cast<qlonglong>(::getpid())) + "-5";
    QVERIFY(QDir().mkpath(live));
    {
        QFile file(live + "/pid");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::number(static_cast<qlonglong>(::getpid())));
    }

    PrivatePulse pulse;
    pulse.setBaseDir(m_data.path());
    QVERIFY(pulse.start());
    QVERIFY(!QFileInfo::exists(stale));
    QVERIFY(!QFileInfo::exists(orphan));
    QVERIFY(QFileInfo::exists(live));
    pulse.stop();
}

void PrivatePulseTest::captureGetsTheSink()
{
    PrivatePulse pulse;
    pulse.setBaseDir(m_data.path());
    QVERIFY(pulse.start());

    // The capture (the pump on the ring the server's sink writes
    // into).
    CapturePump pump(&pulse.ring(), PrivatePulse::kSampleRate,
                     PrivatePulse::kChannels);
    QVector<int16_t> samples;
    QObject::connect(&pump, &CapturePump::chunk, &pump,
                     [&samples](const QByteArray &frames, int) {
                         // The chunk's bytes are interleaved s16le.
                         QDataStream stream{frames};
                         stream.setByteOrder(QDataStream::LittleEndian);
                         int16_t sample{};
                         while (!stream.atEnd()) {
                             stream >> sample;
                             samples.append(sample);
                         }
                     });
    QVERIFY(pump.start());
    QVERIFY(pump.running());

    // The writer (a tone into the null sink) on its own thread.
    std::atomic<bool> stopWriter{false};
    std::thread writer{[&] {
        writeTone(pulse.serverString(), PrivatePulse::sinkName(), stopWriter);
    }};

    // Collect for a fixed wall time (the writer runs a 2 s tone; the
    // rest of the window is the ring's silence — the level checks
    // below are per-window, so the padding cannot bias them).
    int waited = 0;
    while (waited < 3500) {
        QTest::qWait(25);
        waited += 25;
    }
    stopWriter = true;
    writer.join();
    pump.stop();
    pulse.stop();

    QVERIFY2(samples.size() > static_cast<qsizetype>(44100) * 2,
             qPrintable("the capture got no audio (got "
                          + QString::number(samples.size()) + " samples)"));
    // The tone's level: a 16000-amplitude sine (the server's sink is
    // bit-transparent for its native format — the ring carries what
    // the sink played). The peak is global; the rms is the best
    // 100 ms window (a window fully inside the tone, immune to the
    // silence padding).
    int peak = 0;
    for (const int16_t s : samples)
        peak = qMax(peak, qAbs(static_cast<int>(s)));
    const int window = 4410 * 2; // 100 ms of stereo
    double bestRms = 0;
    for (int from = 0; from + window <= samples.size(); from += window) {
        quint64 squareSum = 0;
        for (int i = from; i < from + window; ++i) {
            const int a = qAbs(static_cast<int>(samples.at(i)));
            squareSum += static_cast<quint64>(a) * static_cast<quint64>(a);
        }
        bestRms = qMax(
            bestRms, std::sqrt(static_cast<double>(squareSum) / window));
    }
    QVERIFY2(peak > 14000 && peak < 18000,
             qPrintable(QStringLiteral("unexpected peak %1").arg(peak)));
    QVERIFY2(bestRms > 9500 && bestRms < 13000,
             qPrintable(QStringLiteral("unexpected rms %1").arg(bestRms)));
    // The format: the server's pinned spec.
    QCOMPARE(pump.sampleRate(), PrivatePulse::kSampleRate);
    QCOMPARE(pump.channels(), PrivatePulse::kChannels);
}

void PrivatePulseTest::aStalledConsumerLosesNoAudio()
{
    PrivatePulse pulse;
    pulse.setBaseDir(m_data.path());
    QVERIFY(pulse.start());

    // The capture with a consumer that stalls once, mid-tone, for a
    // second (the relay's blocking write against a hiccuping system
    // sink). A persistent stall is not the failure mode: a slow pump
    // plays a slow, gapless tone (the dropped audio is always the
    // newest, which the pump never reaches). The audible clip comes
    // from the pump RESUMING: the ring's old data plays, then the
    // dropped middle shows up as a gap. The decoupling (the ring,
    // the server's virtual buffer) must absorb the 1 s stall: the
    // tone comes out whole, delayed, not gapped.
    CapturePump pump(&pulse.ring(), PrivatePulse::kSampleRate,
                     PrivatePulse::kChannels);
    QVector<int16_t> samples;
    bool stalled = false;
    // Direct: the stall (the 1 s sleep) must run on the pump thread
    // (a queued hop would run it on the GUI thread and stall nothing).
    QObject::connect(&pump, &CapturePump::chunk, &pump,
                     [&samples, &stalled](const QByteArray &frames, int) {
                         QDataStream stream{frames};
                         stream.setByteOrder(QDataStream::LittleEndian);
                         int16_t sample{};
                         while (!stream.atEnd()) {
                             stream >> sample;
                             samples.append(sample);
                         }
                         // One 1 s stall once 2 s of audio has been
                         // collected: the ~1 s lead-in plus a second
                         // of tone — the stall is mid-tone.
                         if (!stalled
                             && samples.size()
                                    >= static_cast<qsizetype>(2) * 44100) {
                             stalled = true;
                             QThread::msleep(1000);
                         }
                     },
                     Qt::DirectConnection);
    QVERIFY(pump.start());

    // The writer (a 4 s tone into the null sink) on its own thread.
    std::atomic<bool> stopWriter{false};
    std::thread writer{[&] {
        writeTone(pulse.serverString(), PrivatePulse::sinkName(), stopWriter,
                  4);
    }};

    // The tone: ~1 s of lead-in, the 1 s stall, then the remaining
    // ~3 s at the pump's pace — the window covers the whole delayed
    // tone.
    int waited = 0;
    while (waited < 7000) {
        QTest::qWait(25);
        waited += 25;
    }
    stopWriter = true;
    writer.join();
    pump.stop();
    pulse.stop();

    // The tone's windows (100 ms of stereo): a drop in the pipeline
    // shows up as a near-silent window strictly between tone windows.
    const int window = 4410 * 2;
    auto rmsFrom = [](const QVector<int16_t> &samples, int from,
                      int window) -> double {
        quint64 squareSum = 0;
        for (int i = from; i < from + window; ++i) {
            const int a = qAbs(static_cast<int>(samples.at(i)));
            squareSum += static_cast<quint64>(a) * static_cast<quint64>(a);
        }
        return std::sqrt(static_cast<double>(squareSum) / window);
    };
    int firstTone = -1;
    int lastTone = -1;
    for (int from = 0; from + window <= samples.size(); from += window) {
        if (rmsFrom(samples, from, window) > 9500) {
            if (firstTone < 0)
                firstTone = from;
            lastTone = from;
        }
    }
    QVERIFY2(lastTone - firstTone >= window * 5,
             qPrintable("the tone was not captured (got "
                          + QString::number(samples.size()) + " samples)"));
    int silentWindows = 0;
    for (int from = firstTone + window; from < lastTone; from += window) {
        if (rmsFrom(samples, from, window) < 500)
            ++silentWindows;
    }
    QVERIFY2(silentWindows == 0,
             qPrintable(QStringLiteral(
                            "a stalled consumer cut %1 silent window(s) "
                            "into the tone")
                            .arg(silentWindows)));
}

void PrivatePulseTest::theClientKeepsALead()
{
    PrivatePulse pulse;
    pulse.setBaseDir(m_data.path());
    QVERIFY(pulse.start());

    // The client (a full pa_stream, the engine's shape — the server
    // authorizes its writes, it does not buffer them): a mainloop on
    // its own thread, a playback stream on the stub's sink. The
    // stream's buffer attrs are TIGHTER than the server's lead
    // (tlength 100 ms < kLeadMs), so the steady-state writable
    // headroom is the lead's, not the client's buffer's.
    pa_mainloop *ml = pa_mainloop_new();
    pa_mainloop_api *api = pa_mainloop_get_api(ml);
    std::thread mlThread{[ml] {
        int rv = 0;
        pa_mainloop_run(ml, &rv);
    }};
    pa_context *ctx = pa_context_new(api, "lead-test");
    pa_context_connect(ctx, pulse.serverString().toUtf8().constData(),
                       PA_CONTEXT_NOAUTOSPAWN, nullptr);
    int waited = 0;
    while (pa_context_get_state(ctx) != PA_CONTEXT_READY
           && waited < 5000) {
        QTest::qWait(25);
        waited += 25;
    }
    QVERIFY(pa_context_get_state(ctx) == PA_CONTEXT_READY);

    pa_sample_spec spec{};
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = 44100;
    spec.channels = 2;
    pa_channel_map map{};
    pa_channel_map_init_stereo(&map);
    const auto msBytes = [](int ms) {
        return static_cast<uint32_t>(44100) * 4 * static_cast<uint32_t>(ms)
            / 1000U;
    };
    pa_buffer_attr attr{};
    attr.maxlength = msBytes(500);
    attr.tlength = msBytes(100);
    attr.prebuf = msBytes(50);
    attr.minreq = msBytes(20);

    std::atomic<int> state{-1};
    pa_stream *st = pa_stream_new(ctx, "lead", &spec, &map);
    pa_stream_set_state_callback(st, leadStreamState, &state);
    pa_stream_connect_playback(st, PrivatePulse::sinkName(), &attr,
                               PA_STREAM_NOFLAGS, nullptr, nullptr);
    waited = 0;
    while (state.load() != static_cast<int>(PA_STREAM_READY)
           && waited < 5000) {
        QTest::qWait(25);
        waited += 25;
    }
    QVERIFY(state.load() == static_cast<int>(PA_STREAM_READY));

    // The first byte: starts the server's virtual reader (and the
    // pacing). The writer then writes nothing — the headroom is the
    // server's, not the client's.
    const std::array<int16_t, 2> oneFrame{0, 0};
    pa_stream_write(st, oneFrame.data(), oneFrame.size() * sizeof(int16_t),
                    nullptr, 0, PA_SEEK_RELATIVE);
    QThread::msleep(300);

    // Steady state: the tick paces the write requests to the virtual
    // reader PLUS the persistent lead — the client's writable
    // headroom stays at the lead, so a late tick (a descheduled
    // server's thread) starves the client's render thread only after
    // the headroom is gone (a ~1 ms starvation is a ~1 ms hole in the
    // audio, a pop).
    const size_t leadBytes =
        static_cast<size_t>(pulsestub::Server::kLeadMs) * 44100 * 4 / 1000;
    const size_t writable = pa_stream_writable_size(st);
    QVERIFY2(writable >= leadBytes,
             qPrintable(QStringLiteral(
                            "the client's writable headroom is %1, the "
                            "lead is %2")
                            .arg(writable)
                            .arg(leadBytes)));

    pa_stream_disconnect(st);
    pa_context_disconnect(ctx);
    waited = 0;
    while (pa_context_get_state(ctx) != PA_CONTEXT_TERMINATED
           && waited < 5000) {
        QTest::qWait(25);
        waited += 25;
    }
    pa_mainloop_quit(ml, 0);
    mlThread.join();
    pa_stream_unref(st);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    pulse.stop();
}

} // namespace

QTEST_MAIN(PrivatePulseTest)
#include "privatepulse_test.moc"
