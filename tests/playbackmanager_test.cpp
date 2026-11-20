#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "playerregistry.h"
#include "settingsmanager.h"
#include "sourcemanager.h"
#include "testproviders.h"
#include "testsupport.h"

namespace {

// A PlaybackManager whose default player (the one ensurePlayer() creates when
// no player is set explicitly) is an injectable fake, so the default-player
// auto-advance path can be tested without a real media backend.
class TestPlaybackManager : public PlaybackManager
{
public:
    using PlaybackManager::PlaybackManager;

    AudioPlayer* createDefaultPlayer() override { return m_default; }

    AudioPlayer* m_default = nullptr;
};

// A provider with a fixed resolution: every source is claimed, the
// metadata fetch is empty, and playback resolves to m_playable (or uses
// m_player when set, as a provider-owned driver).
class FixedProvider : public SourceProvider
{
    Q_OBJECT
public:
    explicit FixedProvider(QObject *parent = nullptr) : SourceProvider(parent) {}

    QString id() const override { return QStringLiteral("fake"); }
    QString title() const override { return QStringLiteral("Fake"); }
    bool accepts(const QUrl &url) const override { Q_UNUSED(url); return true; }
    void fetchMetadata(const QUrl &url, MetadataCallback cb) override
    {
        Q_UNUSED(url);
        cb(TrackMeta());
    }
    QUrl playableUrl(const MediaItem &item) const override
    {
        Q_UNUSED(item);
        return m_playable;
    }
    AudioPlayer* player() const override { return m_player; }

    QUrl m_playable;
    AudioPlayer* m_player = nullptr;
};

class PlaybackManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void playCurrentOnEmptyPlaylistStaysStopped();
    void playCurrentPlaysCurrentItem();
    void togglePlayPauseCyclesStates();
    void stopAfterPlayResets();
    void nextAdvancesToNextItem();
    void nextAtEndStops();
    void nextToDanglingItemStops();
    void previousRestartsWhenPastThreeSeconds();
    void previousGoesBackWhenUnderThreeSeconds();
    void previousAtFirstItemRestarts();
    void previousToDanglingItemStops();
    void finishedAutoAdvancesToNext();
    void finishedAtLastItemStops();
    void finishedAdvancingToDanglingItemStops();
    void autoAdvancePlaysNewTrackExactlyOnce();
    void gaplessArmQueuesTheNextTrack();
    void gaplessBoundaryFinishedDoesNotDoubleAdvance();
    void gaplessNotArmedOutsideTheLead();
    void gaplessUnknownDurationDoesNotArm();
    void gaplessRepeatTrackNeverArms();
    void gaplessShuffleArmsToAnOtherRow();
    void nonGaplessDriverKeepsThePlainFinished();
    void gaplessSeekRequeuesTheArmedItem();
    void gaplessLastTrackWithoutRepeatDoesNotArm();
    void gaplessLastTrackWithRepeatPlaylistArmsFirst();
    void gaplessArmClearsOnStopAndNewPlay();
    void gaplessArmResolvesProviderItems();
    void gaplessSlowResolutionPlaysArmedItemAtTheEnd();
    void gaplessArmSkipsProviderOwnedItems();
    void gaplessProviderArmsWithALongerLead();
    void gaplessRefusedQueuePlaysArmedItemAtTheEnd();
    void finishedAfterStopIsANoOp();
    void defaultPlayerAutoAdvancesToNext();
    void userChangingCurrentIndexFollowsPlayback();
    void userChangingCurrentIndexFromStoppedStartsPlayback();
    void clearingPlaylistStopsPlayback();
    void removingCurrentRowStopsPlayback();
    void removingOtherRowKeepsPlayback();
    void playFromPlaylistSwitchesAndPlays();
    void playFromPlaylistPlaysWithinCurrentPlaylist();
    void playFromPlaylistUnknownPlaylistNoop();
    void switchingCurrentPlaylistStopsPlayback();
    void positionAndDurationComeFromPlayer();
    void positionAndDurationZeroWithoutPlayer();
    void setPositionForwardsToPlayer();
    void setPositionNoopWhenStopped();
    void playerPositionAndDurationSignalsRelayed();
    void playerAudioTapSignalsRelayed();
    void playerStreamInfoRelayedAndCleared();
    void activePlayerSelectionDrivesEnsurePlayer();
    void setActivePlayerRejectsUnknownId();
    void unknownStoredDriverFallsBackToDefault();
    void preferredDefaultDriverUsedWhenUnselected();
    void playingRegistersThePlay();
    void providerPlayableUrlIsResolvedForPlayback();
    void providerOwnPlayerIsSwitchedIn();
    void providerPlayerSwitchesBackForOtherItems();
    void remoteTransportStateFollowsDriver();
    void providerAsyncResolutionDelaysStart();
    void providerResolutionFailureStops();
    void failedItemClearsOnNextPlayAttempt();
    void missingLocalFileFailsToPlay();
    void playerErrorMarksItemFailedAndStops();
    void newPlaySupersedesInFlightResolution();
    void stopSupersedesInFlightResolution();
    void volumeAppliesToPlayerAndPersists();
    void setVolumeClampsToRange();
    void volumeChangedEmitsOnlyOnChange();
    void muteSilencesPlayerKeepingVolume();
    void settingVolumeWhileMutedUnmutes();
    void volumeFollowsDriverSwitch();
    void driverSwitchEmitsSupportChange();
    void boostAppliesToPlayerAndPersists();
    void setVolumeBoostClampsToRange();
    void boostNotPushedToDriverWithoutHeadroom();
    void boostFollowsDriverSwitch();
    void sinkVolumeReportMovesStateWithoutRepush();
    void sinkVolumeEchoIsANoOp();
    void sinkMuteReportMovesStateWithoutRepush();
    void maxVolumeSettingMigratesToBoost();
    void settingsEditVolumePropagatesToState();
    void settingsEditMuteSilencesThePlayer();
    void settingsEditBoostPropagatesToState();
    void shuffleTogglesAndPersists();
    void setShuffleEmitsOnlyOnChange();
    void cycleRepeatCyclesAllModes();
    void shuffleNextPicksADifferentTrack();
    void shuffleSingleTrackStaysPut();
    void repeatOneReplaysOnFinish();
    void repeatPlaylistWrapsOnFinish();
    void repeatPlaylistNextWraps();
    void repeatOneManualNextStillMovesOn();
    void repeatSettingPersistsAndReloads();
    void settingsEditShuffleAndRepeatPropagate();

private:
    MediaItem makeItem(quint64 id, const QString &title);
    void fillPlaylist(int n);

    FakePlayer m_fake{this};
    FakePlayer m_fakeDefault{this};
    FakePlayer m_providerPlayer{this};
    FixedProvider m_provider{this};
    DeferredProvider m_deferred{this};
    PlaybackManager* m_pm = nullptr;
    PlaylistManager* m_pl = nullptr;
    MediaItemStore* m_store = nullptr;
    // Both managers are write-through: keep their database private to this
    // test (a shared default-path file would collide on the explicit ids).
    std::unique_ptr<QTemporaryDir> m_dir;
};

void PlaybackManagerTest::init()
{
    m_pl = PlaylistManager::getInstance();
    m_store = MediaItemStore::getInstance();
    m_dir = std::make_unique<QTemporaryDir>();
    test::usePrivateDatabase(m_dir->path());
    m_pl->reset();
    m_store->clear();
    // The SettingsManager's store, private to this test's directory.
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setSettingsPath(m_dir->path() + "/settings.json");
    settings->load();

    m_pm = PlaybackManager::getInstance();
    test::installFakePlayer(m_pm, &m_fake);
    SourceManager::getInstance()->clear();
    m_provider.m_playable = QUrl();
    m_provider.m_player = nullptr;
}

