# Da provare (Windows) alla prossima versione

Correzioni già fatte e verificate dai test automatici, da provare sul PC vero prima di considerarle chiuse.

## Dalle prove della 0.3.0

- [ ] **Progetto che si riapre com'era.** Prepara un set con alcune clip in riproduzione, salvalo (Ctrl+S), chiudi STLT e riaprilo: le clip devono ripartire e l'uscita mostrare l'immagine. Prova anche *Composition → Open...* su un progetto salvato, e la timeline: la testina deve tornare dove l'avevi lasciata.
  - Se dopo la riapertura la griglia è **vuota** (non solo ferma), segnalalo: vorrebbe dire un problema diverso, nel salvataggio o nella finestra Salva/Apri.
- [ ] **Effetto STLT-CHASER senza la finestra "MESSAGE FROM DEAR IMGUI"** quando apri le sue sezioni Delayer, Wiper e Outliner.
- [ ] **Timeline con il Chaser attivo nella griglia.** Metti STLT-CHASER sulla composizione nella vista a griglia, passa alla timeline: l'uscita deve mostrare la timeline normalmente.

## Ancora da provare dalla checklist della 0.3.0

- [ ] **Spout e NDI**: uno screen dell'Advanced Output mandato in Spout e in NDI deve vedersi in un altro programma.

## Luci dei controller (nuovo)

- [ ] **APC mini mk2**: i pad mappati sulle clip si accendono **blu** se c'è una clip, **verde** quando suona, **verde pulsante** se parte al prossimo beat (Launch su Beat o Bar); i pulsanti delle tracce e delle scene si accendono o lampeggiano (per esempio su Bypass, Solo, colonne); il pad mappato su TAP lampeggia col tempo. Chiudendo STLT si spengono.
  - Le mappature fatte prima di questa versione non sanno da quale controller vengono: le luci vanno a tutti i controller collegati. Rimappando un pad, va solo al suo.
  - Se un controller si comporta male, *Preferences → MIDI → Controller lights* le spegne.

## Parametri che seguono l'audio e nuove animazioni (nuovo)

