// VJ LINK: one Traktor deck, read for the transmitter (TraktorVJLink.qml posts it).
// Paths checked on Traktor 4.5.1 (docs/VJLINK.md). Installed by the VJ LINK transmitter; do not edit there.
import QtQuick 2.0
import CSI 1.0

Item {
  id: deck
  property int deckId: 1
  readonly property string base: "app.traktor.decks." + deckId + "."
  // Traktor moved the playhead (it does so about 33 times a second while playing): post now, while the values are fresh.
  signal moved()

  // fast (about 33 updates a second)
  AppProperty { id: loaded;     path: base + "is_loaded" }
  AppProperty { id: play;       path: base + "play" }
  AppProperty { id: pos;        path: base + "track.player.playhead_position"; onValueChanged: deck.moved() }
  AppProperty { id: rate;       path: base + "track.player.effective_tempo" }
  AppProperty { id: beatphase;  path: base + "tempo.beatphase" }
  AppProperty { id: beat;       path: base + "track.beat_pos_relative_to_grid" }
  AppProperty { id: bpm;        path: base + "tempo.true_bpm" }
  AppProperty { id: loopOn;     path: base + "loop.is_in_active_loop" }
  AppProperty { id: loopStart;  path: base + "track.cue.active.start_pos" }
  AppProperty { id: loopLen;    path: base + "track.cue.active.length" }
  AppProperty { id: stem1;      path: base + "stems.1.level" }
  AppProperty { id: stem2;      path: base + "stems.2.level" }
  AppProperty { id: stem3;      path: base + "stems.3.level" }
  AppProperty { id: stem4;      path: base + "stems.4.level" }
  AppProperty { id: level;      path: "app.traktor.mixer.channels." + deckId + ".level.prefader.linear.meter" }
  AppProperty { id: volume;     path: "app.traktor.mixer.channels." + deckId + ".volume" }

  // slow (track and hotcues)
  AppProperty { id: title;      path: base + "content.title" }
  AppProperty { id: artist;     path: base + "content.artist" }
  AppProperty { id: mkey;       path: base + "content.musical_key" }
  AppProperty { id: fpath;      path: base + "track.content.file_path" }
  AppProperty { id: entry;      path: base + "track.content.entry_key" }
  AppProperty { id: trackLen;   path: base + "track.content.track_length" }
  AppProperty { id: stableBpm;  path: base + "track.stable_bpm" }
  AppProperty { id: hasStems;   path: base + "stems.stem_info_exists" }
  AppProperty { id: h1e; path: base + "track.cue.hotcues.1.exists" }
  AppProperty { id: h1t; path: base + "track.cue.hotcues.1.type" }
  AppProperty { id: h1s; path: base + "track.cue.hotcues.1.start_pos" }
  AppProperty { id: h1l; path: base + "track.cue.hotcues.1.length" }
  AppProperty { id: h1n; path: base + "track.cue.hotcues.1.name" }
  AppProperty { id: h2e; path: base + "track.cue.hotcues.2.exists" }
  AppProperty { id: h2t; path: base + "track.cue.hotcues.2.type" }
  AppProperty { id: h2s; path: base + "track.cue.hotcues.2.start_pos" }
  AppProperty { id: h2l; path: base + "track.cue.hotcues.2.length" }
  AppProperty { id: h2n; path: base + "track.cue.hotcues.2.name" }
  AppProperty { id: h3e; path: base + "track.cue.hotcues.3.exists" }
  AppProperty { id: h3t; path: base + "track.cue.hotcues.3.type" }
  AppProperty { id: h3s; path: base + "track.cue.hotcues.3.start_pos" }
  AppProperty { id: h3l; path: base + "track.cue.hotcues.3.length" }
  AppProperty { id: h3n; path: base + "track.cue.hotcues.3.name" }
  AppProperty { id: h4e; path: base + "track.cue.hotcues.4.exists" }
  AppProperty { id: h4t; path: base + "track.cue.hotcues.4.type" }
  AppProperty { id: h4s; path: base + "track.cue.hotcues.4.start_pos" }
  AppProperty { id: h4l; path: base + "track.cue.hotcues.4.length" }
  AppProperty { id: h4n; path: base + "track.cue.hotcues.4.name" }
  AppProperty { id: h5e; path: base + "track.cue.hotcues.5.exists" }
  AppProperty { id: h5t; path: base + "track.cue.hotcues.5.type" }
  AppProperty { id: h5s; path: base + "track.cue.hotcues.5.start_pos" }
  AppProperty { id: h5l; path: base + "track.cue.hotcues.5.length" }
  AppProperty { id: h5n; path: base + "track.cue.hotcues.5.name" }
  AppProperty { id: h6e; path: base + "track.cue.hotcues.6.exists" }
  AppProperty { id: h6t; path: base + "track.cue.hotcues.6.type" }
  AppProperty { id: h6s; path: base + "track.cue.hotcues.6.start_pos" }
  AppProperty { id: h6l; path: base + "track.cue.hotcues.6.length" }
  AppProperty { id: h6n; path: base + "track.cue.hotcues.6.name" }
  AppProperty { id: h7e; path: base + "track.cue.hotcues.7.exists" }
  AppProperty { id: h7t; path: base + "track.cue.hotcues.7.type" }
  AppProperty { id: h7s; path: base + "track.cue.hotcues.7.start_pos" }
  AppProperty { id: h7l; path: base + "track.cue.hotcues.7.length" }
  AppProperty { id: h7n; path: base + "track.cue.hotcues.7.name" }
  AppProperty { id: h8e; path: base + "track.cue.hotcues.8.exists" }
  AppProperty { id: h8t; path: base + "track.cue.hotcues.8.type" }
  AppProperty { id: h8s; path: base + "track.cue.hotcues.8.start_pos" }
  AppProperty { id: h8l; path: base + "track.cue.hotcues.8.length" }
  AppProperty { id: h8n; path: base + "track.cue.hotcues.8.name" }

  function n(v) { var x = Number(v); return isFinite(x) ? x : 0; }
  function s(v) { return (v === undefined || v === null) ? "" : String(v); }
  function cue(e, t, st, l, nm) { return e.value ? [n(t.value), n(st.value), n(l.value), s(nm.value)] : null; }

  function fast() {
    return [loaded.value ? 1 : 0, play.value ? 1 : 0, n(pos.value), n(rate.value), n(beatphase.value), n(beat.value),
            n(bpm.value), loopOn.value ? 1 : 0, n(loopStart.value), n(loopLen.value),
            n(stem1.value), n(stem2.value), n(stem3.value), n(stem4.value), n(level.value), n(volume.value)];
  }
  function info() {
    return { title: s(title.value), artist: s(artist.value), key: s(mkey.value), file: s(fpath.value),
             entry: n(entry.value), length: n(trackLen.value), bpm: n(stableBpm.value), stems: !!hasStems.value,
             hotcues: [cue(h1e, h1t, h1s, h1l, h1n), cue(h2e, h2t, h2s, h2l, h2n), cue(h3e, h3t, h3s, h3l, h3n), cue(h4e, h4t, h4s, h4l, h4n),
                       cue(h5e, h5t, h5s, h5l, h5n), cue(h6e, h6t, h6s, h6l, h6n), cue(h7e, h7t, h7s, h7l, h7n), cue(h8e, h8t, h8s, h8l, h8n)] };
  }
}