void PlaybackManagerTest::cleanup()
{
    m_pm->stop();
    m_pm->setPlayer(&m_fake);
    m_pm->setMuted(false);
    m_pm->setVolume(100);
    m_pm->setVolumeBoost(100);
    m_pm->setShuffle(false);
    m_pm->setRepeat(PlaybackManager::RepeatMode::NoRepeat);
    m_pl->reset();
    m_store->clear();
    SourceManager::getInstance()->clear();
    SettingsManager::reset();
}

// Builds the item and registers it in the standalone item store, so
// currentItem()/itemAt() can resolve it (the playback manager reads items
// through the playlist, not by copy).
MediaItem PlaybackManagerTest::makeItem(quint64 id, const QString &title)
{
    MediaItem item;
    item.id = id;
    item.title = title;
    // A real (dummy) file: playback is checked against the disk.
    const QString path = "/tmp/" + title + ".mp3";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("\x00\x01\x02");
    }
    item.source = QUrl::fromLocalFile(path);
    m_store->remove(id);
    m_store->insert(item);
    return item;
}

void PlaybackManagerTest::fillPlaylist(int n)
{
    QList<quint64> ids;
    for (int i = 1; i <= n; ++i)
        ids.append(makeItem(i, QString("Track%1").arg(i)).id);
    m_pl->addItems(ids);
}

void PlaybackManagerTest::playCurrentOnEmptyPlaylistStaysStopped()
{
    m_pm->playCurrent();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playCount, 0);
}

void PlaybackManagerTest::playCurrentPlaysCurrentItem()
{
    QSignalSpy stateSpy(m_pm, &PlaybackManager::stateChanged);
    QSignalSpy itemSpy(m_pm, &PlaybackManager::playingItemChanged);

    fillPlaylist(3);
    QCOMPARE(m_pl->currentIndex(), 0);

    m_pm->playCurrent();

    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 1ULL);
    QCOMPARE(m_fake.m_playing, true);
    QCOMPARE(m_pm->playingItem().id, 1ULL);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(itemSpy.count(), 1);
}

void PlaybackManagerTest::togglePlayPauseCyclesStates()
{
    fillPlaylist(2);

    m_pm->togglePlayPause();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    m_pm->togglePlayPause();
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);
    QCOMPARE(m_fake.m_paused, true);

    m_pm->togglePlayPause();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_paused, false);
    QCOMPARE(m_fake.m_resumeCount, 1);
}

void PlaybackManagerTest::stopAfterPlayResets()
{
    QSignalSpy itemSpy(m_pm, &PlaybackManager::playingItemChanged);

    fillPlaylist(2);
    m_pm->playCurrent();
    m_pm->stop();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playing, false);
    QVERIFY(!m_pm->playingItem().isValid());
    QCOMPARE(itemSpy.count(), 2);
}

void PlaybackManagerTest::nextAdvancesToNextItem()
{
    fillPlaylist(3);
    m_pm->playCurrent();

    m_pm->next();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_item.id, 2ULL);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::nextAtEndStops()
{
    fillPlaylist(3);
    m_pm->playCurrent();
    m_pm->next();
    m_pm->next();
    QCOMPARE(m_pl->currentIndex(), 2);
    QCOMPARE(m_fake.m_playCount, 3);

    m_pm->next();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playCount, 3);
}

void PlaybackManagerTest::nextToDanglingItemStops()
{
    // A row whose id resolves to nothing (the store does not hold it) must
    // not be "played": next() stops instead of entering a Playing state
    // with an invalid item.
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id, 999999 }); // 999999 is not in the store
    m_pm->playCurrent();
    QCOMPARE(m_fake.m_item.id, 1ULL);

    m_pm->next();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::previousRestartsWhenPastThreeSeconds()
{
    fillPlaylist(2);
    m_pl->setCurrentIndex(1);
    QCOMPARE(m_fake.m_playCount, 1);
    m_fake.m_position = 4000;

    m_pm->previous();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_item.id, 2ULL);
    QCOMPARE(m_fake.m_playCount, 2);
}

void PlaybackManagerTest::previousGoesBackWhenUnderThreeSeconds()
{
    fillPlaylist(2);
    m_pl->setCurrentIndex(1);
    m_fake.m_position = 2000;

    m_pm->previous();

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_item.id, 1ULL);
    QCOMPARE(m_fake.m_playCount, 2);
}

void PlaybackManagerTest::previousAtFirstItemRestarts()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    m_fake.m_position = 4000;

    m_pm->previous();

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_playCount, 2);
}

void PlaybackManagerTest::previousToDanglingItemStops()
{
    // Going back to a row that resolves to nothing stops, it does not play
    // an invalid item.
    const MediaItem b = makeItem(2, "B");
    m_pl->addItems({ 999999, b.id }); // row 0 is dangling
    m_pl->setCurrentIndex(1);
    QCOMPARE(m_fake.m_item.id, 2ULL);
    m_fake.m_position = 1000; // early in the track: previous() goes back a row

    m_pm->previous();

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::finishedAutoAdvancesToNext()
{
    fillPlaylist(2);
    m_pm->playCurrent();

    m_fake.triggerFinished();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_item.id, 2ULL);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::finishedAtLastItemStops()
{
    fillPlaylist(2);
    m_pl->setCurrentIndex(1);
    QCOMPARE(m_fake.m_playCount, 1);

    m_fake.triggerFinished();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playCount, 1);
}

void PlaybackManagerTest::finishedAdvancingToDanglingItemStops()
{
    // Auto-advance into a row that resolves to nothing stops, it does not
    // play an invalid item.
    const MediaItem a = makeItem(1, "A");
    m_pl->addItems({ a.id, 999999 }); // row 1 is dangling
    m_pm->playCurrent();
    QCOMPARE(m_fake.m_item.id, 1ULL);

    m_fake.triggerFinished();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::autoAdvancePlaysNewTrackExactlyOnce()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_fake.m_playCount, 1);

    m_fake.triggerFinished();

    QCOMPARE(m_fake.m_playCount, 2);
}

void PlaybackManagerTest::gaplessArmQueuesTheNextTrack()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();
    QCOMPARE(m_pl->currentIndex(), 0);

    QSignalSpy itemSpy(m_pm, &PlaybackManager::playingItemChanged);
    m_fake.triggerPosition(7000);  // within the 3 s lead

    // The display advances early and the next item is queued on the
    // driver — no re-play (the driver plays through the boundary).
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, 2ULL);
    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_fake.m_queuedItem.id, 2ULL);
    QCOMPARE(m_fake.m_playCount, 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(itemSpy.count(), 1);

    // The arm is latched: the position keeps coming (the ring's tail
    // still plays out) and must not arm a second time.
    m_fake.triggerPosition(7500);
    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_pl->currentIndex(), 1);
}

void PlaybackManagerTest::gaplessBoundaryFinishedDoesNotDoubleAdvance()
{
    fillPlaylist(3);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_playCount, 1);

    // The boundary: the driver's finished at the moment the queued
    // item's frames start playing. The advance already happened at
    // the arm — this must be a no-op (a second advance would skip
    // track 3).
    m_fake.triggerFinished();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, 2ULL);
    QCOMPARE(m_fake.m_playCount, 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::gaplessNotArmedOutsideTheLead()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(5000);  // 5 s left: outside the 3 s lead

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 0);
}

void PlaybackManagerTest::gaplessUnknownDurationDoesNotArm()
{
    // A live stream (no duration) cannot be armed early.
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;  // m_duration stays 0
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 0);
}

void PlaybackManagerTest::gaplessRepeatTrackNeverArms()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();
    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatTrack);

    m_fake.triggerPosition(7000);

    // Its replay is the current item itself: no early arm, and the
    // plain finished replays.
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    m_fake.triggerFinished();
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_playCount, 2);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::gaplessShuffleArmsToAnOtherRow()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();
    m_pm->setShuffle(true);

    m_fake.triggerPosition(7000);

    // The shuffled next row (with two tracks, the other one) is armed.
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_fake.m_queuedItem.id, 2ULL);
}

