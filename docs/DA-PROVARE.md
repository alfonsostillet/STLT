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
