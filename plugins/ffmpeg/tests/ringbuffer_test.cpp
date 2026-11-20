#include <QtTest>

#include <QThread>
#include <QVector>

#include <atomic>
#include <thread>

#include <doob/ringbuffer.h>

namespace {

class RingBufferTest : public QObject
{
    Q_OBJECT
private slots:
    void fillAndRead();
    void wraparound();
    void underrunReadsSilence();
    void drainDropsEverything();
    void writeBlocksWhileFull();
    void concurrentWriterReader();
    void stopIsPerWriter();
};

void RingBufferTest::fillAndRead()
{
    RingBuffer ring{100};
    QCOMPARE(ring.capacityFrames(), static_cast<size_t>(100));
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(0));

    QVector<float> in(120);
    for (qsizetype i = 0; i < 60; ++i) {
        in[i * 2] = static_cast<float>(i);
        in[(i * 2) + 1] = static_cast<float>(-i);
    }
    ring.write(in.data(), 60, 0);
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(60));

    QVector<float> out(120);
    QCOMPARE(ring.read(out.data(), 60), static_cast<size_t>(60));
    for (qsizetype i = 0; i < 60; ++i) {
        QCOMPARE(out[i * 2], static_cast<float>(i));
        QCOMPARE(out[(i * 2) + 1], static_cast<float>(-i));
    }
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(0));
}

void RingBufferTest::wraparound()
{
    RingBuffer ring{50};
    // Write 40 (head at 40), read 35 (count 5), write 45 again — that
    // write crosses the wrap point (10 before it, 35 after).
    QVector<float> buf(100);
    for (qsizetype i = 0; i < 40; ++i) {
        buf[i * 2] = static_cast<float>(i);
        buf[(i * 2) + 1] = static_cast<float>(i);
    }
    ring.write(buf.data(), 40, 0);
    ring.read(buf.data(), 35);  // count 5 (frames 35..39), tail at 35

    for (qsizetype i = 0; i < 45; ++i) {
        buf[i * 2] = static_cast<float>(40 + i);
        buf[(i * 2) + 1] = static_cast<float>(40 + i);
    }
    ring.write(buf.data(), 45, 0);  // wraps: 10 at 40..49, 35 at 0..34
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(50));

    QVector<float> out(100);
    QCOMPARE(ring.read(out.data(), 50), static_cast<size_t>(50));
    // The ring now holds frames 35..49 then 50..84 (one sequence).
    for (qsizetype i = 0; i < 50; ++i) {
        QCOMPARE(out[i * 2], static_cast<float>(35 + i));
        QCOMPARE(out[(i * 2) + 1], static_cast<float>(35 + i));
    }
}

void RingBufferTest::underrunReadsSilence()
{
    RingBuffer ring{100};
    QVector<float> in{1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                      11, 12, 13, 14, 15, 16, 17, 18, 19, 20,};
    ring.write(in.data(), 10, 0);

    QVector<float> out(40);
    QCOMPARE(ring.read(out.data(), 20), static_cast<size_t>(10));
    for (qsizetype i = 0; i < 10; ++i)
        QCOMPARE(out[i * 2], static_cast<float>((2 * i) + 1));
    for (qsizetype i = 10; i < 20; ++i) {
        QCOMPARE(out[i * 2], 0.0F);
        QCOMPARE(out[(i * 2) + 1], 0.0F);
    }
}

void RingBufferTest::drainDropsEverything()
{
    RingBuffer ring{100};
    QVector<float> in(20);
    ring.write(in.data(), 10, 0);
    ring.drain();
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(0));

    // drain clears the stop flags too (a write after a stop works
    // again).
    ring.requestStop(0);
    ring.write(in.data(), 5, 0);
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(0));
    ring.drain();
    ring.write(in.data(), 5, 0);
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(5));
}

void RingBufferTest::writeBlocksWhileFull()
{
    RingBuffer ring{100};
    QVector<float> in{1, 1};
    std::atomic<bool> done{false};
    std::thread writer([&] {
        // One frame at a time (like the decode worker's chunks): the
        // writer fills the ring and blocks on frame 101.
        for (int i = 0; i < 200; ++i)
            ring.write(in.data(), 1, 0);
        done = true;
    });

    // Give the writer a head start (it fills 100 and blocks).
    int waited = 0;
    while (ring.availableFrames() < 100 && waited < 2000) {
        QThread::msleep(10);
        waited += 10;
    }
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(100));
    QVERIFY(!done);

    // Room for the rest.
    QVector<float> out(200);
    ring.read(out.data(), 100);
    waited = 0;
    while (!done && waited < 2000) {
        QThread::msleep(10);
        waited += 10;
    }
    QVERIFY(done);
    writer.join();
}

void RingBufferTest::concurrentWriterReader()
{
    const qint64 totalFrames = 200;
    RingBuffer ring{50};
    std::atomic<qint64> written{0};
    std::thread writer([&] {
        QVector<float> in(2);
        for (qint64 i = 0; i < totalFrames; ++i) {
            in[0] = static_cast<float>(i);
            in[1] = static_cast<float>(-i);
            ring.write(in.data(), 1, 0);
            written = i + 1;
        }
    });

    qint64 readFrames = 0;
    QVector<float> out(2);
    while (readFrames < totalFrames) {
        const size_t got = ring.read(out.data(), 1);
        if (got > 0) {
            QCOMPARE(out[0], static_cast<float>(readFrames));
            QCOMPARE(out[1], static_cast<float>(-readFrames));
            readFrames++;
        } else {
            QThread::msleep(1);
        }
    }
    writer.join();
    QCOMPARE(written.load(), totalFrames);
}

// The gapless handoff's two decode workers share a ring, so the stop
// flag is per writer: stopping one (a killed queued worker) must not
// wake or silence the other (the current track's decode).
void RingBufferTest::stopIsPerWriter()
{
    RingBuffer ring{100};
    QVector<float> in(2);

    // Writer 0 fills the ring and blocks (back-pressure).
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 101; ++i)
            ring.write(in.data(), 1, 0);
        done = true;
    });
    int waited = 0;
    while (ring.availableFrames() < 100 && waited < 2000) {
        QThread::msleep(10);
        waited += 10;
    }
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(100));

    // Stopping the OTHER writer (1) must not wake writer 0: it rechecks
    // its own flag and goes back to sleep.
    ring.requestStop(1);
    QThread::msleep(50);
    QVERIFY(!done);

    // Stopping writer 0's own id wakes it, and its write no-ops.
    ring.requestStop(0);
    writer.join();
    QVERIFY(done);

    // drain clears ALL the stop flags (both writers write again).
    ring.drain();
    ring.write(in.data(), 1, 0);
    ring.write(in.data(), 1, 1);
    QCOMPARE(ring.availableFrames(), static_cast<size_t>(2));
}

} // namespace

QTEST_MAIN(RingBufferTest)
#include "ringbuffer_test.moc"