void PlaybackManagerTest::nonGaplessDriverKeepsThePlainFinished()
{
    fillPlaylist(2);
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);

    // No capability: no early arm, and the plain finished advances.
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    m_fake.triggerFinished();
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_playCount, 2);
}

void PlaybackManagerTest::gaplessSeekRequeuesTheArmedItem()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);
    QCOMPARE(m_fake.m_queueNextCount, 1);

    // A seek kills the driver's queued decode (its ring drain takes
    // the buffered frames): the manager re-queues the armed item.
    m_pm->setPositionMs(1000);

    QCOMPARE(m_fake.m_queueNextCount, 2);
    QCOMPARE(m_fake.m_queuedItem.id, 2ULL);
    QCOMPARE(m_pl->currentIndex(), 1);
}

void PlaybackManagerTest::gaplessLastTrackWithoutRepeatDoesNotArm()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pl->setCurrentIndex(1);

    m_fake.triggerPosition(7000);

    // Nothing left to play: no arm, and the plain finished stops.
    QCOMPARE(m_fake.m_queueNextCount, 0);
    m_fake.triggerFinished();
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
}

void PlaybackManagerTest::gaplessLastTrackWithRepeatPlaylistArmsFirst()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatPlaylist);
    m_pl->setCurrentIndex(1);

    m_fake.triggerPosition(7000);

    // The wrap target is the first row.
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_fake.m_queuedItem.id, 1ULL);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::gaplessArmClearsOnStopAndNewPlay()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);
    QCOMPARE(m_fake.m_queueNextCount, 1);

    m_pm->stop();
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);

    // A new play of the same item arms again (the latch is per play).
    m_pl->setCurrentIndex(0);
    m_fake.triggerPosition(7000);
    QCOMPARE(m_fake.m_queueNextCount, 2);
    QCOMPARE(m_pl->currentIndex(), 1);
}

// A provider item (its stored source is the provider's — a watch
// page) is resolved before it reaches the driver's queue: the arm
// advances the display, the resolution goes in flight, and queueNext
// gets the resolved stream — never the stored source.
void PlaybackManagerTest::gaplessArmResolvesProviderItems()
{
    fillPlaylist(2);
    const quint64 id = m_pl->itemIds().at(1);
    MediaItem item = m_store->item(id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);

    // The display advanced, the resolution is in flight — and the
    // driver was not handed the stored source in the meantime.
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, id);
    QCOMPARE(m_deferred.m_pending.count(), 1);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));

    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_fake.m_queuedItem.id, id);
    QCOMPARE(m_fake.m_queuedItem.source,
             QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_fake.m_playCount, 1);  // no re-play (the handoff)

    // The boundary's finished is a no-op (the queue made it).
    m_fake.triggerFinished();
    QCOMPARE(m_fake.m_playCount, 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

// The resolution is still in flight when the boundary arrives: the
// driver ended the old item plainly, and the manager plays the armed
// item itself (a gap, never a stuck state).
void PlaybackManagerTest::gaplessSlowResolutionPlaysArmedItemAtTheEnd()
{
    fillPlaylist(2);
    const quint64 id = m_pl->itemIds().at(1);
    MediaItem item = m_store->item(id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_deferred.m_pending.count(), 1);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    // The boundary, before the resolution landed.
    m_fake.triggerFinished();

    // The armed item is being played through the provider-aware path
    // (its own resolution, the display already on it) — no double
    // advance, no stuck state.
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, id);
    QCOMPARE(m_fake.m_playCount, 1);  // the fallback is in flight
    QCOMPARE(m_deferred.m_pending.count(), 2);

    // The stale arm resolution lands first: a no-op (the latch is
    // gone) — the play's resolution is still in flight.
    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_fake.m_playCount, 1);
    QCOMPARE(m_deferred.m_pending.count(), 1);

    // The play's resolution: the armed item plays with the stream.
    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_fake.m_playCount, 2);
    QCOMPARE(m_fake.m_item.source, QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_deferred.m_pending.count(), 0);  // the fixture is clean
}

// A next item whose provider owns a player needs a driver switch at
// the boundary — a handoff through the current driver is impossible:
// no arm, and the plain finished path does the switch.
void PlaybackManagerTest::gaplessArmSkipsProviderOwnedItems()
{
    fillPlaylist(2);
    const quint64 id = m_pl->itemIds().at(1);
    MediaItem item = m_store->item(id);
    item.provider = m_provider.id();
    QVERIFY(m_store->updateItem(item));
    m_provider.m_player = &m_providerPlayer;
    m_providerPlayer.resetCounters();
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    m_fake.triggerPosition(7000);

    // No arm: the index did not move, nothing was queued.
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    // The plain finished path: the provider's driver switches in.
    m_fake.triggerFinished();
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->player(), &m_providerPlayer);
    QCOMPARE(m_providerPlayer.m_playCount, 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

// A provider item's stream has to connect and buffer before the
// boundary, so it arms with the longer lead: a position between the
// two leads (9 s to the end) does not arm a local next, but arms a
// provider one.
void PlaybackManagerTest::gaplessProviderArmsWithALongerLead()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.triggerDuration(15000);

    // A local next: 9000 ms to the end is outside the 3 s lead — no arm.
    m_pm->playCurrent();
    m_fake.triggerPosition(6000);
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    m_pm->stop();

    // A provider next: the same position is inside the 10 s lead — the
    // display advances and the resolution goes in flight.
    const quint64 id = m_pl->itemIds().at(1);
    MediaItem item = m_store->item(id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_pl->setCurrentIndex(0);
    m_fake.triggerDuration(15000);
    m_pm->playCurrent();
    m_fake.triggerPosition(6000);  // 9000 ms to the end
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, id);
    QCOMPARE(m_deferred.m_pending.count(), 1);
    QCOMPARE(m_fake.m_queueNextCount, 0);

    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_deferred.m_pending.count(), 0);  // the fixture is clean
}

// The driver's queue is refused (queueNext answers false — a missing
// file, a bad scheme): the arm's latch records the refusal, and the
// boundary's finished is NOT the gapless no-op — the driver ended the
// old item plainly and the manager plays the armed item itself (a
// gap, never a stuck state).
void PlaybackManagerTest::gaplessRefusedQueuePlaysArmedItemAtTheEnd()
{
    fillPlaylist(2);
    m_fake.m_supportsGapless = true;
    m_fake.m_queueNextRefused = true;
    m_fake.triggerDuration(10000);
    m_pm->playCurrent();

    // The arm advances the display; the queue is refused (the latch
    // records the driver's false).
    m_fake.triggerPosition(7000);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, 2ULL);
    QCOMPARE(m_fake.m_queueNextCount, 1);
    QCOMPARE(m_fake.m_playCount, 1);

    // The boundary: the plain path plays the armed item — no stuck
    // state, no silent end.
    m_fake.triggerFinished();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->playingItem().id, 2ULL);
    QCOMPARE(m_fake.m_item.id, 2ULL);
    QCOMPARE(m_fake.m_playCount, 2);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

// A finished that lands after a stop is a no-op (a stale signal —
// the driver's boundary resolving across the stop): the playback is
// dead and the plain advance must not start a track from nothing.
void PlaybackManagerTest::finishedAfterStopIsANoOp()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_fake.m_playCount, 1);

    m_pm->stop();
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);

    m_fake.triggerFinished();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playCount, 1);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::defaultPlayerAutoAdvancesToNext()
{
    TestPlaybackManager pm;
    pm.m_default = &m_fakeDefault;
    m_fakeDefault.resetCounters();

    fillPlaylist(2);
    pm.playCurrent();

    // The lazily-created default player is the injected fake and is already
    // wired up (regression: it used to be created without the finished
    // connection, so the real app never auto-advanced).
    QCOMPARE(pm.player(), &m_fakeDefault);
    QCOMPARE(pm.state(), PlaybackManager::Playing);

    m_fakeDefault.triggerFinished();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(pm.state(), PlaybackManager::Playing);
    QCOMPARE(m_fakeDefault.m_item.id, 2ULL);
}