- [ ] **Ingresso audio**: in *Preferences → Audio → Audio Input* scegli il microfono, la scheda audio o l'ingresso del mixer. Le quattro barre (Level, Bass, Mid, Treble) devono muoversi con il suono; *Input Gain* le alza o le abbassa (doppio clic: 0 dB).
- [ ] **Un parametro che segue il suono**: clic sul nome di un parametro (per esempio l'Opacity di una clip o un parametro di un effetto) → *Animate* → *Audio Bass*: il parametro deve muoversi tra From e To insieme alla cassa. Prova anche Level, Mid e Treble, e *Reverse*.
- [ ] **Periodo in secondi**: in *Animate*, accanto a *Period*, scegli *Seconds* al posto di *Beats*: l'animazione deve andare al suo passo anche cambiando il BPM.
- [ ] **Smooth Random**: come Random ma scivola da un valore all'altro invece di saltare.
- [ ] Il progetto salvato e riaperto deve ricordare tutte queste impostazioni.

## Ableton Link e MIDI clock (nuovo)

- [ ] **Link**: con Ableton Live (o Traktor, rekordbox, Serato con Link attivo) sullo stesso PC o sulla stessa rete, premi **LINK** accanto al BPM. Al primo avvio Windows può chiedere il permesso per la rete: consenti. Il pulsante deve mostrare il numero delle altre app (per esempio *LINK 1*); il BPM di STLT diventa quello di Live e i quattro pallini della battuta vanno insieme al metronomo di Live. Cambia il BPM in Live: cambia anche in STLT. Cambialo in STLT (o con TAP): cambia anche in Live.
- [ ] **MIDI clock**: in *Preferences → Sync → MIDI Clock* scegli l'ingresso del mixer o della drum machine (deve essere acceso in *Preferences → MIDI*). Nella barra si accende **CLOCK**, il BPM segue il clock; premendo Play/Start sul dispositivo la battuta riparte da 1. Con lo Stop le animazioni continuano al tempo di prima.
- [ ] **Delay**: se le immagini arrivano dopo la musica (proiettore, LED), sposta *Preferences → Sync → Delay* a destra finché i flash cadono sulla cassa.
- [ ] Con Link o clock attivi, Ctrl+Z non deve cambiare il BPM.

## Timeline che segue il timecode (nuovo)

- [ ] **Art-Net** (il più facile da provare): in *Preferences → Sync → Timecode* scegli *Art-Net*; manda timecode da una console luci o da un programma (per esempio TouchDesigner, o un generatore Art-Net gratuito). Nella timeline accendi **TC** e scrivi l'inizio (per esempio 01:00:00:00): la testina deve andare dove dice il timecode, suonare quando scorre, fermarsi quando si ferma, saltare quando salta. Windows può chiedere il permesso per la rete: consenti.
- [ ] **MTC**: da Ableton Live (o Reaper) sullo stesso PC con una porta MIDI virtuale (loopMIDI): in Live *Preferences → Link/Tempo/MIDI*, sulla porta di loopMIDI attiva *Sync* in uscita e il tipo *MTC*. In STLT accendi la porta in *Preferences → MIDI* e scegli *MTC* come sorgente. Play, stop e salti in Live devono muovere la timeline.
- [ ] **LTC**: un file audio di LTC (si trovano generatori online) suonato in un ingresso della scheda audio (o con un cavo da un'uscita a un ingresso): scegli *LTC*, l'ingresso e il canale. Lo stato sotto deve mostrare il timecode e il frame rate giusto.
- [ ] **Take**: con il timecode che arriva, sposta la testina dove deve partire lo show e premi *Take*: da lì in poi la timeline segue con quell'inizio.
- [ ] **Freewheel**: scollegando il cavo (o fermando l'invio) per un attimo, la timeline deve andare avanti per il tempo scelto e poi fermarsi.
- [ ] Durante la riproduzione della timeline Ctrl+Z non deve più far tornare indietro la testina (prima ogni due secondi c'era un passo di annullamento finto).

## Effetti ISF (nuovo)

- [ ] Nel pannello *Effects* c'è la sezione **ISF** con circa 200 effetti divisi per categoria (Blur, Color, Glitch, Halftone, Distortion, Kaleidoscope, Feedback...); nella scheda *Sources* le sorgenti ISF (gradienti, pattern, rumore, forme). Trascinane qualcuno su clip, layer e composizione: devono funzionare come gli altri effetti, con i loro parametri (colori con il selettore colore, punti con X e Y, menu), animabili e mappabili.
- [ ] Prova soprattutto quelli **Feedback** e **Glitch** (usano più passaggi e buffer): per esempio *Echo Trace*, *Comet Tails*, *RGB Trails 3.0*, *Time Glitch RGB*, *Slit Scan*.
- [ ] Un progetto salvato con effetti ISF si riapre con gli stessi effetti e valori.
- [ ] Se hai altri shader ISF (per esempio da VDMX o scaricati), mettili in `C:\ProgramData\ISF` o nella cartella `isf` delle impostazioni di STLT: devono comparire dopo il riavvio.

## Poligoni di qualsiasi forma (nuovo)

- [ ] **+ Polygon → Draw...**: clicca sul canvas (Input o Output) per mettere i punti uno dopo l'altro; per chiudere clicca il primo punto (si ingrandisce quando ci sei sopra), fai doppio clic o premi Invio. Backspace toglie l'ultimo punto, Esc annulla. I punti si agganciano a quelli delle altre slice (Ctrl li lascia liberi).
- [ ] **+ Polygon → Triangle, Rectangle, Pentagon, Hexagon, Octagon, Circle**: forme pronte da modificare.
- [ ] Su un poligono già fatto: **doppio clic sul bordo** aggiunge un punto, oppure tasto destro sul bordo → *Add Point Here*; Canc toglie il punto selezionato. Nelle proprietà, *Points* dice quanti sono (il (?) spiega come).
- [ ] Le forme concave (una stella, una L) devono riempirsi giuste, senza triangoli che escono.

## Tracce su schermi diversi (nuovo, punto 9 proposta B)

- [ ] Nella timeline, accanto al nome di ogni traccia video c'è una pastiglia (*Composition* all'inizio). Cliccala → **New Canvas for Screen 2** (con almeno due screen nell'Advanced Output): la traccia ora va solo sullo Screen 2, e non più sugli altri; lo Screen 2 mostra quello che va sulla sua tela.
- [ ] Manda due tracce sulla stessa tela: si mescolano tra loro (blend e opacità delle tracce come al solito).
- [ ] Nell'Advanced Output, slice per slice, **Source** sceglie Composition o una tela; con la scheda *Input Selection* vedi l'immagine della tela e scegli la parte da prendere (per esempio metà di una tela LED larga per ogni processore).
- [ ] Il pallino colorato accanto alle slice nella lista ha lo stesso colore della pastiglia delle tracce che vanno lì.
- [ ] Salva l'Advanced Output e il progetto, chiudi e riapri: tele, sorgenti delle slice e uscite delle tracce restano.
- [ ] *Remove Canvas* rimanda slice e tracce alla composizione.

## Advanced Output rifatto (nuovo)

- [ ] In alto gli **schermi come schede** con l'anteprima viva (pallino verde quando arrivano a un display, una finestra o in rete); clic per lavorare su uno, tasto destro per accenderlo/spegnerlo, duplicarlo o eliminarlo; *+ Screen* per aggiungerne.
- [ ] A sinistra solo le slice dello schermo scelto; tasto destro su una: su, giù, duplica, elimina.
- [ ] Al centro **Input** (*Takes from*) e **Output** (*Shows on*) affiancati: trascinando un punto in uno si vede subito l'altro. *Input* / *Output* in alto mostrano una vista sola, grande.
- [ ] In basso: se il setup è in uscita, quanti schermi vanno dove, *Unsaved changes*, *Discard Changes*, *Save*. I preset sono in *Presets...* in alto.
- [ ] Dimmi se è più chiaro di prima, e cosa cambieresti.

## Volume delle tracce audio con i keyframe (nuovo)

- [ ] Su una traccia audio della timeline premi **Vol** (in alto a destra nell'intestazione, prima di M e S): sulla traccia compare la linea del volume.
- [ ] **Doppio clic** sulla traccia aggiunge un punto dove clicchi (altezza = volume); **trascina** un punto per spostarlo (Shift: fuori dalla griglia); **tasto destro** su un punto: curva (Linear, Ease In, Ease Out, Ease In Out, Hold) o *Delete*.
- [ ] In riproduzione il volume segue la linea. Spostando le clip la linea resta dov'è nel tempo.
- [ ] Il **rombo** accanto al cursore *Volume* aggiunge o toglie il punto sotto la testina; con dei punti, muovendo il cursore si cambia il punto lì.
- [ ] Con *Vol* acceso le clip di quella traccia non si spostano (per non spostarle per sbaglio): premi di nuovo *Vol* per tornare a muoverle.
- [ ] Salva, chiudi e riapri: i punti restano.
