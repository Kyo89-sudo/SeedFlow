# Compilazione SeedFlow 0.8.8 (C++20)

Visual Studio 2026 o 2022 con MSVC x64, Windows SDK, CMake e Git. Con VS 2026 serve CMake >= 4.2. Non serve .NET per eseguire il programma.

```bat
build.bat -NoPause -CheckOnly
build.bat -NoPause -RunTests
```

La prima compilazione scarica e costruisce le dipendenze tramite vcpkg e può richiedere tempo. Per impostazione predefinita usa `x64-windows-static` e runtime MSVC `/MT`. Il risultato è un EXE senza DLL aggiuntive da distribuire.

Output: `SeedFlow-Pronto/data-ora`; log: `COMPILAZIONE.log`. Il pacchetto include la licenza GPL e gli avvisi delle dipendenze. Non avviare due versioni di SeedFlow insieme.

## Opzioni

- `-SharedLibraries`: triplet `x64-windows` e runtime `/MD`, con DLL copiate nel pacchetto Release.
- `-DebugBuild`: build Debug statica.
- `-RunTests`: regressione Win32 e test isolato dei flag del motore, senza tracker pubblici o trasferimenti esterni.

`VCPKG_ROOT` viene letto prima di VsDevCmd; in assenza di un checkout valido, viene usata `.build-tools/vcpkg`.

## CMake manuale

```bat
cmake -S . -B build-win32 -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static -DSF_BUILD_TESTS=ON -DSF_BUILD_ENGINE_TESTS=ON
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
```

## Risorse e verifiche

I sorgenti e le risorse sono UTF-8; MSVC usa `/utf-8`, rc `/c65001`. Le icone sono già incluse: non occorre rigenerarle per compilare. Per modificarle: `python tools/make_icons.py` con Playwright e Chromium disponibili.

Il progetto richiede C++20. La versione condivisa è in `src/version.h`, FileVersion `0.8.8.0`. Consulta [VERIFICA.txt](VERIFICA.txt) per le verifiche effettivamente eseguite e [BUILD-STATIC-RECIPE.txt](BUILD-STATIC-RECIPE.txt) per le dipendenze della build consegnata.