void PlaybackManagerTest::userChangingCurrentIndexFollowsPlayback()
{
    fillPlaylist(3);
    m_pm->playCurrent();
    QCOMPARE(m_fake.m_item.id, 1ULL);

    m_pl->setCurrentIndex(2);

    QCOMPARE(m_fake.m_item.id, 3ULL);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::userChangingCurrentIndexFromStoppedStartsPlayback()
{
    fillPlaylist(3);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);

    m_pl->setCurrentIndex(1);

    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 2ULL);
}

void PlaybackManagerTest::clearingPlaylistStopsPlayback()
{
    fillPlaylist(2);
    m_pm->playCurrent();

    m_pl->clear();

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playing, false);
}

void PlaybackManagerTest::removingCurrentRowStopsPlayback()
{
    // Removing the row that is currently playing stops playback: the
    // playing track is gone, and the row that slides into its place is not
    // silently skipped on auto-advance.
    fillPlaylist(3);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 1ULL);

    m_pl->removeAt(0);

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::removingOtherRowKeepsPlayback()
{
    // Removing a row that is not the playing one leaves playback running.
    fillPlaylist(3);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 1ULL);

    m_pl->removeAt(1);

    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_pm->playingItem().id, 1ULL);
}

void PlaybackManagerTest::playFromPlaylistSwitchesAndPlays()
{
    fillPlaylist(3);
    const quint64 defId = m_pl->currentPlaylistId();
    const quint64 other = m_pl->addPlaylist("Other");
    m_pl->setCurrentPlaylist(other);
    m_pl->addItems({ makeItem(10, "X").id, makeItem(11, "Y").id });
    m_pl->setCurrentPlaylist(defId);

    m_pm->playFromPlaylist(other, 1);

    QCOMPARE(m_pl->currentPlaylistId(), other);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 11ULL);
}

void PlaybackManagerTest::playFromPlaylistPlaysWithinCurrentPlaylist()
{
    fillPlaylist(2);
    const quint64 defId = m_pl->currentPlaylistId();

    m_pm->playFromPlaylist(defId, 1);

    QCOMPARE(m_pl->currentPlaylistId(), defId);
    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 2ULL);
}

void PlaybackManagerTest::playFromPlaylistUnknownPlaylistNoop()
{
    fillPlaylist(2);

    m_pm->playFromPlaylist(999, 0);

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QCOMPARE(m_fake.m_playCount, 0);
}

void PlaybackManagerTest::switchingCurrentPlaylistStopsPlayback()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    const quint64 other = m_pl->addPlaylist("Other");
    m_pl->setCurrentPlaylist(other);

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::positionAndDurationComeFromPlayer()
{
    fillPlaylist(2);
    m_pm->playCurrent();

    m_fake.m_position = 65000;
    m_fake.m_duration = 215000;

    QCOMPARE(m_pm->positionMs(), 65000LL);
    QCOMPARE(m_pm->durationMs(), 215000LL);
}

void PlaybackManagerTest::positionAndDurationZeroWithoutPlayer()
{
    m_pm->stop();
    m_pm->setPlayer(nullptr);

    QCOMPARE(m_pm->positionMs(), 0LL);
    QCOMPARE(m_pm->durationMs(), 0LL);
}

void PlaybackManagerTest::setPositionForwardsToPlayer()
{
    fillPlaylist(2);
    m_pm->playCurrent();

    m_pm->setPositionMs(90000);

    QCOMPARE(m_fake.m_position, 90000);
    QCOMPARE(m_fake.m_seekCount, 1);
}

void PlaybackManagerTest::setPositionNoopWhenStopped()
{
    m_pm->setPositionMs(90000);

    QCOMPARE(m_fake.m_position, 0);
    QCOMPARE(m_fake.m_seekCount, 0);
}

void PlaybackManagerTest::playerPositionAndDurationSignalsRelayed()
{
    QSignalSpy positionSpy(m_pm, &PlaybackManager::positionChanged);
    QSignalSpy durationSpy(m_pm, &PlaybackManager::durationChanged);

    fillPlaylist(2);
    m_pm->playCurrent();

    m_fake.triggerPosition(12345);
    m_fake.triggerDuration(215000);

    QCOMPARE(positionSpy.count(), 1);
    QCOMPARE(positionSpy.first().first().toLongLong(), 12345LL);
    QCOMPARE(durationSpy.count(), 1);
    QCOMPARE(durationSpy.first().first().toLongLong(), 215000LL);
}

// The audio tap of the active player is re-emitted by the manager
// one-for-one, so a widget observes the manager (not the driver); the
// waveform carries the stream's sample rate with it (the display's
// scroll runs on the audio clock, see OscillogramWidget).
void PlaybackManagerTest::playerAudioTapSignalsRelayed()
{
    QSignalSpy levelsSpy(m_pm, &PlaybackManager::audioLevels);
    QSignalSpy waveformSpy(m_pm, &PlaybackManager::audioWaveform);
    QSignalSpy framesSpy(m_pm, &PlaybackManager::audioFrames);

    fillPlaylist(1);
    m_pm->playCurrent();

    m_fake.triggerLevels(0.25F, 0.5F);
    QCOMPARE(levelsSpy.count(), 1);
    QCOMPARE(levelsSpy.first().at(0).toFloat(), 0.25F);
    QCOMPARE(levelsSpy.first().at(1).toFloat(), 0.5F);

    m_fake.triggerWaveform(QVector<float>{0.0F, 0.5F, -0.25F, 0.5F}, 48000);
    QCOMPARE(waveformSpy.count(), 1);
    const QVector<float> expected = {0.0F, 0.5F, -0.25F, 0.5F};
    QCOMPARE(waveformSpy.first().at(0).value<QVector<float>>(), expected);
    QCOMPARE(waveformSpy.first().at(1).toUInt(), 48000U);

    m_fake.triggerFrames(QVector<float>{0.0F, 0.25F, 0.5F, 0.5F}, 48000);
    QCOMPARE(framesSpy.count(), 1);
    const QVector<float> frames = {0.0F, 0.25F, 0.5F, 0.5F};
    QCOMPARE(framesSpy.first().at(0).value<QVector<float>>(), frames);
    QCOMPARE(framesSpy.first().at(1).toUInt(), 48000U);

    m_fake.triggerLevels(0.3F, 0.2F);
    QCOMPARE(levelsSpy.count(), 2);
    QCOMPARE(levelsSpy.last().at(0).toFloat(), 0.3F);
    QCOMPARE(levelsSpy.last().at(1).toFloat(), 0.2F);
}

// The stream info of the active player (the codec, container and the
// decoded stream's actual parameters) is re-emitted by the manager, so
// a widget observes the manager, not the driver. A new play and a stop
// clear it (an empty map = no info).
void PlaybackManagerTest::playerStreamInfoRelayedAndCleared()
{
    QSignalSpy infoSpy(m_pm, &PlaybackManager::streamInfoChanged);

    fillPlaylist(2);
    m_pm->playCurrent();

    // A new play starts with no info (the previous track's report is
    // stale from that moment).
    QCOMPARE(infoSpy.count(), 1);
    QVERIFY(infoSpy.first().first().toMap().isEmpty());

    const QVariantMap info{
        { QStringLiteral("codec"), QStringLiteral("mp3") },
        { QStringLiteral("format"), QStringLiteral("mp3") },
        { QStringLiteral("bitrate"), 320000 },
        { QStringLiteral("sampleRate"), 44100 },
        { QStringLiteral("channels"), 2 },
        { QStringLiteral("channelLayout"), QStringLiteral("stereo") },
    };
    m_fake.triggerStreamInfo(info);
    QCOMPARE(infoSpy.count(), 2);
    QCOMPARE(infoSpy.last().first().toMap(), info);

    m_pm->stop();
    QCOMPARE(infoSpy.count(), 3);
    QVERIFY(infoSpy.last().first().toMap().isEmpty());
}

