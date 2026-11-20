#include "nullplayer.h"

NullPlayer::NullPlayer(QObject *parent)
    : AudioPlayer(parent)
{
}

void NullPlayer::play(const MediaItem &item)
{
    Q_UNUSED(item)
    // A refusal, not a failure of the item: the manager marks the item
    // failed and reports the message (a visible "no backend", not a
    // silent silence).
    emit errorOccurred(QStringLiteral("No playback backend available — "
                                      "the FFmpeg player plugin is not "
                                      "loaded"));
}

void NullPlayer::pause()
{
}

void NullPlayer::resume()
{
}

void NullPlayer::stop()
{
}
