#ifndef DOOB_TESTS_TESTSUPPORT_H
#define DOOB_TESTS_TESTSUPPORT_H

#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QSignalSpy>
#include <QtTest>

#include <functional>

#include "fakeplayer.h"
#include "librarymanager.h"
#include "mediaitem.h"
#include "mediaitemstore.h"
#include "playbackmanager.h"
#include "playlistmanager.h"

// The shared test helpers: the boilerplate the per-test init/cleanup
// blocks and the async waits used to repeat.

// Counts the QEvent::Paint events a widget receives (the paint-rate
// tests: the event loop delivers one paint per coalesced update()).
// No Q_OBJECT: eventFilter is a virtual, not a meta member.
class PaintCounter : public QObject
{
public:
    int count = 0;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Paint)
            ++count;
        return QObject::eventFilter(obj, event);
    }
};

namespace test {

// Points the database components at a file private to the test's
// directory (the write-through components would otherwise collide on
// the shared default path).
inline void usePrivateDatabase(const QString &dir)
{
    MediaItemStore::getInstance()->setStoragePath(dir + "/doob.db");
    PlaylistManager::getInstance()->setStoragePath(dir + "/doob.db");
}

// The manager's fake driver in place: set, stopped, and its counters
// reset (a test's init; cleanup() stops and resets the managers).
inline void installFakePlayer(PlaybackManager *pm, FakePlayer *fake)
{
    pm->setPlayer(fake);
    pm->stop();
    fake->resetCounters();
}

// Spins the event loop (queued signals only deliver while it runs)
// until pred() is true or the timeout expires; the result is pred()
// after the spin. A plain msleep would deadlock on queued signals.
template <typename Pred>
bool waitUntil(const Pred &pred, int timeoutMs = 10000, int stepMs = 25)
{
    int waited = 0;
    while (!pred() && waited < timeoutMs) {
        QTest::qWait(stepMs);
        waited += stepMs;
    }
    return pred();
}

// Spins the event loop until the library reports the operation finished.
inline void waitForIdle(LibraryManager *lib)
{
    waitUntil([lib] { return !lib->busy(); });
    QVERIFY(!lib->busy());
}

// The test import primitive: addFile + the itemAdded payload, once the
// manager is idle. A provider's synchronous callback emits itemAdded
// before QSignalSpy::wait() starts waiting (wait() only sees emissions
// that happen while it waits), so an already-arrived signal is taken
// as-is.
// The test import primitive, the variant with the addFiles provided
// metadata (see LibraryManager::addFiles).
inline MediaItem addLibraryFileAndWait(LibraryManager *lib, const QUrl &url,
                                       const QHash<QString, TrackMeta> &metadata)
{
    QSignalSpy addedSpy(lib, &LibraryManager::itemAdded);
    lib->addFiles({ url }, metadata);
    if (addedSpy.isEmpty() && !addedSpy.wait(10000)) {
        qWarning("addLibraryFileAndWait: itemAdded was not emitted");
        return {};
    }
    waitForIdle(lib);
    return addedSpy.at(0).at(0).value<MediaItem>();
}

inline MediaItem addLibraryFileAndWait(LibraryManager *lib, const QUrl &url)
{
    return addLibraryFileAndWait(lib, url, QHash<QString, TrackMeta>());
}

// A solid-color image as encoded bytes (the tests' artwork): size×size,
// so a scaled-to-size thumbnail keeps the fill pixel-for-pixel.
inline QByteArray solidImageBytes(const QColor &color, int size = 64,
                                  const char *format = "PNG")
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(color);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, format))
        return {};
    return buffer.data();
}

}  // namespace test

#endif // DOOB_TESTS_TESTSUPPORT_H
