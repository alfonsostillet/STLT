# Da provare (Windows) alla prossima versione

Correzioni già fatte e verificate dai test automatici, da provare sul PC vero prima di considerarle chiuse.

## Dalle prove della 0.3.0

- [ ] **Progetto che si riapre com'era.** Prepara un set con alcune clip in riproduzione, salvalo (Ctrl+S), chiudi STLT-VJ e riaprilo: le clip devono ripartire e l'uscita mostrare l'immagine. Prova anche *Composition → Open...* su un progetto salvato, e la timeline: la testina deve tornare dove l'avevi lasciata.
  - Se dopo la riapertura la griglia è **vuota** (non solo ferma), segnalalo: vorrebbe dire un problema diverso, nel salvataggio o nella finestra Salva/Apri.
- [ ] **Effetto STLT-CHASER senza la finestra "MESSAGE FROM DEAR IMGUI"** quando apri le sue sezioni Delayer, Wiper e Outliner.
- [ ] **Timeline con il Chaser attivo nella griglia.** Metti STLT-CHASER sulla composizione nella vista a griglia, passa alla timeline: l'uscita deve mostrare la timeline normalmente.

## Ancora da provare dalla checklist della 0.3.0

- [ ] **Spout e NDI**: uno screen dell'Advanced Output mandato in Spout e in NDI deve vedersi in un altro programma.

## Luci dei controller (nuovo)

- [ ] **APC mini mk2**: i pad mappati sulle clip si accendono **blu** se c'è una clip, **verde** quando suona, **verde pulsante** se parte al prossimo beat (Launch su Beat o Bar); i pulsanti delle tracce e delle scene si accendono o lampeggiano (per esempio su Bypass, Solo, colonne); il pad mappato su TAP lampeggia col tempo. Chiudendo STLT-VJ si spengono.
  - Le mappature fatte prima di questa versione non sanno da quale controller vengono: le luci vanno a tutti i controller collegati. Rimappando un pad, va solo al suo.
  - Se un controller si comporta male, *Preferences → MIDI → Controller lights* le spegne.

## Parametri che seguono l'audio e nuove animazioni (nuovo)

- [ ] **Ingresso audio**: in *Preferences → Audio → Audio Input* scegli il microfono, la scheda audio o l'ingresso del mixer. Le quattro barre (Level, Bass, Mid, Treble) devono muoversi con il suono; *Input Gain* le alza o le abbassa (doppio clic: 0 dB).
- [ ] **Un parametro che segue il suono**: clic sul nome di un parametro (per esempio l'Opacity di una clip o un parametro di un effetto) → *Animate* → *Audio Bass*: il parametro deve muoversi tra From e To insieme alla cassa. Prova anche Level, Mid e Treble, e *Reverse*.
- [ ] **Periodo in secondi**: in *Animate*, accanto a *Period*, scegli *Seconds* al posto di *Beats*: l'animazione deve andare al suo passo anche cambiando il BPM.
- [ ] **Smooth Random**: come Random ma scivola da un valore all'altro invece di saltare.
- [ ] Il progetto salvato e riaperto deve ricordare tutte queste impostazioni.

## Ableton Link e MIDI clock (nuovo)

- [ ] **Link**: con Ableton Live (o Traktor, rekordbox, Serato con Link attivo) sullo stesso PC o sulla stessa rete, premi **LINK** accanto al BPM. Al primo avvio Windows può chiedere il permesso per la rete: consenti. Il pulsante deve mostrare il numero delle altre app (per esempio *LINK 1*); il BPM di STLT-VJ diventa quello di Live e i quattro pallini della battuta vanno insieme al metronomo di Live. Cambia il BPM in Live: cambia anche in STLT-VJ. Cambialo in STLT-VJ (o con TAP): cambia anche in Live.
- [ ] **MIDI clock**: in *Preferences → Sync → MIDI Clock* scegli l'ingresso del mixer o della drum machine (deve essere acceso in *Preferences → MIDI*). Nella barra si accende **CLOCK**, il BPM segue il clock; premendo Play/Start sul dispositivo la battuta riparte da 1. Con lo Stop le animazioni continuano al tempo di prima.
- [ ] **Delay**: se le immagini arrivano dopo la musica (proiettore, LED), sposta *Preferences → Sync → Delay* a destra finché i flash cadono sulla cassa.
- [ ] Con Link o clock attivi, Ctrl+Z non deve cambiare il BPM.
