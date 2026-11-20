#include <QtTest>

#include <QVector>

#include <array>
#include <cmath>

#include "audiotap.h"

namespace {

class AudioTapTest : public QObject
{
    Q_OBJECT

private slots:
    void constantBuffer();
    void negativeSamples();
    void emptyBuffer();
    void nullBuffer();
    void moreBucketsThanFrames();
    void sineWave();
    void tapDelayHoldsUntilAgedPastTheLatency();
    void tapDelayKeepsOrder();
    void tapDelayInsertsOutOfOrderStamps();
    void tapDelayPrunesStaleEntries();
    void tapDelayClears();
};

AudioTap makeTap(float peak)
{
    AudioTap tap;
    tap.leftPeak = peak;
    tap.rightPeak = peak;
    tap.waveform.append(peak);
    tap.waveform.append(peak);
    return tap;
}

// 100 frames of (0.5, 0.25): the peaks are exact, every column spans
// from 0.25 to 0.5, and the raw frames ride the snapshot untouched.
void AudioTapTest::constantBuffer()
{
    QVector<float> frames(200);
    for (qsizetype i = 0; i < 100; ++i) {
        frames[i * 2] = 0.5F;
        frames[(i * 2) + 1] = 0.25F;
    }
    const AudioTap tap = tapFrames(frames.data(), 100, 10);
    QCOMPARE(tap.leftPeak, 0.5F);
    QCOMPARE(tap.rightPeak, 0.25F);
    QCOMPARE(tap.waveform.size(), 20);
    for (qsizetype b = 0; b < 10; ++b) {
        QCOMPARE(tap.waveform[b * 2], 0.25F);
        QCOMPARE(tap.waveform[(b * 2) + 1], 0.5F);
    }
    QCOMPARE(tap.frames, frames);
}

// Negative samples: the peaks are magnitudes, the waveform keeps the
// signed envelope (below the center line).
void AudioTapTest::negativeSamples()
{
    QVector<float> frames(100);
    for (qsizetype i = 0; i < 50; ++i) {
        frames[i * 2] = -0.7F;
        frames[(i * 2) + 1] = 0.3F;
    }
    const AudioTap tap = tapFrames(frames.data(), 50, 5);
    QCOMPARE(tap.leftPeak, 0.7F);
    QCOMPARE(tap.rightPeak, 0.3F);
    for (qsizetype b = 0; b < 5; ++b) {
        QCOMPARE(tap.waveform[b * 2], -0.7F);
        QCOMPARE(tap.waveform[(b * 2) + 1], 0.3F);
    }
}

void AudioTapTest::emptyBuffer()
{
    const float sample = 0.5F;
    const AudioTap tap = tapFrames(&sample, 0, 10);
    QCOMPARE(tap.leftPeak, 0.0F);
    QCOMPARE(tap.rightPeak, 0.0F);
    QVERIFY(tap.waveform.isEmpty());
    QVERIFY(tap.frames.isEmpty());
}

void AudioTapTest::nullBuffer()
{
    const AudioTap tap = tapFrames(nullptr, 10, 10);
    QCOMPARE(tap.leftPeak, 0.0F);
    QCOMPARE(tap.rightPeak, 0.0F);
    QVERIFY(tap.waveform.isEmpty());
    QVERIFY(tap.frames.isEmpty());
}

// More buckets than frames: every frame folds into bucket 0 (no divide
// by zero), the remaining buckets stay flat.
void AudioTapTest::moreBucketsThanFrames()
{
    const std::array<float, 8> frames = {0.1F, 0.2F, 0.3F, 0.4F,
                                         0.5F, 0.6F, 0.7F, 0.8F,};
    const AudioTap tap = tapFrames(frames.data(), 4, 10);
    QCOMPARE(tap.leftPeak, 0.7F);
    QCOMPARE(tap.rightPeak, 0.8F);
    QCOMPARE(tap.waveform.size(), 20);
    QCOMPARE(tap.waveform[0], 0.1F);
    QCOMPARE(tap.waveform[1], 0.8F);
    for (qsizetype b = 1; b < 10; ++b) {
        QCOMPARE(tap.waveform[b * 2], 0.0F);
        QCOMPARE(tap.waveform[(b * 2) + 1], 0.0F);
    }
}

// A full-scale sine: the peaks reach 1.0 and the waveform envelope spans
// close to the full height.
void AudioTapTest::sineWave()
{
    static const float kPi = 3.14159265358979323846F;
    const qsizetype frameCount = 960;
    QVector<float> data(2 * frameCount);
    for (qsizetype i = 0; i < frameCount; ++i) {
        const float s =
            std::sin(2.0F * kPi * static_cast<float>(i) / 100.0F);
        data[i * 2] = s;
        data[(i * 2) + 1] = s;
    }
    const AudioTap tap = tapFrames(data.data(),
                                   static_cast<size_t>(frameCount), 96);
    QVERIFY(qAbs(tap.leftPeak - 1.0F) < 0.02F);
    QVERIFY(qAbs(tap.rightPeak - 1.0F) < 0.02F);
    float mn = 1.0F;
    float mx = -1.0F;
    for (const float v : tap.waveform) {
        mn = qMin(mn, v);
        mx = qMax(mx, v);
    }
    QVERIFY(mn < -0.9F);
    QVERIFY(mx > 0.9F);
}

// The tap's latency shift: a tap younger than the latency stays held,
// and once it has aged past it it comes out (and is popped).
void AudioTapTest::tapDelayHoldsUntilAgedPastTheLatency()
{
    TapDelay delay;
    delay.push(1000, makeTap(0.5F));
    QVERIFY(delay.due(1100, 200).isEmpty());  // age 100 < 200
    QCOMPARE(delay.size(), 1);
    const QVector<AudioTap> due = delay.due(1200, 200);  // age 200
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.first().leftPeak, 0.5F);
    QVERIFY(delay.due(5000, 0).isEmpty());  // popped
    QCOMPARE(delay.size(), 0);
}

