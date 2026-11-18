// NIMS PRO LINK: posts Traktor's four decks to the NIMS PRO LINK transmitter on this computer (http://127.0.0.1:9125),
// as Traktor updates them: about 33 times a second. Each post has positions and levels; the tracks and hotcues
// go with every 15th (about twice a second).
// "fast" per deck: [loaded, play, pos, rate, beatphase, beat, bpm, loopOn, loopStart, loopLen,
//                   stem1, stem2, stem3, stem4, level, volume]
// Installed by the NIMS PRO LINK transmitter into the Kontrol D2's mapping; do not edit there.
import QtQuick 2.0
import CSI 1.0

Module {
  id: link
  property string endpoint: "http://127.0.0.1:9125/traktor"
  property string session: "s" + Date.now() + "-" + Math.floor(Math.random() * 1000000)
  property int seq: 0
  property var pending: null
  property double pendingAt: 0
  property double sentAt: 0

  NIMSLinkDeck { id: d1; deckId: 1; onMoved: link.soon() }
  NIMSLinkDeck { id: d2; deckId: 2; onMoved: link.soon() }
  NIMSLinkDeck { id: d3; deckId: 3; onMoved: link.soon() }
  NIMSLinkDeck { id: d4; deckId: 4; onMoved: link.soon() }

  // A deck moved: post once the other decks have their new values too (they change in the same update).
  function soon() {
    if (typeof Qt.callLater === "function") Qt.callLater(link.send); else link.send();
  }

  function send() {
    var now = Date.now();
    // Not more often than every 10 ms (a moving deck and the timer can both ask).
    if (now - sentAt < 10) return;
    // One request at a time. Never abort(): in Qt an interrupted request can leave the connections stuck (seen
    // on 17/11/2026, restarting the receiving side). With nobody listening a request ends by itself with an
    // error; after 3 s without an answer it is left alone, without abort.
    if (pending) {
      if (now - pendingAt < 3000) return;
      pending = null;
    }
    var msg = { v: 2, session: session, seq: seq, t: now, fast: [d1.fast(), d2.fast(), d3.fast(), d4.fast()] };
    if (seq % 15 === 0) msg.info = [d1.info(), d2.info(), d3.info(), d4.info()];
    seq++;
    sentAt = now;
    try {
      var xhr = new XMLHttpRequest();
      pending = xhr; pendingAt = now;
      xhr.onreadystatechange = function() { if (xhr.readyState === XMLHttpRequest.DONE && link.pending === xhr) link.pending = null; };
      xhr.open("POST", endpoint, true);
      xhr.setRequestHeader("Content-Type", "application/json");
      xhr.send(JSON.stringify(msg));
    } catch (e) { pending = null; }
  }

  // Also when nothing moves (decks paused: loads, hotcues, faders), and if the moves are not signalled.
  Timer { interval: 30; repeat: true; running: true; onTriggered: link.send() }
}
