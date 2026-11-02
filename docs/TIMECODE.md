# Timeline agganciata al timecode: i protocolli dei live

Studio per il punto 11 della roadmap: la timeline (vista *Arrangement*) che segue un timecode esterno, come in uno show dove luci, audio e video partono dalla stessa sorgente.

## Cosa si usa davvero nei live

| Protocollo | Come arriva | Chi lo manda di solito | Precisione | Note |
|---|---|---|---|---|
| **LTC** (SMPTE audio) | un canale audio (jack, XLR, scheda audio) | registratori, Ableton/Reaper/Logic/QLab con un plugin, console luci, playback di tour | al fotogramma, latenza minima | **Lo standard dei tour.** L'unico che Resolume Arena legge di suo (due ingressi, nelle preferenze Audio). |
| **MTC** (MIDI Time Code) | MIDI (USB o rete: rtpMIDI, loopMIDI) | DAW (Ableton Live, Logic, Cubase, Reaper), QLab, console luci | ±1 fotogramma (un quarto di fotogramma alla volta) | Il più comodo se il playback è un computer: niente cavi audio. Resolume non lo legge (serve un convertitore verso LTC). |
| **Art-Net Timecode** (ArtTimeCode) | rete Ethernet, UDP 6454 | console luci (grandMA, Chamsys, Avolites, Onyx), TouchDesigner, media server | al fotogramma | Comodo se c'è già la rete delle luci. Anche questo Resolume non lo legge. |
| **Pro DJ Link** (Pioneer/AlphaTheta) | rete Ethernet dei CDJ | CDJ-2000NXS2, CDJ-3000, XDJ | posizione nel brano e beat | Proprietario. Pioneer lo concede in licenza (ArKaos, Green Hippo, Resolume, TC Supply) e lo esce con l'app gratuita **PRO DJ LINK Bridge**, che manda anche **Ableton Link**. |
| OSC | rete | TouchDesigner, QLab, Max | dipende | Nessuno standard per il timecode: ognuno usa i suoi indirizzi. |

Fonti: [l'articolo di Resolume sul SMPTE](https://resolume.com/article/46), le richieste nel forum di Resolume per [MTC e Art-Net](https://resolume.com/forum/viewtopic.php?p=99115) e per [l'Art-Net timecode](https://www.resolume.com/forum/viewtopic.php?p=92024), [Pioneer sulle licenze Pro DJ Link](https://www.pioneerdj.com/en/news/2019/pro-dj-link-network-information-licensed-to-vfx-companies/) e [PRO DJ LINK Bridge](https://www.pioneerdj.com/en-gb/product/software/pro-dj-link-bridge/software/overview/).

## Cosa facciamo

1. **LTC, MTC e Art-Net Timecode, tutti e tre**: STLT legge direttamente quello che Resolume legge solo con un convertitore (MTC, Art-Net). Così con un DAW basta un cavo MIDI virtuale, con le luci basta la rete.
2. **Pro DJ Link: attraverso Ableton Link.** Il protocollo è proprietario e il modo pulito è PRO DJ LINK Bridge (gratuito, di Pioneer), che manda il tempo e la battuta dei CDJ come Ableton Link: STLT ora lo segue (pulsante LINK). La posizione nel brano dei CDJ (per agganciare la timeline a un brano preciso) richiederebbe di fare da "CDJ virtuale" sulla rete Pioneer, cosa che solo le aziende con licenza fanno ufficialmente: per ora no.

## Come funziona per chi lo usa

- **Preferences → Sync → Timecode**: la sorgente (Off, LTC, MTC, Art-Net), e per ciascuna da dove: per LTC l'ingresso audio e il canale (sinistro o destro), per MTC l'ingresso MIDI, per Art-Net la porta e l'universo di rete (stream).
- Il **frame rate** si riconosce da solo (24, 25, 29.97 drop frame, 30) e si vede accanto al timecode.
- Sulla timeline, accanto al tempo, il timecode che arriva e la spia **TC** (verde quando la timeline lo segue).
- **Start**: quale timecode corrisponde all'inizio della timeline (per esempio 01:00:00:00, come spesso negli show). Pulsante per prendere quello che arriva in quel momento.
- **La timeline segue**: quando il timecode scorre la timeline suona alla sua posizione; quando si ferma la timeline si ferma; quando salta (il fonico torna indietro) la timeline salta. Piccole differenze si recuperano senza scatti, quelle grandi con un salto.
- **Freewheel**: se il segnale si interrompe per un attimo (un cavo, un pacchetto perso), la timeline va avanti da sola per un tempo che si sceglie (per esempio mezzo secondo) prima di fermarsi.
- **Delay** (quello già in *Preferences → Sync*): compensa il ritardo del proiettore o del LED anche per il timecode.

## Come è fatto dentro

- **LTC**: decodifica del segnale bifase (80 bit a fotogramma, parola di sincronismo 0x3FFD) dall'ingresso audio già usato per far seguire il suono ai parametri. Scritto da noi, con i test che generano LTC e lo rileggono anche con rumore e volume basso.
- **MTC**: quarter frame (F1) e full frame (SysEx F0 7F 7F 01 01), dagli ingressi MIDI come già il MIDI clock. Il quarter frame completa il tempo ogni due fotogrammi: si aggiungono i due fotogrammi di ritardo.
- **Art-Net**: pacchetti ArtTimeCode (OpCode 0x9700) in UDP sulla porta 6454.
- Un unico **Timecode**: il tempo che arriva, quando è arrivato, il frame rate, se scorre; la timeline lo segue come oggi la composizione segue Link e il MIDI clock.