// Several held taps come out oldest first (the display must not
// reorder the audio).
void AudioTapTest::tapDelayKeepsOrder()
{
    TapDelay delay;
    delay.push(1000, makeTap(0.2F));
    delay.push(1020, makeTap(0.4F));
    delay.push(1040, makeTap(0.6F));
    const QVector<AudioTap> due = delay.due(1300, 200);
    QCOMPARE(due.size(), 3);
    QCOMPARE(due.at(0).leftPeak, 0.2F);
    QCOMPARE(due.at(1).leftPeak, 0.4F);
    QCOMPARE(due.at(2).leftPeak, 0.6F);
    QCOMPARE(delay.size(), 0);
}

// A tap stamped for the future (a pass slice's audible moment) is
// inserted in time order, not appended: the display must not reorder
// the audio even when pushes arrive out of stamp order.
void AudioTapTest::tapDelayInsertsOutOfOrderStamps()
{
    TapDelay delay;
    delay.push(1000, makeTap(0.2F));
    delay.push(1040, makeTap(0.6F));
    delay.push(1020, makeTap(0.4F));  // lands between the two
    const QVector<AudioTap> due = delay.due(1300, 200);
    QCOMPARE(due.size(), 3);
    QCOMPARE(due.at(0).leftPeak, 0.2F);
    QCOMPARE(due.at(1).leftPeak, 0.4F);
    QCOMPARE(due.at(2).leftPeak, 0.6F);
    QCOMPARE(delay.size(), 0);
}

// A tap whose moment has passed is stale, not late (the sound it
// describes is over): prune drops it, the fresh one is kept.
void AudioTapTest::tapDelayPrunesStaleEntries()
{
    TapDelay delay;
    delay.push(1000, makeTap(0.2F));
    delay.push(4000, makeTap(0.4F));
    delay.prune(4100, 3000);
    QCOMPARE(delay.size(), 1);
    const QVector<AudioTap> due = delay.due(4100, 0);
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.first().leftPeak, 0.4F);
}

void AudioTapTest::tapDelayClears()
{
    TapDelay delay;
    delay.push(1000, makeTap(0.5F));
    delay.clear();
    QCOMPARE(delay.size(), 0);
    QVERIFY(delay.due(9000, 0).isEmpty());
}

}  // namespace

QTEST_MAIN(AudioTapTest)
#include "audiotap_test.moc"