void PlaybackManagerTest::activePlayerSelectionDrivesEnsurePlayer()
{
    static int created = 0;
    created = 0;
    PlayerRegistry::getInstance()->registerPlayer("reg", "Registered",
        [](QObject *parent) {
            ++created;
            return new FakePlayer(parent);
        });
    m_pm->loadSettings();
    QCOMPARE(m_pm->activePlayerId(), QString());

    QSignalSpy changedSpy(m_pm, &PlaybackManager::activePlayerChanged);
    QVERIFY(m_pm->setActivePlayer("reg"));
    QCOMPARE(m_pm->activePlayerId(), QString("reg"));
    QCOMPARE(changedSpy.count(), 1);

    // The currently set player keeps playing; the selection applies at
    // the next ensurePlayer (no player set).
    fillPlaylist(1);
    m_pm->stop();
    m_pm->setPlayer(nullptr);
    m_pm->playCurrent();

    QCOMPARE(created, 1);
    QVERIFY(m_pm->player() != nullptr);
    QVERIFY(m_pm->player() != &m_fake);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    // The selection is persisted and reloaded.
    m_pm->loadSettings();
    QCOMPARE(m_pm->activePlayerId(), QString("reg"));

    // Leave the singleton in its default state for the other tests.
    m_pm->setActivePlayer("");
}

void PlaybackManagerTest::setActivePlayerRejectsUnknownId()
{
    QVERIFY(m_pm->setActivePlayer(""));
    QVERIFY(!m_pm->setActivePlayer("nope"));
    QCOMPARE(m_pm->activePlayerId(), QString());
}

void PlaybackManagerTest::unknownStoredDriverFallsBackToDefault()
{
    // A file that names a driver the registry no longer holds (its
    // plugin was removed): ensurePlayer falls back to the built-in default.
    QFile f(m_dir->path() + "/settings.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"playback.player": "gone"})");
    f.close();
    SettingsManager::getInstance()->load();

    TestPlaybackManager pm;
    pm.m_default = &m_fakeDefault;
    m_fakeDefault.resetCounters();
    pm.loadSettings();
    QCOMPARE(pm.activePlayerId(), QString("gone"));

    fillPlaylist(1);
    pm.playCurrent();

    QCOMPARE(pm.player(), &m_fakeDefault);
    QCOMPARE(pm.state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::preferredDefaultDriverUsedWhenUnselected()
{
    static int created = 0;
    created = 0;
    PlayerRegistry* registry = PlayerRegistry::getInstance();
    registry->registerPlayer("pref", "Preferred",
        [](QObject *parent) {
            ++created;
            return new FakePlayer(parent);
        });
    QVERIFY(registry->setPreferredDefault("pref"));

    TestPlaybackManager pm;
    pm.m_default = &m_fakeDefault;
    m_fakeDefault.resetCounters();
    pm.loadSettings();
    QCOMPARE(pm.activePlayerId(), QString());

    fillPlaylist(1);
    pm.playCurrent();

    // No explicit selection: the preferred default wins over the
    // built-in default.
    QCOMPARE(created, 1);
    QVERIFY(pm.player() != &m_fakeDefault);
    QCOMPARE(pm.state(), PlaybackManager::Playing);

    // An explicit selection wins over the preferred default.
    registry->registerPlayer("exp", "Explicit",
        [](QObject *parent) {
            ++created;
            return new FakePlayer(parent);
        });
    QVERIFY(pm.setActivePlayer("exp"));
    pm.setPlayer(nullptr);
    pm.playCurrent();
    QCOMPARE(created, 2);
    QVERIFY(pm.player() != &m_fakeDefault);

    // Clearing the preference (the plugin's shutdown does this)
    // restores the built-in default.
    pm.setActivePlayer("");
    registry->setPreferredDefault(QString());
    pm.setPlayer(nullptr);
    pm.playCurrent();
    QCOMPARE(created, 2);  // no new player from the registry
    QCOMPARE(pm.player(), &m_fakeDefault);

    // A stale preference (the driver unregistered) also falls back to
    // the built-in default.
    registry->setPreferredDefault("pref");
    registry->unregisterPlayer("pref");
    pm.setPlayer(nullptr);
    pm.playCurrent();
    QCOMPARE(pm.player(), &m_fakeDefault);

    // Leave the registry in its default state for the other tests.
    registry->setPreferredDefault(QString());
    registry->unregisterPlayer("exp");
    QCOMPARE(registry->preferredDefault(), QString());
}

void PlaybackManagerTest::playingRegistersThePlay()
{
    fillPlaylist(2);
    const QList<quint64> ids = m_pl->itemIds();

    m_pm->playCurrent();
    QCOMPARE(m_store->item(ids.at(0)).playCount, quint64(1));
    QVERIFY(!m_store->item(ids.at(0)).lastPlayedAt.isNull());

    m_pm->next();
    QCOMPARE(m_store->item(ids.at(1)).playCount, quint64(1));
    QCOMPARE(m_store->item(ids.at(0)).playCount, quint64(1));  // untouched

    m_pm->playCurrent();  // a repeat of the current track counts again
    QCOMPARE(m_store->item(ids.at(1)).playCount, quint64(2));
}

void PlaybackManagerTest::providerPlayableUrlIsResolvedForPlayback()
{
    fillPlaylist(1);
    const quint64 id = m_pl->itemIds().at(0);
    MediaItem item = m_store->item(id);
    item.provider = m_provider.id();
    QVERIFY(m_store->updateItem(item));

    m_provider.m_playable = QUrl("https://stream.example.com/s.m3u8");
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    m_pm->playCurrent();

    // The player got the resolved stream, the display item the original.
    QCOMPARE(m_fake.m_item.source, QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_pm->playingItem().source, item.source);
    QCOMPARE(m_pm->playingItem().provider, m_provider.id());
}

void PlaybackManagerTest::providerOwnPlayerIsSwitchedIn()
{
    fillPlaylist(1);
    const quint64 id = m_pl->itemIds().at(0);
    MediaItem item = m_store->item(id);
    item.provider = m_provider.id();
    QVERIFY(m_store->updateItem(item));

    m_provider.m_player = &m_providerPlayer;
    m_providerPlayer.resetCounters();
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    m_pm->playCurrent();

    // The provider's own driver was switched in and played the item.
    QCOMPARE(m_pm->player(), &m_providerPlayer);
    QCOMPARE(m_providerPlayer.m_playCount, 1);
    QCOMPARE(m_pm->playingItem().id, id);
}

void PlaybackManagerTest::providerPlayerSwitchesBackForOtherItems()
{
    // A provider-owned driver plays only its provider's items: an item
    // of another origin (a local file) restores the configured driver,
    // and the provider's item switches its driver back in.
    //
    // The configured driver (the restore target) is a registry entry
    // that creates a plain fake — the restore never touches a real
    // backend.
    static int created = 0;
    created = 0;
    PlayerRegistry::getInstance()->registerPlayer("switchback", "Switchback",
        [](QObject *parent) {
            ++created;
            return new FakePlayer(parent);
        });
    QVERIFY(m_pm->setActivePlayer("switchback"));

    fillPlaylist(2);
    const quint64 providerId = m_pl->itemIds().at(0);
    const quint64 plainId = m_pl->itemIds().at(1);
    MediaItem providerItem = m_store->item(providerId);
    providerItem.provider = m_provider.id();
    QVERIFY(m_store->updateItem(providerItem));

    m_provider.m_player = &m_providerPlayer;
    m_providerPlayer.resetCounters();
    m_fake.resetCounters();
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_provider));

    const quint64 playlist = m_pl->currentPlaylistId();
    m_pm->playFromPlaylist(playlist, 0);
    QCOMPARE(m_pm->player(), &m_providerPlayer);
    QCOMPARE(m_providerPlayer.m_playCount, 1);
    // The replaced player was told to stop — a driver switched out of
    // the way must not keep playing in parallel with its successor.
    QCOMPARE(m_fake.m_stopCount, 1);

    // The plain item: the configured driver is restored (fresh from the
    // registry) and plays it.
    m_pm->playFromPlaylist(playlist, 1);
    QCOMPARE(created, 1);
    QVERIFY(m_pm->player() != &m_providerPlayer);
    QVERIFY(m_pm->player() != &m_fake);
    QCOMPARE(m_pm->playingItem().id, plainId);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_providerPlayer.m_playCount, 1);
    QCOMPARE(m_providerPlayer.m_stopCount, 1);

    // The provider's item again: its driver switches back in (the
    // configured driver is not created in between).
    m_pm->playFromPlaylist(playlist, 0);
    QCOMPARE(m_pm->player(), &m_providerPlayer);
    QCOMPARE(m_providerPlayer.m_playCount, 2);
    QCOMPARE(created, 1);

    m_pm->setActivePlayer("");
}

