<div align="center">

<img src="docs/seedflow-banner.svg" alt="SeedFlow" width="100%">

**Il client BitTorrent nativo per Windows.**

Scritto in C++ / Win32 · Motore libtorrent · Interfaccia italiana

[![Download Windows x64](https://img.shields.io/badge/Scarica_per_Windows_x64-0.8.4_r14-258b45?style=for-the-badge&logo=windows)](https://github.com/Kyo89-sudo/SeedFlow/releases/tag/v0.8.4-r14)
[![Codice C++17](https://img.shields.io/badge/C%2B%2B-17-1565c0?style=for-the-badge&logo=cplusplus)](BUILDING.md)

[Download](https://github.com/Kyo89-sudo/SeedFlow/releases/tag/v0.8.4-r14) · [Compilazione](BUILDING.md) · [Changelog](CHANGELOG.txt) · [Segnala un problema](https://github.com/Kyo89-sudo/SeedFlow/issues)

</div>

---

SeedFlow è un client BitTorrent per **Windows 10/11 x64**: torrent, magnet, statistiche e gestione della coda in un’applicazione desktop nativa.

## Download — 0.8.4 r14

[Scarica la release](https://github.com/Kyo89-sudo/SeedFlow/releases/tag/v0.8.4-r14). Estrai lo ZIP `win64-static` e avvia `SeedFlow.exe`.
Il programma include staticamente libtorrent, OpenSSL e il runtime MSVC: nessuna DLL aggiuntiva o .NET richiesti. Conserva le licenze incluse nel pacchetto.

Chiudi prima eventuali altre istanze, anche nella tray. Lo stato viene conservato in `%LOCALAPPDATA%\SeedFlow\State`.

## Tutto sotto controllo

| Trasferimenti | Informazioni | Desktop |
| :--- | :--- | :--- |
| Torrent e magnet | Grafico download / upload | Applicazione Win32 nativa |
| Priorità dei file e code | Ratio, peer e tracker | Riduzione nella tray |
| Ripresa dei trasferimenti | Diagnostica della porta | Un solo EXE, nessun .NET |

<details>
<summary>Esplora le funzioni</summary>

- Torrent e magnet, copia magnet, priorità dei file e gestione delle code.
- Tracker, peer, ratio, statistiche e fast resume.
- Riduzione nella tray, avvio con Windows e preferenze integrate.
- Grafico delle velocità complessive di download e upload degli ultimi cinque minuti.
- Diagnostica della porta e impostazioni di connessione.

</details>

## Schermate

### Finestra principale

![Finestra principale di SeedFlow](docs/main-window.png)

*Vista senza elementi; barra di stato esclusa dall'immagine.*

### Preferenze

<img src="docs/preferences.png" alt="Preferenze di SeedFlow" width="700">

## Compilazione e verifiche

Visual Studio 2026 o 2022 con MSVC x64, Windows SDK, CMake e Git. Con VS 2026 è richiesto CMake 4.2 o successivo.

```bat
build.bat -NoPause -CheckOnly
build.bat -NoPause -RunTests
```

La configurazione predefinita usa vcpkg `x64-windows-static` e runtime `/MT`.
Consulta [BUILDING.md](BUILDING.md), [ricetta delle dipendenze statiche](BUILD-STATIC-RECIPE.txt) e [CHANGELOG](CHANGELOG.txt).

La r14 è stata compilata e collegata realmente su Windows x64 con MSVC di Visual Studio 2026. CTest: 2/2 test superati; regressione: 94 controlli. La compilazione verificata ha utilizzato direttamente cl/link/rc, con configurazione e registrazione dei test tramite CMake.
Non è stato eseguito un collaudo interattivo completo della GUI o un test di trasferimento verso tracker pubblici. Dettagli in [VERIFICA.txt](VERIFICA.txt).

## Licenze

Le licenze delle dipendenze sono nella cartella [licenses](licenses). Per il codice originale di SeedFlow non è ancora stata scelta una licenza di riutilizzo: la pubblicazione dei sorgenti non concede automaticamente una licenza open source.

## Segnalazioni

Apri una issue indicando versione, versione di Windows, passaggi per riprodurre il problema ed eventuale messaggio di errore. Rimuovi dati personali e tracker privati dai log prima di allegarli.
