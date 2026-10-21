<div align="center">

<img src="docs/seedflow-banner.svg" alt="SeedFlow — client BitTorrent per Windows" width="100%">

Torrent e magnet in un'applicazione desktop nativa, con interfaccia in italiano.

[![Scarica SeedFlow](https://img.shields.io/badge/Scarica_per_Windows_x64-0.8.4_r14-258b45?style=for-the-badge&logo=windows)](https://github.com/Kyo89-sudo/SeedFlow/releases/tag/v0.8.4-r14)
[![GPL v3](https://img.shields.io/badge/Licenza-GPL_v3-1565c0?style=for-the-badge)](LICENSE)

[Download](https://github.com/Kyo89-sudo/SeedFlow/releases) · [Changelog](CHANGELOG.txt) · [Compilazione](BUILDING.md) · [Segnalazioni](https://github.com/Kyo89-sudo/SeedFlow/issues)

</div>

## Prova SeedFlow

SeedFlow funziona su **Windows 10 e 11, a 64 bit**. Scarica lo ZIP `win64-static` dalla [pagina delle release](https://github.com/Kyo89-sudo/SeedFlow/releases), estrailo e apri `SeedFlow.exe`.

Non serve .NET e non ci sono DLL aggiuntive da installare. Le librerie sono incluse nell'eseguibile; documenti e licenze accompagnano il pacchetto.

Prima di sostituire una versione, chiudila con **File → Esci**, anche se è nella tray. I torrent e le impostazioni restano in `%LOCALAPPDATA%\SeedFlow\State`.

**La versione pubblicata è la 0.8.4 r14.** La revisione di manutenzione **r15** è in prova nel [ramo dedicato](https://github.com/Kyo89-sudo/SeedFlow/pull/1). Le modifiche sono descritte nel [changelog](CHANGELOG.txt).

## Cosa puoi fare

- Aggiungere torrent e magnet, scegliere i file da scaricare e gestire le priorità della coda.
- Controllare tracker, peer, ratio e quantità di dati trasferiti.
- Seguire download e upload dal grafico delle velocità complessive.
- Tenere il programma nella tray e riprendere i trasferimenti alla riapertura.
- Regolare connessioni, limiti di velocità e impostazioni di rete dalle Preferenze.

## Schermata del programma

<p align="center">
<a href="docs/seedflow-0.8.4-main-window.png"><img src="docs/seedflow-preview.svg" alt="La finestra principale di SeedFlow 0.8.4" width="640"></a>
</p>

*Apri l'immagine per vederla nelle dimensioni originali.*

## Compilare il programma

Il progetto è scritto in C++17 / Win32 e usa libtorrent. Per compilarlo servono Visual Studio 2026 o 2022, MSVC x64, Windows SDK, CMake e Git. Con VS 2026 usa CMake 4.2 o successivo.

```bat
build.bat -NoPause -RunTests
```

Lo script prepara le dipendenze, compila ed esegue i test. La prima volta può richiedere tempo. Le istruzioni complete sono in [BUILDING.md](BUILDING.md).

La r15 è stata compilata su Windows con MSVC e ha superato 124 controlli di regressione e il test isolato del motore. Il [rapporto della r15](https://github.com/Kyo89-sudo/SeedFlow/blob/maintenance/0.8.4-r15/VERIFICA.txt) spiega come sono state eseguite le prove e cosa resta da collaudare. Per la versione che stai usando, consulta anche il rapporto incluso nel suo ZIP.

## Licenza

SeedFlow è distribuito sotto **GNU GPL versione 3** (`GPL-3.0-only`). Il testo completo è in [LICENSE](LICENSE); le librerie mantengono le [proprie licenze](licenses/README.md).

## Segnalare un problema

Apri una [issue](https://github.com/Kyo89-sudo/SeedFlow/issues) con la versione di SeedFlow, quella di Windows e i passaggi per riprodurre il problema. Se alleghi un log, rimuovi prima dati personali, indirizzi IP e tracker privati.