void PlaybackManagerTest::remoteTransportStateFollowsDriver()
{
    // A transport change outside of doob (the driver mirrors a remote
    // pause/resume/stop): the manager's state — and the stopped item —
    // follow, so the UI does not keep showing "playing".
    fillPlaylist(1);
    m_pm->playCurrent();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    m_fake.triggerPaused();
    QCOMPARE(m_pm->state(), PlaybackManager::Paused);

    m_fake.triggerResumed();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    m_fake.triggerStopped();
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::providerAsyncResolutionDelaysStart()
{
    fillPlaylist(1);
    const quint64 id = m_pl->itemIds().at(0);
    MediaItem item = m_store->item(id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_pm->playCurrent();

    // The resolution is in flight: nothing has started yet, and the item
    // is the one being resolved (the "downloading" mark).
    QCOMPARE(m_deferred.m_pending.count(), 1);
    QCOMPARE(m_fake.m_playCount, 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
    QCOMPARE(m_pm->resolvingItem(), item);

    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));

    // The player got the resolved stream, the display item the original.
    QCOMPARE(m_fake.m_item.source, QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_pm->playingItem().source, item.source);
    QCOMPARE(m_pm->playingItem().provider, m_deferred.id());
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    // The resolution landed: the "downloading" mark is over.
    QVERIFY(!m_pm->resolvingItem().isValid());
}

void PlaybackManagerTest::providerResolutionFailureStops()
{
    fillPlaylist(1);
    const quint64 id = m_pl->itemIds().at(0);
    MediaItem item = m_store->item(id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    QSignalSpy errorSpy(m_pm, &PlaybackManager::playbackError);
    m_pm->playCurrent();

    // No stream (a missing binary, a network failure): the item is
    // unplayable — the player must not be asked for the watch page.
    m_deferred.resolveNow();

    QCOMPARE(m_fake.m_playCount, 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
    // The failure is user-visible (the main window's status bar shows it).
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains("provider"));
    // The "downloading" mark is over, and the failed item carries the
    // "cannot play" mark on its row.
    QVERIFY(!m_pm->resolvingItem().isValid());
    QCOMPARE(m_pm->failedItem(), item);
}

void PlaybackManagerTest::failedItemClearsOnNextPlayAttempt()
{
    fillPlaylist(1);
    const quint64 id0 = m_pl->itemIds().at(0);
    MediaItem item0 = m_store->item(id0);
    item0.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item0));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_pm->playCurrent();
    m_deferred.resolveNow();  // no stream: a failure
    QCOMPARE(m_pm->failedItem(), item0);
    // A stop keeps the mark (the row is still the failed one).
    m_pm->stop();
    QCOMPARE(m_pm->failedItem(), item0);

    // A new play attempt clears the mark (and starts resolving again).
    m_pm->playCurrent();
    QVERIFY(!m_pm->failedItem().isValid());
    QCOMPARE(m_pm->resolvingItem(), item0);

    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_pm->playingItem().id, id0);
    QVERIFY(!m_pm->resolvingItem().isValid());
}

void PlaybackManagerTest::missingLocalFileFailsToPlay()
{
    // A local item whose file is gone: a failure before the player is
    // even asked.
    fillPlaylist(1);
    const quint64 id = m_pl->itemIds().at(0);
    MediaItem item = m_store->item(id);
    const QString path = "/tmp/doob_missing_test_gone.mp3";
    item.source = QUrl::fromLocalFile(path);
    QVERIFY(m_store->updateItem(item));
    QFile::remove(path);

    QSignalSpy errorSpy(m_pm, &PlaybackManager::playbackError);
    m_pm->playCurrent();

    QCOMPARE(m_fake.m_playCount, 0);  // the player was never asked
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
    QCOMPARE(m_pm->failedItem(), item);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains("missing"));
}

void PlaybackManagerTest::playerErrorMarksItemFailedAndStops()
{
    // Any player failure (a corrupt file, a dead connection) marks the
    // item and stops — it is not treated as a track end (no
    // auto-advance over the broken row).
    fillPlaylist(2);
    m_pm->playCurrent();
    const MediaItem item = m_pm->playingItem();
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);

    QSignalSpy errorSpy(m_pm, &PlaybackManager::playbackError);
    m_fake.fail("disk on fire");

    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
    QCOMPARE(m_pm->failedItem(), item);
    QCOMPARE(m_pl->currentIndex(), 0);  // no auto-advance
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains("disk on fire"));
}

void PlaybackManagerTest::newPlaySupersedesInFlightResolution()
{
    fillPlaylist(2);
    const quint64 id1 = m_pl->itemIds().at(0);
    const quint64 id2 = m_pl->itemIds().at(1);
    MediaItem item1 = m_store->item(id1);
    item1.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item1));
    MediaItem item2 = m_store->item(id2);
    item2.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item2));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_pm->playCurrent();  // item1's resolution goes in flight
    m_pm->next();         // item2's supersedes it
    QCOMPARE(m_deferred.m_pending.count(), 2);

    // The stale callback is a no-op, even when it delivers a stream.
    m_deferred.resolveNow(QUrl("https://stream.example.com/first"));
    QCOMPARE(m_fake.m_playCount, 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);

    m_deferred.resolveNow(QUrl("https://stream.example.com/second"));
    QCOMPARE(m_fake.m_playCount, 1);
    QCOMPARE(m_fake.m_item.source, QUrl("https://stream.example.com/second"));
    QCOMPARE(m_pm->playingItem().id, id2);
}

void PlaybackManagerTest::stopSupersedesInFlightResolution()
{
    fillPlaylist(1);
    const quint64 id = m_pl->itemIds().at(0);
    MediaItem item = m_store->item(id);
    item.provider = m_deferred.id();
    QVERIFY(m_store->updateItem(item));
    QVERIFY(SourceManager::getInstance()->registerProvider(&m_deferred));

    m_pm->playCurrent();
    m_pm->stop();

    // A late resolution must not start playback after a stop.
    m_deferred.resolveNow(QUrl("https://stream.example.com/s.m3u8"));
    QCOMPARE(m_fake.m_playCount, 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Stopped);
    QVERIFY(!m_pm->playingItem().isValid());
}

