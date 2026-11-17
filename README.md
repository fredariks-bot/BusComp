# BUS COMP – Professional VCA Bus Compressor (VST3)

BUS COMP ist ein professioneller, transparenter VCA-artiger Bus-Kompressor als
VST3-Plugin für **Windows x64** (C++17, JUCE 8, CMake). Gedacht für Drum-Busse,
Instrumentengruppen und den Mix-/Masterbus: kontrollierte Dynamikreduktion,
stabile Stereomitte, musikalische Zeitkonstanten, keine künstliche Sättigung.

## Funktionsumfang

- **VCA-Kompressor-DSP** mit Hard-Knee- und optionaler 6-dB-Soft-Knee-Kennlinie
  (`Output dB = Threshold + (Input dB − Threshold) / Ratio` oberhalb des Thresholds)
- **Threshold** −30 … 0 dB, **Ratio** kontinuierlich 1:1 … 20:1 (enthält 2:1, 4:1, 10:1)
- **Attack** 0,1 … 30 ms (u. a. 0,1 / 0,3 / 1 / 3 / 10 / 30 ms), **Release** 0,1 … 1,2 s
  (u. a. 100 / 200 / 400 / 600 / 800 / 1200 ms) als fester, automatisierbarer Parameter
- **Auto Release**: tatsächlich signalabhängige, programmadaptive Release-Regelung
  (siehe unten), zusätzlich feste Release-Zeiten
- **Stereo Link** 0 … 100 %: bei 100 % gemeinsame Detektorregelung über das
  Peak-Maximum beider Kanäle, bei 0 % unabhängige Kanäle, dazwischen linear geblendet
- **Sidechain-Hochpass** 20 … 300 Hz + Bypass, nur im Detektorpfad
  (4th-order, 24 dB/Oktave); das Audiosignal bleibt unberührt
- **Makeup Gain** 0 … +12 dB und zuschaltbarer **Auto Gain** (gemessener Pegelausgleich)
- **Parallelkompression** (Mix 0 … 100 %, linear; 0 % = unbearbeitetes Signal,
  100 % = voller Wet-Anteil)
- **Oversampling** OFF / 2× / 4× (JUCE `dsp::Oversampling`, Polyphase-IIR-Halbbandfilter,
  integer Latency; Latenz wird an den Host gemeldet)
- **Gain-Reduction-Meter** (0 … −20 dB, geglättet, Peak-Hold mit Reset, numerische Anzeige)
  plus **Input-/Output-Peak- und RMS-Meter** mit Peak-Hold und Clip-Anzeige
