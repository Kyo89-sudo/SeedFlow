<div align="center">

<img src="docs/seedflow-banner.svg" alt="SeedFlow — client BitTorrent per Windows" width="100%">

**Client BitTorrent per Windows, scritto in C++ / Win32 e basato su libtorrent.**

[![Download Windows x64](https://img.shields.io/badge/Download-Windows_x64-258b45?style=for-the-badge&logo=windows)](https://github.com/Kyo89-sudo/SeedFlow/releases)
[![GPL v3](https://img.shields.io/badge/License-GPL_v3-1565c0?style=for-the-badge)](LICENSE)

[Release](https://github.com/Kyo89-sudo/SeedFlow/releases) · [Changelog](CHANGELOG.txt) · [Build](BUILDING.md) · [Issue tracker](https://github.com/Kyo89-sudo/SeedFlow/issues)

</div>

## SeedFlow

SeedFlow gestisce torrent e magnet con una GUI desktop nativa. Include priorità dei file, queue management, fast resume, statistiche e diagnostica di rete. La build statica contiene le librerie e il runtime MSVC: **un solo EXE, senza DLL aggiuntive o .NET**.

La release pubblicata è **0.8.4 r14**. La **r15** è una revisione di manutenzione, disponibile nel [ramo dedicato](https://github.com/Kyo89-sudo/SeedFlow/pull/1).

## Funzionalità

| Area | Supporto |
| :--- | :--- |
| Torrent e magnet | File .torrent e magnet link, copia magnet, selezione e priorità dei file |
| Queue e bandwidth | Gestione della coda, limiti download/upload globali e per torrent, connessioni e upload slot |
| Seeding | Ratio target e azioni automatiche al raggiungimento del limite |
| Super-seeding | Modalità dedicata alla distribuzione iniziale di torrent completi |
| NAT traversal | Port mapping automatico tramite UPnP e NAT-PMP su router compatibili |
| Connessioni peer | IPv4/IPv6 e protocol encryption configurabile: consentita, preferita o obbligatoria |
| Peer discovery | Tracker HTTP/UDP, DHT, PeX e Local Peer Discovery |
| Fast resume | Salvataggio e ripristino dello stato dei torrent, con verifica forzata dei file disponibile |
| Monitoraggio | Peer e tracker, health dello swarm, dati trasferiti, ratio e grafico download/upload |
| Desktop e diagnostica | System tray, avvio con Windows, controllo della porta e diagnostica di rete |

## Screenshot

<p align="center">
<a href="docs/seedflow-0.8.4-main-window.png"><img src="docs/seedflow-preview.svg" alt="SeedFlow 0.8.4 — main window" width="640"></a>
</p>

*Click sull'immagine per aprire lo screenshot originale.*

## Download e avvio

Richiede **Windows 10/11 x64**. Scarica lo ZIP `win64-static` dalla [pagina delle release](https://github.com/Kyo89-sudo/SeedFlow/releases), estrailo e avvia `SeedFlow.exe`. Conserva le licenze incluse nel pacchetto.

Prima di aggiornare, chiudi la versione precedente con **File → Esci**, anche se è nella tray. Stato e impostazioni sono in `%LOCALAPPDATA%\SeedFlow\State`.

## Build

Servono Visual Studio 2026 o 2022, MSVC x64, Windows SDK, CMake e Git. Con VS 2026 è richiesto CMake 4.2 o successivo.

```bat
build.bat -NoPause -RunTests
```

Il default è `x64-windows-static` con runtime `/MT`. La prima build compila anche le dipendenze. Opzioni e istruzioni CMake: [BUILDING.md](BUILDING.md).

La r15 ha superato la compilazione/link nativi su Windows, 124 controlli di regressione e il test isolato del motore. Modalità di verifica e limiti sono nel [report della r15](https://github.com/Kyo89-sudo/SeedFlow/blob/maintenance/0.8.4-r15/VERIFICA.txt). Queste prove non equivalgono a un collaudo end-to-end su uno swarm pubblico.

## License

**GNU GPL v3** (`GPL-3.0-only`): [LICENSE](LICENSE). I componenti di terze parti mantengono le [rispettive licenze](licenses/README.md).

## Bug report

Apri una [issue](https://github.com/Kyo89-sudo/SeedFlow/issues) indicando versione di SeedFlow, versione di Windows, passaggi per riprodurre il problema e risultato atteso. Prima di allegare log o screenshot, rimuovi dati personali, indirizzi IP e tracker privati.
