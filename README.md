<div align="center">

<img src="docs/seedflow-banner.svg" alt="SeedFlow — client BitTorrent per Windows" width="100%">

### Download, seeding e creazione torrent in una finestra Windows nativa.

SeedFlow è un client BitTorrent per Windows basato su libtorrent 2.1 e C++20. L’interfaccia Win32 raccoglie in un unico posto torrent, magnet, peer, tracker e statistiche.

[![Scarica per Windows](https://img.shields.io/badge/Windows-x64-258b45?style=for-the-badge&logo=windows)](https://github.com/Kyo89-sudo/SeedFlow/releases/latest)
[![Release](https://img.shields.io/github/v/release/Kyo89-sudo/SeedFlow?style=for-the-badge&label=Release&color=2b4a6f)](https://github.com/Kyo89-sudo/SeedFlow/releases/latest)
[![GPL v3](https://img.shields.io/badge/Licenza-GPL_v3-1565c0?style=for-the-badge)](LICENSE)

[Funzionalità](#cosa-puoi-fare) · [Installazione](#installazione) · [Compilazione](#compilazione) · [Changelog](CHANGELOG.txt) · [Segnalazioni](https://github.com/Kyo89-sudo/SeedFlow/issues)

</div>

SeedFlow viene distribuito come **un solo eseguibile statico**: non servono DLL esterne né runtime .NET o Visual C++ da installare.

## Cosa puoi fare

- **Scaricare e condividere:** apri file `.torrent` o magnet, scegli la cartella, imposta priorità ai file e segui download e seeding.
- **Creare torrent:** genera metadati v1, v2 o ibridi da file e cartelle; aggiungi tracker, web seed e commenti.
- **Gestire la rete:** controlla peer e tracker, usa DHT, PeX, IPv4/IPv6, UPnP e NAT-PMP.
- **Riconoscere i peer:** bandierine opzionali con database GeoLite2 Country CSV locale e caricamento in background.
- **Tenere sotto controllo l’attività:** consulta velocità, statistiche, salute dei torrent e grafici globali o per torrent.
- **Gestire il seeding:** imposta obiettivi di ratio, usa il super-seeding e organizza i torrent con code e limiti di banda.

## Una schermata

<p align="center">
<a href="docs/seedflow-main-window.png"><img src="docs/seedflow-main-window.png" alt="Finestra principale di SeedFlow" width="900"></a>
</p>

## Installazione

Serve Windows 10 o 11 a 64 bit.

1. Scarica l’ultima versione dalla [pagina delle release](https://github.com/Kyo89-sudo/SeedFlow/releases/latest).
2. Estrai `SeedFlow-<versione>-win64-static.zip` e avvia `SeedFlow.exe`.
3. Facoltativo: verifica lo ZIP con il file `.sha256` incluso tra gli allegati della release.

Per aggiornare, chiudi SeedFlow e sostituisci l’eseguibile. Torrent e impostazioni restano in `%LOCALAPPDATA%\SeedFlow\State`.

L'installer `SeedFlow-<versione>-win64-setup.exe` offre una GUI nativa Win32 e installa il programma in `%LOCALAPPDATA%\Programs\SeedFlow`. Puoi registrare SeedFlow per i file torrent e i link magnet e aggiungere il collegamento desktop. Se esiste già un'app predefinita, scegli SeedFlow nelle impostazioni Windows aperte al termine. La disinstallazione è disponibile in Impostazioni > App e conserva download e impostazioni personali.

### Bandierine dei peer

In **Preferenze > Generale**, abilita le bandierine e seleziona la cartella dei CSV GeoLite2 Country forniti da MaxMind. Servono `GeoLite2-Country-Locations-en.csv` e i blocchi `GeoLite2-Country-Blocks-IPv4.csv` e `GeoLite2-Country-Blocks-IPv6.csv`. Il database va scaricato separatamente rispettando le condizioni del fornitore. La ricerca avviene sul PC. Disattivando l'opzione, SeedFlow libera indice e cache e annulla l'eventuale caricamento.

## Compilazione

Sono richiesti Visual Studio 2026 o 2022 con MSVC x64 e Windows SDK, CMake 4.2 o successivo con VS 2026, e Git. Il progetto usa C++20.

```bat
build.bat -NoPause -RunTests
```

La configurazione predefinita produce un eseguibile statico x64 con runtime `/MT`. La prima compilazione prepara anche le dipendenze e può richiedere tempo. Opzioni e configurazione CMake sono descritte in [BUILDING.md](BUILDING.md).

## Verifiche

La 0.8.9.4 è stata compilata con Visual Studio 2026 e ha superato i tre test CTest del motore, della GUI e dell'installer. [VERIFICA.txt](VERIFICA.txt) descrive la copertura e i controlli manuali.

## Licenza

SeedFlow è distribuito secondo **GNU GPL v3** (`GPL-3.0-only`): [testo della licenza](LICENSE). Le dipendenze mantengono le [proprie licenze](licenses/README.md).

## Segnalazioni

Per segnalare un problema, apri una [issue](https://github.com/Kyo89-sudo/SeedFlow/issues) con versione del programma e di Windows, passaggi per riprodurlo e risultato atteso. Prima di allegare log o screenshot, rimuovi dati personali, indirizzi IP e tracker privati.
