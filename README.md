<div align="center">

<img src="docs/seedflow-banner.svg?v=2" alt="SeedFlow — client BitTorrent nativo per Windows" width="100%">

<br>

**Download, seeding e creazione torrent in un'unica finestra Windows nativa.**<br>
Basato su libtorrent 2.1 e C++20, distribuito come un solo eseguibile statico.

<br>

[![Scarica per Windows](https://img.shields.io/badge/Scarica-Windows_x64-2fa84a?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/Kyo89-sudo/SeedFlow/releases/latest)
[![Release](https://img.shields.io/github/v/release/Kyo89-sudo/SeedFlow?style=for-the-badge&label=Release&color=2f80c9)](https://github.com/Kyo89-sudo/SeedFlow/releases/latest)
[![Licenza GPL v3](https://img.shields.io/badge/Licenza-GPL_v3-0f3140?style=for-the-badge)](LICENSE)

[Funzionalità](#funzionalità) · [Installazione](#installazione) · [Compilazione](#compilazione) · [Changelog](CHANGELOG.txt) · [Segnalazioni](https://github.com/Kyo89-sudo/SeedFlow/issues)

</div>

---

## Perché SeedFlow

| | |
|---|---|
| 🪶 **Nativo e leggero** | Interfaccia Win32 scritta in C++: niente .NET, Electron o runtime da installare. |
| 📦 **Un solo EXE** | Build statica con runtime `/MT`: nessuna DLL esterna da distribuire. |
| ⚙️ **Motore collaudato** | libtorrent 2.1 con DHT, PeX, IPv4/IPv6, UPnP e NAT-PMP. |
| 🔒 **Locale e senza pubblicità** | Nessun servizio esterno obbligatorio; anche la geolocalizzazione dei peer avviene sul PC. |

## Funzionalità

**Download e seeding**
- Apre file `.torrent` e link magnet, anche mentre SeedFlow è già in esecuzione.
- Scelta della cartella e priorità per singolo file.
- Code, limiti di banda, obiettivi di ratio e super-seeding.

**Creazione torrent**
- Metadati v1, v2 o ibridi v1 + v2 da file e cartelle.
- Tracker, web seed e commento.

**Rete e peer**
- Schede dedicate per peer e tracker, con diagnostica di rete e verifica della porta.
- Cifratura delle connessioni configurabile.
- Bandierine dei peer opzionali da database GeoLite2 Country locale, caricato in background.

**Monitoraggio**
- Velocità, statistiche e indicatore di salute per ogni torrent.
- Grafici di upload e download, globali o per singolo torrent.

**Comodità**
- Tutte le impostazioni in un'unica finestra Preferenze.
- Riduzione nell'area di notifica e avvio automatico con Windows.

## Uno sguardo

<p align="center">
<a href="docs/seedflow-main-window.png"><img src="docs/seedflow-main-window.png?v=2" alt="Finestra principale di SeedFlow" width="900"></a>
</p>

## Installazione

Requisiti: **Windows 10 o 11 a 64 bit**. Scarica l'ultima versione dalla [pagina delle release](https://github.com/Kyo89-sudo/SeedFlow/releases/latest).

| Pacchetto | Quando usarlo |
|---|---|
| `SeedFlow-<versione>-win64-setup.exe` | Installazione guidata per l'utente corrente, con associazione di torrent e magnet, collegamento desktop e disinstallazione da Impostazioni › App. |
| `SeedFlow-<versione>-win64-static.zip` | Versione portabile: estrai lo ZIP e avvia `SeedFlow.exe`. |

- **Installer:** installa in `%LOCALAPPDATA%\Programs\SeedFlow`. Se un'altra app gestisce già torrent o magnet, al termine si aprono le impostazioni di Windows per scegliere SeedFlow. La disinstallazione conserva download e impostazioni.
- **Aggiornamento portabile:** chiudi SeedFlow e sostituisci l'eseguibile. Torrent e impostazioni restano in `%LOCALAPPDATA%\SeedFlow\State`.
- **Verifica:** gli hash SHA-256 di tutti i pacchetti sono in `SHA256SUMS.txt`, tra gli allegati della release.

<details>
<summary><b>Attivare le bandierine dei peer</b></summary>

<br>

1. Scarica da MaxMind il database **GeoLite2 Country** in formato CSV, rispettando le condizioni del fornitore.
2. In **Preferenze › Generale** abilita le bandierine e indica la cartella che contiene:
   - `GeoLite2-Country-Locations-en.csv`
   - `GeoLite2-Country-Blocks-IPv4.csv`
   - `GeoLite2-Country-Blocks-IPv6.csv`

La ricerca avviene interamente sul PC. Disattivando l'opzione, SeedFlow libera indice e cache e annulla un eventuale caricamento in corso.

</details>

## Compilazione

Servono Visual Studio 2026 o 2022 con MSVC x64 e Windows SDK, CMake (4.2 o successivo con VS 2026) e Git.

```bat
build.bat -NoPause -RunTests
```

Il risultato predefinito è un eseguibile statico x64. La prima compilazione prepara le dipendenze tramite vcpkg e può richiedere tempo. Opzioni, CMake manuale e creazione dell'installer sono descritti in [BUILDING.md](BUILDING.md).

La 0.8.9.5 è stata compilata con Visual Studio 2026 e ha superato i test CTest di motore, GUI e installer: [VERIFICA.txt](VERIFICA.txt) riporta copertura e controlli manuali.

## Segnalazioni

Apri una [issue](https://github.com/Kyo89-sudo/SeedFlow/issues) indicando versione di SeedFlow e di Windows, passaggi per riprodurre il problema e risultato atteso. Prima di allegare log o screenshot, rimuovi dati personali, indirizzi IP e tracker privati.

## Licenza

SeedFlow è distribuito secondo la **GNU GPL v3** (`GPL-3.0-only`): [testo della licenza](LICENSE). Le dipendenze mantengono le [proprie licenze](licenses/README.md).