void PlaybackManagerTest::volumeAppliesToPlayerAndPersists()
{
    m_pm->setVolume(40);

    QCOMPARE(m_pm->volume(), 40);
    QCOMPARE(m_fake.m_volume, 0.4F);
    // Write-through into the settings store.
    QVERIFY(SettingsManager::getInstance()->isModified("playback.volume"));
    QCOMPARE(SettingsManager::getInstance()->value("playback.volume").toInt(), 40);
}

void PlaybackManagerTest::setVolumeClampsToRange()
{
    m_pm->setVolume(150);
    QCOMPARE(m_pm->volume(), 100);
    QCOMPARE(m_fake.m_volume, 1.0F);

    m_pm->setVolume(-5);
    QCOMPARE(m_pm->volume(), 0);
    QCOMPARE(m_fake.m_volume, 0.0F);
}

void PlaybackManagerTest::volumeChangedEmitsOnlyOnChange()
{
    QSignalSpy spy(m_pm, &PlaybackManager::volumeChanged);

    m_pm->setVolume(50);
    QCOMPARE(spy.count(), 1);
    m_pm->setVolume(50);  // the same value: a no-op
    QCOMPARE(spy.count(), 1);
    m_pm->setVolume(51);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().first().toInt(), 51);
}

void PlaybackManagerTest::muteSilencesPlayerKeepingVolume()
{
    m_pm->setVolume(60);
    m_pm->setMuted(true);

    QVERIFY(m_pm->muted());
    QCOMPARE(m_fake.m_muted, true);
    QCOMPARE(m_pm->volume(), 60);  // kept, not zeroed
    QCOMPARE(m_fake.m_volume, 0.6F);  // the level travels with the mute

    m_pm->setMuted(false);

    QVERIFY(!m_pm->muted());
    QCOMPARE(m_fake.m_muted, false);
    QCOMPARE(m_fake.m_volume, 0.6F);
}

void PlaybackManagerTest::settingVolumeWhileMutedUnmutes()
{
    m_pm->setMuted(true);
    m_pm->setVolume(30);

    QVERIFY(!m_pm->muted());
    QCOMPARE(m_pm->volume(), 30);
    QCOMPARE(m_fake.m_volume, 0.3F);
}

void PlaybackManagerTest::volumeFollowsDriverSwitch()
{
    m_pm->setVolume(20);

    m_fakeDefault.resetCounters();
    m_pm->setPlayer(&m_fakeDefault);

    // The fresh driver starts at the current level.
    QCOMPARE(m_fakeDefault.m_volume, 0.2F);
}

void PlaybackManagerTest::driverSwitchEmitsSupportChange()
{
    QSignalSpy spy(m_pm, &PlaybackManager::volumeSupportChanged);

    FakePlayer *quiet = new FakePlayer(this);
    quiet->m_supportsVolume = false;

    m_pm->setPlayer(quiet);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toBool(), false);
    QVERIFY(!m_pm->volumeSupported());

    m_pm->setPlayer(&m_fake);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().first().toBool(), true);
    QVERIFY(m_pm->volumeSupported());
}

void PlaybackManagerTest::boostAppliesToPlayerAndPersists()
{
    QSignalSpy spy(m_pm, &PlaybackManager::volumeBoostChanged);

    m_pm->setVolumeBoost(150);

    QCOMPARE(m_pm->volumeBoost(), 150);
    // The driver is asked for a gain above 1.0.
    QCOMPARE(m_fake.m_boost, 1.5F);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 150);
    // Write-through into the settings store.
    QVERIFY(SettingsManager::getInstance()->isModified("playback.volume_boost"));
    QCOMPARE(SettingsManager::getInstance()->value("playback.volume_boost").toInt(), 150);

    m_pm->setVolumeBoost(150);  // the same gain: a no-op
    QCOMPARE(spy.count(), 1);
}

void PlaybackManagerTest::setVolumeBoostClampsToRange()
{
    m_pm->setVolumeBoost(300);
    QCOMPARE(m_pm->volumeBoost(), 200);
    QCOMPARE(m_fake.m_boost, 2.0F);

    m_pm->setVolumeBoost(-10);
    QCOMPARE(m_pm->volumeBoost(), 0);
    QCOMPARE(m_fake.m_boost, 0.0F);
}

void PlaybackManagerTest::boostNotPushedToDriverWithoutHeadroom()
{
    FakePlayer *noHeadroom = new FakePlayer(this);
    noHeadroom->m_supportsBoost = false;
    m_pm->setPlayer(noHeadroom);

    m_pm->setVolumeBoost(150);

    QCOMPARE(m_pm->volumeBoost(), 150);
    QCOMPARE(noHeadroom->m_setBoostCount, 0);  // the gain is kept, not asked

    m_pm->setPlayer(&m_fake);
}

void PlaybackManagerTest::boostFollowsDriverSwitch()
{
    m_pm->setVolumeBoost(150);

    m_fakeDefault.resetCounters();
    m_pm->setPlayer(&m_fakeDefault);

    // The fresh driver starts at the current gain.
    QCOMPARE(m_fakeDefault.m_boost, 1.5F);
}

void PlaybackManagerTest::sinkVolumeReportMovesStateWithoutRepush()
{
    QSignalSpy spy(m_pm, &PlaybackManager::volumeChanged);

    m_pm->setVolume(60);
    QCOMPARE(m_fake.m_setVolumeCount, 1);

    // An external client (pavucontrol) changed the app's stream volume:
    // the state — and the setting — follow it.
    m_fake.triggerSinkVolume(0.42F);

    QCOMPARE(spy.count(), 2);  // setVolume(60) + the report
    QCOMPARE(spy.last().first().toInt(), 42);
    QCOMPARE(m_pm->volume(), 42);
    QCOMPARE(SettingsManager::getInstance()->value("playback.volume").toInt(), 42);
    // The previous level is NOT pushed back — the server is the source
    // of truth.
    QCOMPARE(m_fake.m_setVolumeCount, 1);
}

void PlaybackManagerTest::sinkVolumeEchoIsANoOp()
{
    QSignalSpy spy(m_pm, &PlaybackManager::volumeChanged);

    m_pm->setVolume(60);
    QCOMPARE(spy.count(), 1);

    // The server echoes doob's own push back: the state already
    // matches — no emit, no re-push.
    m_fake.triggerSinkVolume(0.6F);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_pm->volume(), 60);
    QCOMPARE(m_fake.m_setVolumeCount, 1);
}

void PlaybackManagerTest::sinkMuteReportMovesStateWithoutRepush()
{
    QSignalSpy spy(m_pm, &PlaybackManager::mutedChanged);

    m_pm->setVolume(60);
    // The level's apply pushed the (un)mute flag once already (the
    // fake has server-side mute).
    const int mutePushesBefore = m_fake.m_setMuteCount;
    m_fake.triggerSinkMute(true);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toBool(), true);
    QVERIFY(m_pm->muted());
    QCOMPARE(SettingsManager::getInstance()->value("playback.muted").toBool(), true);
    QCOMPARE(m_fake.m_setMuteCount, mutePushesBefore);  // no re-push

    m_fake.triggerSinkMute(true);  // an echo: a no-op
    QCOMPARE(spy.count(), 1);

    m_fake.triggerSinkMute(false);
    QCOMPARE(spy.count(), 2);
    QVERIFY(!m_pm->muted());
    // The level survived the external mute and unmute.
    QCOMPARE(m_pm->volume(), 60);
}

