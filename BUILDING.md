# SeedFlow 0.8.4 — revisione 14, Win32 C++

Basata sui sorgenti 0.8.3 r13 forniti dall'utente. Versione file Windows: 0.8.4.14.

## Novita' r14

- Scheda Velocità: download blu continuo e upload verde tratteggiato, totali
  di tutti i torrent, storico di sessione degli ultimi cinque minuti.
  Campionamento circa una volta al secondo; scala verticale automatica.
  Buffer fisso da 301 campioni, senza file di storico o nuove dipendenze.
  La raccolta continua nella tray; il grafico viene ridisegnato solo se visibile.
- Corretto il passaggio del risultato della verifica porta al thread della GUI:
  nessun puntatore proprietario lasciato nella coda messaggi alla chiusura.
- Validazione di ordine/larghezze delle colonne salvate prima di applicarle.
  Le disposizioni valide e le frecce download/upload restano conservate.
- Dimensioni/argomenti non validi delle icone verificati prima dell'allocazione.
- Nessuno spoof dei contatori: upload/download e annunci ai tracker restano reali.

## Novita' r13

- Icone: set originale "duotone" coerente in tutta l'interfaccia (sorgenti SVG
  in res/icons, strisce PNG incorporate nell'EXE).
- Barra di avanzamento arrotondata nello stesso stile; la verifica dei file
  mostra strisce animate e la percentuale di verifica.
- Colonna Salute: tacche e copie complete stimate dello sciame.
- Opzioni > Preferenze (Ctrl+P): tutte le impostazioni in una finestra con
  albero delle sezioni, OK / Annulla / Applica.
- Avvio con Windows (anche ridotto nella tray) e priorita' del processo.

## Tracker in errore (r12)

libtorrent 2.x annuncia da ogni indirizzo locale. Le schede virtuali (Hyper-V,
WSL, VPN, VirtualBox) o un IPv6 senza connettivita' non raggiungono i tracker:
ora sono ignorati finche' almeno un indirizzo funziona (riga
"** [indirizzi locali] **" nella tab Tracker). Se la tua linea non ha IPv6,
puoi disattivarlo in Preferenze > Connessione.

## Avvio

Chiudi la versione precedente con File > Esci, anche se e' nella tray.
Estrai SeedFlow.exe dallo ZIP e aprilo. I documenti e le licenze accompagnano
il pacchetto, ma non sono file necessari al caricamento del programma.

Questa edizione statica include libtorrent, OpenSSL e runtime MSVC nell'EXE.
Non occorre distribuire DLL native o installare Visual C++ Redistributable.
Rimangono le normali dipendenze di sistema Windows 10/11.
Non richiede .NET. I dati restano in %LOCALAPPDATA%\SeedFlow\State:
non occorre reimportare i torrent. Non avviare due versioni insieme.

## Correzioni

Codifica esplicita per risorse e controlli Unicode; testo di dialogo non piu'
troncato; migrazione dei caratteri supplementari JSON; rilascio bitmap menu;
controlli overflow e appunti. Vedi CHANGELOG.txt e VERIFICA.txt.
I limiti del collaudo della build attuale sono descritti in VERIFICA.txt.

Sono conservati elenco virtuale, filtri, dettagli, copia magnet, tracker,
priorita' file, ratio, code, statistiche, fast resume, tray e diagnostica.

## Compilazione

Visual Studio 2026 o 2022 con MSVC x64, Windows SDK, CMake e Git.
Con VS 2026 serve CMake >= 4.2. Nessun SDK .NET richiesto.

    build.bat -NoPause -CheckOnly
    build.bat -NoPause
    build.bat -NoPause -RunTests
    build.bat -NoPause -SharedLibraries -RunTests

Default: vcpkg x64-windows-static e runtime /MT, EXE statico.
SharedLibraries: x64-windows e /MD, DLL e CRT copiati nel pacchetto, come nella
precedente pacchetto dinamico r11. Shared e' disponibile per Release; per Debug usa
DebugBuild nella modalita' statica o configura CMake manualmente.
VCPKG_ROOT viene letto prima di VsDevCmd; fallback in .build-tools/vcpkg.
Output: SeedFlow-Pronto/data-ora. Log: COMPILAZIONE.log.

CMake manuale con dipendenze gia' presenti:

    cmake -S . -B build-win32 -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static -DSF_BUILD_TESTS=ON
    cmake --build build-win32 --config Release
    ctest --test-dir build-win32 -C Release --output-on-failure

Per shared: triplet x64-windows e SF_STATIC_RUNTIME=OFF, con DLL accessibili.
Le risorse vengono compilate con /c65001 in MSVC e --codepage=65001 in windres.

## Icone

Le icone sono definite in res/icons/icons.py (SVG in res/icons/svg). Dopo una
modifica: `python tools/make_icons.py` (richiede Playwright con Chromium)
rigenera le strisce PNG e src/icon_ids.hpp. Non serve per compilare.

## Versione e integrita'

src/version.h contiene versione e revisione condivise da C++ e risorse.
CMake e manifest corrispondono a 0.8.4 r14.
SHA256SUMS.txt elenca tutti i file, escluso il manifesto stesso.
L'hash dello ZIP e' nel file .sha256 separato.
