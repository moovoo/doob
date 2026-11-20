#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>

#include <cmath>

#include "decodeworker.h"
#include <doob/ringbuffer.h>

namespace {

// The fixed output rate the worker resamples to (the player's
// kOutputRate; 48 k here, the test tones' own rate).
constexpr int kTargetRate = 48000;

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
// (a sine tone) — a hermetic source for the decode worker.
QByteArray makeWav(int rate, int seconds, double hz)
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
            12000.0 * std::sin(2.0 * M_PI * hz * i / rate));
        appendU16(data, static_cast<quint16>(s));
        appendU16(data, static_cast<quint16>(s));
    }
    return data;
}

class DecodeWorkerTest : public QObject
{
    Q_OBJECT
private slots:
    void decodeToRing();
    void seekTruncates();
    void missingFileIsAnError();

private:
    QString makeToneFile(const QString &name);

    QTemporaryDir m_dir{QDir::tempPath() + "/doob_decodeworker_test"};
};

QString DecodeWorkerTest::makeToneFile(const QString &name)
{
    QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(makeWav(48000, 2, 440.0));
    f.close();
    return path;
}

void DecodeWorkerTest::decodeToRing()
{
    const QString path = makeToneFile("tone.wav");
    RingBuffer ring{static_cast<size_t>(48000) * 4};
    // Heap-owned: the test deletes it after stop() (which joins the
    // decode thread).
    DecodeWorker *worker = new DecodeWorker;
    QSignalSpy opened(worker, &DecodeWorker::opened);
    QSignalSpy eosSpy(worker, &DecodeWorker::eos);

    QVERIFY(worker->start(QUrl::fromLocalFile(path), &ring, kTargetRate, 0));
    // Poll the spy's count, not spy.wait(): QSignalSpy::wait() only sees
    // emissions that happen *during* the wait — a fast decode emits eos
    // before the wait starts, and the wait would time out on a signal
    // already received.
    int waited = 0;
    while (opened.count() == 0 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.first().at(0).toInt(), 48000);
    // The stream info: the decoded stream's codec, container and
    // actual parameters (a PCM s16le stereo wav at 48 kHz).
    const QVariantMap info = opened.first().at(1).toMap();
    QCOMPARE(info.value("codec").toString(), QStringLiteral("pcm_s16le"));
    QCOMPARE(info.value("format").toString(), QStringLiteral("wav"));
    QCOMPARE(info.value("sampleRate").toInt(), 48000);
    QCOMPARE(info.value("channels").toInt(), 2);
    QCOMPARE(info.value("channelLayout").toString(),
             QStringLiteral("stereo"));
    QCOMPARE(worker->streamInfo(), info);
    QCOMPARE(worker->durationMs(), static_cast<qint64>(2000));

    // The decode fills the ring...
    waited = 0;
    while (ring.availableFrames() < 1000 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY(ring.availableFrames() >= 1000);

    // ...with real (non-silent) audio.
    QVector<float> out(200);
    QCOMPARE(ring.read(out.data(), 100), static_cast<size_t>(100));
    float peak = 0.0F;
    for (int i = 0; i < 200; ++i)
        peak = std::max(peak, std::abs(out[i]));
    QVERIFY(peak > 0.1F);

    // ...and finishes with an eos.
    waited = 0;
    while (eosSpy.count() == 0 && waited < 10000) {
        QTest::qWait(10);
        waited += 10;
    }
    QCOMPARE(eosSpy.count(), 1);
    worker->stop();  // a no-op once the loop has ended
    delete worker;   // the thread is joined
}

void DecodeWorkerTest::seekTruncates()
{
    const QString path = makeToneFile("tone_seek.wav");
    RingBuffer ring{static_cast<size_t>(48000) * 4};
    DecodeWorker *worker = new DecodeWorker;
    QSignalSpy opened(worker, &DecodeWorker::opened);
    QSignalSpy eosSpy(worker, &DecodeWorker::eos);

    QVERIFY(worker->start(QUrl::fromLocalFile(path), &ring, kTargetRate, 0));
    // Pause immediately: PCM decodes in milliseconds, so without this
    // the whole file would be in the ring (and eos emitted) before a
    // seek request could be applied. The worker cannot have decoded
    // anything yet — thread startup plus openSource outlast this call.
    worker->setPaused(true);
    int waited = 0;
    while (opened.count() == 0 && waited < 5000) {
        QTest::qWait(10);
        waited += 10;
    }
    QCOMPARE(opened.count(), 1);
    QVERIFY(ring.availableFrames() == 0);

    // Seek (which drains the ring), then resume: the remainder is
    // exactly the audio after 1 s.
    worker->seekToMs(1000);
    worker->setPaused(false);
    // Wait for the decode to finish, then drain the ring (the whole
    // remainder fits: the ring is 4 s, the remainder is 1 s).
    waited = 0;
    while (eosSpy.count() == 0 && waited < 10000) {
        QTest::qWait(10);
        waited += 10;
    }
    QVERIFY(eosSpy.count() > 0);
    QVector<float> buf(2048);
    size_t total = 0;
    size_t got = 0;
    do {
        got = ring.read(buf.data(), 1024);
        total += got;
    } while (got > 0);
    // The remainder after the seek is ~1 s of audio (the seek is
    // BACKWARD, so at most a little more).
    QVERIFY(total >= static_cast<size_t>(48000)
              && total <= static_cast<size_t>(50000));
    worker->stop();
    delete worker;
}

void DecodeWorkerTest::missingFileIsAnError()
{
    RingBuffer ring{48000};
    DecodeWorker *worker = new DecodeWorker;
    QSignalSpy errorSpy(worker, &DecodeWorker::decodeError);
    QVERIFY(worker->start(QUrl::fromLocalFile(m_dir.filePath("absent.wav")),
                          &ring, kTargetRate, 0));
    QVERIFY(errorSpy.wait(5000));
    QVERIFY(!errorSpy.first().at(0).toString().isEmpty());
    worker->stop();
    delete worker;
}

} // namespace

QTEST_MAIN(DecodeWorkerTest)
#include "decodeworker_test.moc"