void PlaybackManagerTest::maxVolumeSettingMigratesToBoost()
{
    // A settings file from before the slider ceiling was dropped
    // carries "playback.max_volume": loadSettings carries the ceiling
    // into the boost and drops the key.
    QFile f(m_dir->path() + "/settings.json");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"playback.max_volume": 150})");
    f.close();
    SettingsManager::getInstance()->load();

    m_pm->loadSettings();

    QCOMPARE(m_pm->volumeBoost(), 150);
    QVERIFY(!SettingsManager::getInstance()->isModified("playback.max_volume"));
    QCOMPARE(SettingsManager::getInstance()->value("playback.max_volume"), QVariant());

    // An explicit boost wins over the carried ceiling.
    f.open(QIODevice::WriteOnly);
    f.write(R"({"playback.max_volume": 150, "playback.volume_boost": 120})");
    f.close();
    SettingsManager::getInstance()->load();

    m_pm->loadSettings();

    QCOMPARE(m_pm->volumeBoost(), 120);
    QVERIFY(!SettingsManager::getInstance()->isModified("playback.max_volume"));
}

void PlaybackManagerTest::settingsEditVolumePropagatesToState()
{
    // The settings page edits the SettingsManager directly: the manager must
    // follow (loadSettings() binds it to the test's SettingsManager).
    m_pm->loadSettings();
    QSignalSpy spy(m_pm, &PlaybackManager::volumeChanged);

    SettingsManager::getInstance()->setValue("playback.volume", 65);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_pm->volume(), 65);
    QCOMPARE(m_fake.m_volume, 0.65F);
}

void PlaybackManagerTest::settingsEditMuteSilencesThePlayer()
{
    m_pm->loadSettings();
    m_pm->setVolume(60);

    SettingsManager::getInstance()->setValue("playback.muted", true);

    QVERIFY(m_pm->muted());
    QCOMPARE(m_fake.m_muted, true);
    QCOMPARE(m_fake.m_volume, 0.6F);  // the level is kept
}

void PlaybackManagerTest::settingsEditBoostPropagatesToState()
{
    m_pm->loadSettings();
    QSignalSpy spy(m_pm, &PlaybackManager::volumeBoostChanged);

    SettingsManager::getInstance()->setValue("playback.volume_boost", 130);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_pm->volumeBoost(), 130);
    QCOMPARE(m_fake.m_boost, 1.3F);
}

void PlaybackManagerTest::shuffleTogglesAndPersists()
{
    QSignalSpy spy(m_pm, &PlaybackManager::shuffleChanged);

    m_pm->toggleShuffle();
    QVERIFY(m_pm->shuffle());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toBool(), true);
    // Write-through into the settings store.
    QVERIFY(SettingsManager::getInstance()->isModified("playback.shuffle"));
    QCOMPARE(SettingsManager::getInstance()->value("playback.shuffle").toBool(), true);

    m_pm->toggleShuffle();
    QVERIFY(!m_pm->shuffle());
    QCOMPARE(spy.count(), 2);
}

void PlaybackManagerTest::setShuffleEmitsOnlyOnChange()
{
    QSignalSpy spy(m_pm, &PlaybackManager::shuffleChanged);

    m_pm->setShuffle(true);
    QCOMPARE(spy.count(), 1);
    m_pm->setShuffle(true);  // the same value: a no-op
    QCOMPARE(spy.count(), 1);
}

void PlaybackManagerTest::cycleRepeatCyclesAllModes()
{
    QSignalSpy spy(m_pm, &PlaybackManager::repeatChanged);
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::NoRepeat);

    m_pm->cycleRepeat();
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatPlaylist);
    m_pm->cycleRepeat();
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatTrack);
    m_pm->cycleRepeat();
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::NoRepeat);
    QCOMPARE(spy.count(), 3);
}

void PlaybackManagerTest::shuffleNextPicksADifferentTrack()
{
    fillPlaylist(3);
    m_pm->playCurrent();
    QCOMPARE(m_pl->currentIndex(), 0);

    m_pm->setShuffle(true);

    // The next track is always a different row; over enough presses the
    // walk must leave the starting row and visit more than one of the rest.
    QSet<int> visited;
    int lastIndex = 0;
    for (int i = 0; i < 30; ++i) {
        m_pm->next();
        const int index = m_pl->currentIndex();
        QVERIFY(index >= 0 && index < 3);
        QVERIFY(index != lastIndex);  // never stays on the same row
        visited.insert(index);
        lastIndex = index;
    }
    QVERIFY(visited.size() >= 2);
    QVERIFY(m_pm->state() == PlaybackManager::Playing);
}

void PlaybackManagerTest::shuffleSingleTrackStaysPut()
{
    fillPlaylist(1);
    m_pm->playCurrent();
    m_pm->setShuffle(true);

    m_pm->next();

    // A single-row playlist has no "other" row: it stays (and keeps
    // playing the one track).
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
    QCOMPARE(m_fake.m_item.id, 1ULL);
}

void PlaybackManagerTest::repeatOneReplaysOnFinish()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_playCount, 1);

    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatTrack);
    m_fake.triggerFinished();

    // The same row replays itself.
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_item.id, 1ULL);
    QCOMPARE(m_fake.m_playCount, 2);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::repeatPlaylistWrapsOnFinish()
{
    fillPlaylist(2);
    m_pl->setCurrentIndex(1);
    QCOMPARE(m_fake.m_playCount, 1);

    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatPlaylist);
    m_fake.triggerFinished();

    // The last row wraps to the first.
    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_item.id, 1ULL);
    QCOMPARE(m_fake.m_playCount, 2);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::repeatPlaylistNextWraps()
{
    fillPlaylist(2);
    m_pm->playCurrent();
    QCOMPARE(m_pl->currentIndex(), 0);

    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatPlaylist);
    m_pm->next();
    QCOMPARE(m_pl->currentIndex(), 1);
    m_pm->next();  // at the end: wraps to the first

    QCOMPARE(m_pl->currentIndex(), 0);
    QCOMPARE(m_fake.m_item.id, 1ULL);
    QCOMPARE(m_pm->state(), PlaybackManager::Playing);
}

void PlaybackManagerTest::repeatOneManualNextStillMovesOn()
{
    // Repeat one governs the auto-advance only: a manual next moves on.
    fillPlaylist(2);
    m_pm->playCurrent();
    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatTrack);

    m_pm->next();

    QCOMPARE(m_pl->currentIndex(), 1);
    QCOMPARE(m_fake.m_item.id, 2ULL);
}

void PlaybackManagerTest::repeatSettingPersistsAndReloads()
{
    m_pm->setRepeat(PlaybackManager::RepeatMode::RepeatTrack);
    QVERIFY(SettingsManager::getInstance()->isModified("playback.repeat"));
    QCOMPARE(SettingsManager::getInstance()->value("playback.repeat").toInt(),
             static_cast<int>(PlaybackManager::RepeatMode::RepeatTrack));

    // A stored value is re-read by loadSettings() (a fresh
    // SettingsManager reload picks it back up from the file).
    SettingsManager::getInstance()->load();
    m_pm->loadSettings();
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatTrack);
}

void PlaybackManagerTest::settingsEditShuffleAndRepeatPropagate()
{
    m_pm->loadSettings();
    QSignalSpy shuffleSpy(m_pm, &PlaybackManager::shuffleChanged);
    QSignalSpy repeatSpy(m_pm, &PlaybackManager::repeatChanged);

    SettingsManager::getInstance()->setValue("playback.shuffle", true);
    QCOMPARE(shuffleSpy.count(), 1);
    QVERIFY(m_pm->shuffle());

    SettingsManager::getInstance()->setValue("playback.repeat",
                                             static_cast<int>(PlaybackManager::RepeatMode::RepeatPlaylist));
    QCOMPARE(repeatSpy.count(), 1);
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::RepeatPlaylist);

    // An out-of-range stored value is refused: NoRepeat.
    SettingsManager::getInstance()->setValue("playback.repeat", 42);
    QCOMPARE(m_pm->repeat(), PlaybackManager::RepeatMode::NoRepeat);
}
}


QTEST_MAIN(PlaybackManagerTest)

#include "playbackmanager_test.moc"
