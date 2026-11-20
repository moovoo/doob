#!/usr/bin/env bash
# The fake spotify-web helper (tests only): speaks the JSON-lines
# protocol the driver's SpotifyIpc talks, without the WebEngine, the
# SDK, the Web API or any PulseAudio server.
#
# The command -> event table (deterministic, the assertions rely on it):
#   (start) -> ready (device "fake-device")
#   play    -> state (playing, position 0, duration 210000)
#             + play-result ok
#   pause   -> state (paused, position 5000, duration 210000)
#   resume  -> state (playing, position 5000, duration 210000)
#   seek    -> position 9999 (duration 210000)
#   volume  -> state (playing, position 0, duration 210000, "Next
#              Track" — a synthetic "the SDK started a new track": the
#              driver's backward-jump finished() detector decides by
#              the title)
#   token   -> position 0 (an ack: the token command landed)
#   connect -> connect-result ok + ready (device "fake-device" — a
#              fresh device on every connect, like the real SDK)
#   disconnect -> (nothing — the session closes, the helper stays up)
#   quit    -> exit 0
echo '{"type":"ready","device":"fake-device"}'
while IFS= read -r line; do
  case "$line" in
    *'"cmd":"play"'*)
      echo '{"type":"state","paused":false,"position":0,"duration":210000,"title":"Fake Track","context":"spotify:album:fake"}'
      echo '{"type":"play-result","ok":true}'
      ;;
    *'"cmd":"pause"'*)
      echo '{"type":"state","paused":true,"position":5000,"duration":210000,"title":"Fake Track","context":"spotify:album:fake"}'
      ;;
    *'"cmd":"resume"'*)
      echo '{"type":"state","paused":false,"position":5000,"duration":210000,"title":"Fake Track","context":"spotify:album:fake"}'
      ;;
    *'"cmd":"seek"'*)
      echo '{"type":"position","position":9999,"duration":210000}'
      ;;
    *'"cmd":"volume"'*)
      echo '{"type":"state","paused":false,"position":0,"duration":210000,"title":"Next Track","context":"spotify:album:fake"}'
      ;;
    *'"cmd":"token"'*)
      echo '{"type":"position","position":0,"duration":210000}'
      ;;
    *'"cmd":"connect"'*)
      echo '{"type":"connect-result","ok":true}'
      echo '{"type":"ready","device":"fake-device"}'
      ;;
    *'"cmd":"disconnect"'*)
      :
      ;;
    *'"cmd":"quit"'*)
      exit 0
      ;;
  esac
done
