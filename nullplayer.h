#ifndef NULLPLAYER_H
#define NULLPLAYER_H

#include "audioplayer.h"

// The built-in fallback driver: plays nothing. It exists so the
// PlaybackManager's resolution chain (the selected driver, else the
// preferred default a plugin registered, else this) always ends in a
// driver without a real media backend being bundled in the core — the
// FFmpeg player plugin is the out-of-the-box backend, and a doob
// running without it reports a play failure through the driver's
// errorOccurred signal (the manager marks the item failed and shows
// the message) instead of playing into the void.
class NullPlayer : public AudioPlayer
{
    Q_OBJECT
public:
    explicit NullPlayer(QObject *parent = nullptr);

    void play(const MediaItem &item) override;
    void pause() override;
    void resume() override;
    void stop() override;

    qint64 positionMs() const override { return 0; }
};

#endif // NULLPLAYER_H