- **Vollständige VST3-Parameterautomatisierung** über `AudioProcessorValueTreeState`
- **Presets**: Gentle Glue, Drum Bus, Mix Bus, Fast Punch, Heavy Compression,
  Transparent Master Bus (sinnvolle Ausgangspunkte, nicht „objektiv optimal")
- **Zustandsspeicherung** über `getStateInformation`/`setStateInformation`
  (XML-basiert, robust gegen alte/korrupte States)
- **A/B-Vergleich** im Header der GUI
- **Automatisierte Tests** (DSP, Parameter, Preset/Zustand, Processor) und
  **GitHub-Actions-CI** für Windows x64

## Projektstruktur

```
CMakeLists.txt                  CMake-Konfiguration, JUCE 8.0.10 via FetchContent (GIT_TAG gepinnt)
Source/ParameterDefs.h/.cpp     zentrale Parameterdefinitionen (IDs, Bereiche, Defaults)
Source/CompressorDSP.h/.cpp     komplette Kompressor-Engine (Detektor, Kennlinie, Envelope,
                                Auto Release, Auto Gain, Oversampling, Dry-Delay, Mix)
Source/SidechainFilter.h/.cpp   4th-order-HP-Filter für den Sidechain-Pfad
Source/GainReductionMeter.*     GR-Meter (geglättet + Peak-Hold, atomics)
Source/LevelMeter.*             In/Out-Peak/RMS-Meter mit Hold und Clip (atomics)
Source/PresetManager.h/.cpp     Factory-Presets + State-(De)Serialisierung
Source/PluginProcessor.h/.cpp   AudioProcessor, APVTS, State, A/B, Latenz
Source/PluginEditor.h/.cpp      eigene JUCE-GUI mit echten Metern
Tests/TestMain.cpp              Konsolen-Testrunner (juce::UnitTestRunner)
Tests/CompressorTests.cpp       DSP-Tests (Kennlinie, Attack/Release, Auto Release, Stereo, …)
Tests/ParameterTests.cpp        Parameter- und Preset-/State-Tests
Tests/ProcessorStateTests.cpp   Processor-Tests (Automation, State-Roundtrip, Latenz, A/B)
.github/workflows/build.yml     CI: Windows x64, VS2022, CMake Release, Tests, VST3-Artefakt
README.md
LICENSE                         AGPL-3.0 (siehe Lizenzhinweise)
```

## Build-Voraussetzungen

- Windows 11 x64, **Visual Studio 2022 Build Tools** (MSVC, „Desktopentwicklung mit C++“)
- **CMake ≥ 3.22**
- Git (für den reproduzierbaren JUCE-Fetch; JUCE wird über `FetchContent` mit
  `GIT_TAG 8.0.10` bezogen – https://github.com/juce-framework/JUCE/releases/tag/8.0.10)

## Lokaler Build (Windows)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure -C Release   # DSP-/Parameter-Tests
```

Das VST3-Bundle liegt danach unter:

```
build\BusComp_artefacts\Release\VST3\BUS COMP.vst3
```

## GitHub Actions CI

`.github/workflows/build.yml` läuft bei Push und Pull Requests auf `windows-latest`
(x64, VS 2022 preinstalliert), konfiguriert CMake (VS 2022, x64), baut Release,
führt die Tests via `ctest` aus, prüft das VST3-Bundle und lädt das Artefakt
**`BUS_COMP_VST3_Windows_x64`** (ZIP des `.vst3`-Bundles) hoch.

Artefakt herunterladen: GitHub → Actions → Workflow-Run → „Artifacts“ →
`BUS_COMP_VST3_Windows_x64` → entpacken.

## Installation des VST3

1. ZIP entpacken, sodass `BUS COMP.vst3` als Ordner vorliegt.
2. Nach `C:\Program Files\Common Files\VST3` kopieren (oder einen Ordner, den die
   DAW überwacht, z. B. `%USERPROFILE%\Documents\VST3` bei manchen Hosts;
   `Common Files\VST3` wird von den meisten DAWs automatisch gescannt).
3. In der DAW einen erneuten Plugin-Scan auslösen (z. B. in REAPER:
   *Options → Preferences → Plug-ins → VST → Re-scan*; in Ableton:
   *Preferences → Plug-ins → Rescan*; in Studio One: *Studio One → Options → Locations →
   VST Plug-ins → Scan*; in Bitwig: *Settings → Locations → Rescan*). Ggf. persönliche
   VST3-Ordner in den DAW-Einstellungen ergänzen.

## Presets

Im Header der GUI über das Preset-Menü wählbar; zusätzlich über die
Program-Schnittstelle des Hosts (VST3-Programme = die sechs Factory-Presets).
Ändert man einen Parameter, wird der Preset-Name auf „(custom)“ gesetzt. Der Zustand
(alle Parameter + Preset-Name) wird mit dem DAW-Projekt gespeichert.

## Parameterbeschreibung

| Parameter | Bereich | Default | Bedeutung |
|---|---|---|---|
| THRESHOLD | −30 … 0 dB | −18 dB | Einsatzpunkt der Kompression |
| RATIO | 1:1 … 20:1 | 4:1 | Stärke der Reduktion oberhalb des Thresholds |
| ATTACK | 0,1 … 30 ms | 10 ms | Anstiegszeit der Gain Reduction |
| RELEASE | 0,1 … 1,2 s | 0,4 s | Abfallzeit (fest), wenn Auto Release aus |
| AUTO RELEASE | Off/On | Off | signalabhängige Release-Regelung |
| MAKEUP | 0 … +12 dB | 0 dB | manuelle Pegelkompensation |
| AUTO GAIN | Off/On | Off | automatische Pegelkompensation (gemessen) |
| MIX | 0 … 100 % | 100 % | Parallelkompression (Dry/Wet) |
| STEREO LINK | 0 … 100 % | 100 % | Kopplung der Detektoren L/R |
| SC HPF | Off, 20 … 300 Hz | Off | Hochpass nur im Sidechain-Detektor |
| OVERSAMPLING | Off / 2× / 4× | Off | optionale Qualitäts-OS |
| SOFT KNEE | Off/On | Off | 6-dB-Soft-Knee |

Alle Parameter sind VST3-automatisierbar (APVTS).

### Auto Release (programmadaptiv)

Die Release-Zeit wird pro Sample aus dem **zeitlichen Verlauf des Detektorpegels**
abgeleitet: Ein interner „Hold“-Timer misst, wie lange das Signal zuletzt nahe/oberhalb
(Threshold − 6 dB) lag. Lange anhaltende Pegel (Hold ≥ 250 ms) ergeben eine langsame
Erholung (bis 0,9 s), kurze Transienten eine schnelle (ab 60 ms); dazwischen linear.
Die Zeitkonstante wird zusätzlich mit 50 ms geglättet, daher keine Sprünge/Pumps.
Kurzes Beispiel: nach einem 50-ms-Impuls ist die Reduktion nach < 0,9 s wieder unter
1 dB; nach 1,5 s Dauerton erst nach > 0,9 s – die Regelung ist also nachweislich
signalabhängig und nicht konstant. Feste Release-Zeiten bleiben unabhängig davon
verfügbar (AUTO RELEASE = Off).

### Auto Gain

Misst den geglätteten RMS-Pegel von Eingang (dry) und komprimiertem Signal (wet, vor
Makeup) mit 300-ms-Fenstern und regelt die Differenz (geclampt auf 0 … +12 dB) mit
1 s Zeitkonstante als zusätzliches Makeup nach – unabhängig vom Threshold, da die
tatsächliche Pegeländerung gemessen wird. Unter −60 dBFS Eingangspegel wird keine
Verstärkung appliziert (Stille/leise Signale bleiben unangetastet). Der manuelle
MAKEUP-Regler addiert sich unabhängig davon. Es wird keine perfekte Lautheitsgleichheit
für jedes Signal behauptet.

### Stereo-Kopplung

Pro Kanal wird die eigene GR aus dem eigenen Detektorpegel berechnet; zusätzlich die
gemeinsame GR aus `max(|L|, |R|)` (Peak-Kombination). Die angewendete GR ist
`link × GR_max + (1 − link) × GR_own`. Bei 100 % ist die GR beider Kanäle identisch
(stabile Mitte), bei 0 % reagieren die Kanäle unabhängig.

### Latenz

OFF: 0 Samples. 2×/4×: die integer Latenz der JUCE-Oversampling-Filter
(wird via `setLatencySamples` an den Host gemeldet). Der Dry-Pfad der
Parallelmischung wird intern um genau diese Latenz verzögert, damit Dry/Wet
phasenkohärent mischen. Umschalten ist allokationsfrei (Filter-Reset im Audio-Thread,
kein Neuaufbau).

## Echtzeit-Sicherheit

Im Audio-Callback: keine Heap-Allokationen, keine Locks, keine Datei-/Netzwerkzugriffe,
keine Strings, keine GUI-Aufrufe. Alle Filter/Puffer werden in `prepareToPlay` alloziert.
Parameter werden pro Block gelesen und mit 20-ms-One-Pole geglättet (kein Zipper).
Meter kommunizieren über `std::atomic` (lock-frei), die GUI liest sie per Timer (30 Hz).
Sample-Rate-Wechsel (44,1 … 192 kHz), Blockgrößenwechsel und Mono/Stereo werden
unterstützt (getestet).

## Tests

`BusCompTests` (Konsolen-App, via `ctest`) enthält u. a.:

- Kennlinien-Mathe (unter/am/über Threshold; Ratios 2/4/10 numerisch; Soft-Knee-Kontinuität)
- Attack-/Release-Zeiten (t90/t10 gegen die theoretische One-Pole-Zeit)
- Auto Release: Nachweis der Signalabhängigkeit (kurzer vs. langer Impuls)
- Stereo Link 0/50/100 %, identische/unterschiedliche Kanäle, Mono
- Sidechain-HPF: Frequenzgang (−6 dB bei fc, 24 dB/Oktave) und Trennung Detektor/Audio
- Mix 0/50/100 %, Makeup, Auto Gain (inkl. Stille), Oversampling OFF/2×/4× inkl. Latenz
  und Umschalten, Sample-Rates 44,1–192 kHz, Stille, Rauschen, NaN-Checks,
  Parameter-Rampen während der Verarbeitung
- Parameter-Layout (IDs, Bereiche, Defaults), Presets, State-Roundtrip, korrupte States
- Processor: Automation (`setValueNotifyingHost`), State-Roundtrip, A/B, Latenz-Meldung

Toleranzen und bekannte Einschränkungen sind in den Testquellen dokumentiert
(z. B. GR-Messung am Signal-Peak, One-Pole-Näherung t90 ≈ 2,303·τ).

## Lizenz

Dieses Projekt steht unter **AGPL-3.0-or-later** (siehe `LICENSE`), passend zur
JUCE-8-Lizenzierung (JUCE 8 ist dual lizenziert unter
[AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html) und der kommerziellen
[JUCE-Lizenz](https://juce.com/legal/juce-8-licence/)). Wer BUS COMP ohne AGPL
nutzen möchte, benötigt eine kommerzielle JUCE-Lizenz.

## Bekannte Einschränkungen

- Maximal 2 Kanäle (Mono/Stereo) auf dem Main-Bus – für Bus-Kompression ausgelegt.
- Auto Gain mittelt über 1 s; bei sehr dynamischem Material ist die Kompensation
  bewusst träge (keine Sprünge).
- Oversampling erhöht die Latenz (wird gemeldet und im Dry-Pfad kompensiert).
- Die Presets sind Ausgangspunkte, nicht universelle „beste“ Einstellungen.
