# OpenBF1942-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenBF1942-Plus" width="100%"></p>

![Status: early prototype](https://img.shields.io/badge/status-early%20prototype-orange)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-lightgrey)
![Battlefield 1942](https://img.shields.io/badge/Battlefield-1942-8a7a52)

[Español](README.es.md) · [English](README.md) · [Português](README.pt.md) · [Français](README.fr.md) · **Deutsch** · [Русский](README.ru.md)

**Battlefield 1942 auf einer neuen Engine.** OpenBF1942-Plus ist eine Clean-Room-Neuimplementierung von Refractor 1, der Engine von Battlefield 1942, von Grund auf neu geschrieben: Sie liest die Dateien des Spiels aus deiner Installation und führt sie auf modernen Systemen aus. Das Ziel ist, mit und gegen das Originalspiel zu spielen: Originalclients auf unserem Server, unser Client auf Originalservern.

<table>
  <tr>
    <td><img src="docs/images/first_person.jpg" alt="El Alamein in first person with the BAR"></td>
    <td><img src="docs/images/third_person.jpg" alt="A British soldier in third person at El Alamein"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/berlin.jpg" alt="A street in Berlin with tank traps"></td>
  </tr>
</table>
<p align="center"><sub>El Alamein und Berlin, von OpenBF1942-Plus aus den Dateien der Verkaufsversion gezeichnet.</sub></p>

## Was heute funktioniert

- **Jedes Level von Battlefield 1942, Road to Rome und Secret Weapons of WWII lädt und wird gerendert**: Gelände, Gebäude, Bäume, Wasser, Himmel, Nebel und Beleuchtung nach den eigenen Einstellungen jedes Levels.
- **Eine Conquest-Runde gegen Bots** auf jedem Level: Flaggen, Tickets, Kits, Magazine und Nachladen, Granaten, Minen und Sprengladungen, Versorgungsdepots, Hocken und Liegen, Fallschirme, Ego- und Third-Person-Ansicht.
- **Fahrzeuge**: Autos, Panzer, Flugzeuge und Boote, die man steuern kann, mit Plätzen für Schützen und Passagiere; Bots fahren Landfahrzeuge und Landungsboote und bedienen deren Waffen.
- **Sound und HUD** aus den Soundskripten und Schriftarten des Spiels.
- **Verglichen mit dem Originalclient** an denselben Stellen in Berlin und El Alamein: Gebäude, Dunst, Himmel, Fahrzeuge und Sand innerhalb von etwa 15 %.
- **Netzwerkforschung**: eine Originalsitzung zwischen Client und Server aufgezeichnet; das Server-Query-Protokoll dokumentiert.

> [!NOTE]
> **Früher Prototyp.** Nur kleine Teile wurden von einem Menschen ausprobiert, viele Regeln sind Vermutungen und nichts ist kalibriert. Es gibt keine Menüs, noch keinen Mehrspielermodus, und die Straßen Berlins sind noch dunkler als im Original.

> [!IMPORTANT]
> **Es sind keine Spielinhalte enthalten — bring deine eigenen Daten mit.** Dieses Repository enthält nur Quellcode: keine ausführbaren Dateien, Archive, Karten, Modelle, Texturen oder Sounds. Die Engine liest sie aus einer Kopie von Battlefield 1942, die **du rechtmäßig besitzt**, hier als `<BF1942_DIR>` bezeichnet.

## Wie man spielt

Es gibt noch keine fertigen Builds: Kompiliere die Engine (siehe [Kompilieren](#kompilieren)) und gib ihr deinen Battlefield-1942-Ordner an, den mit `Mods/`:

```sh
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # Conquest in Team 2, Sturm-Kit
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # freie Kamera
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # ein Fahrzeug vor der Kamera
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # ohne Fenster: 10 Minuten Conquest, ausgegeben
```

| Steuerung | |
|---|---|
| Bewegen, umsehen | WASD, Maus (gefangen; F10 gibt sie frei), Shift geht, Leertaste springt (im Fall: öffnet den Fallschirm) |
| Waffen | linke Maustaste feuert, R lädt nach, 1–9 Kit-Waffen, G zündet Sprengladungen |
| Haltung, Ansicht | linke Strg-Taste duckt, Z legt hin, V Ego- / Third-Person-Ansicht |
| Fahrzeuge | E steigt ein / aus, 1–8 wechseln den Platz, WASD fährt, die Maus richtet Geschütztürme aus |
| Funk | F1–F7 öffnet ein Menü, dann sendet F1–F7 eine Nachricht |
| Flugzeuge | W / S Schub, Pfeiltasten Nicken und Rollen, A / D Seitenruder |
| Freie Kamera | rechte Maustaste sieht sich um, WASD / QE bewegen, Shift schnell; C zeigt Kollisionsmeshes; Esc beendet |

## Roadmap

Die Reihenfolge, mit den Begründungen und Abschlusskriterien, steht in [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md):

1. **Konsolidieren** (erledigt): das Spiel als Bibliothek, ein Renderer und schlanke ausführbare Dateien; CI.
2. **Erforschung des Netzwerkprotokolls** (eine Sitzung aufgezeichnet): den originalen Client/Server-Verkehr entschlüsseln, parallel zu den folgenden Schritten.
3. **KI-Navigation**, **Audio**, **HUD** (erste Teile erledigt).
4. **Vollständige Soldaten und Waffen** (größtenteils erledigt), **Fahrzeugbesatzungen** (Land und See erledigt, Piloten offen), **Effekte**.
5. **Originaltreue**: das Original messen und kalibrieren.
6. **Spielmodi**, **Menüs**.
7. **Netzwerk**: eigener Server und Client, dann gemeinsam mit dem Original spielen.
8. **Rendering**, **Verteilung** (Builds für Windows und Linux, Breitbild, Mods).

## Für Mitwirkende

Jede Art von Hilfe zählt: es ausprobieren und melden, was nicht stimmt, mit dem Originalspiel vergleichen, oder Code. Die technische Dokumentation, auf Englisch, beginnt bei [docs/README.md](docs/README.md): Dateiformate, Architekturentscheidungen und bekannte Schulden. [AGENTS.md](AGENTS.md) ist der Einstiegspunkt für KI-Programmieragenten.

## Kompilieren

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy) und Jolt Physics, das CMake beim ersten Konfigurieren herunterlädt. Unter Debian oder Ubuntu:

```sh
sudo apt install cmake g++ git libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/obf_mapview (Fenster), build/obf_sim (ohne Fenster)
```

<details>
<summary><b>Python-Werkzeuge</b></summary>

Leser für die Formate des Spiels, nur mit der Standardbibliothek von Python 3.

```sh
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # ein Level laden, eine Zusammenfassung ausgeben
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + Texturen
python3 -m unittest discover tools     # Tests
python3 tools/check_docs.py            # Dokumentationsprüfungen
```

</details>

<details>
<summary><b>Was wo liegt</b></summary>

| Pfad | Inhalt |
|---|---|
| `engine` | Die C++-Engine: Archive, Skripte, Meshes, Gelände, Animation, Physik, Spielregeln, KI, Audio, HUD, Renderer und die ausführbaren Dateien |
| `tools` | Python-Leser für die Formate, der Dokumentationsprüfer und ihre Tests |
| `docs/formats` | Die Spezifikationen der Datei- und Netzwerkformate |
| `docs/architecture` | Schichten, Grenzen und die Architekturentscheidungen (ADRs) |
| `docs/roadmap` | Die Roadmap und die bekannten Schulden |
| `docs/ai` | Die Gedächtnisschicht für KI-Agenten |

</details>

## Transparenz

- **Wer das pflegt:** OpenGamePlus. Ich mag alte Spiele und möchte Battlefield 1942 weiterhin mit Freunden auf heutigen Systemen spielen, mit einer offenen Engine, die die Community am Leben halten kann.
- **Wie die Arbeit gemacht wird:** Ich arbeite mit [Claude Code](https://claude.com/claude-code), dem Programmierassistenten von Anthropic. Code und Dokumentation, einschließlich dieser README und ihrer Übersetzungen, werden mit seiner Hilfe geschrieben. Auf diese Weise erstellte Commits tragen eine Zeile `Co-Authored-By: Claude` in ihrer Nachricht, sodass sie in der Historie mit `git log` erkennbar sind.
- **Clean Room:** Es wird kein Code der Original-Engine verwendet oder dekompiliert. Die Engine wird anhand der Datendateien des Spiels und durch Beobachtung des laufenden Originalspiels geschrieben.
- **Was es nicht ist:** Es ist kein offizielles Projekt und steht in keiner Verbindung zu Electronic Arts oder DICE. Es enthält keine Spieldateien.

## Hinweis

Dies ist eine unabhängige Neuimplementierung der Engine Refractor 1. Sie steht in keiner Verbindung zu Electronic Arts Inc., EA Digital Illusions CE (DICE) oder einem ihrer verbundenen Unternehmen und wird von diesen weder autorisiert, gesponsert noch unterstützt.

„Battlefield“ und „Battlefield 1942“ sind Marken von Electronic Arts Inc. Sie werden in diesem Repository nur zur Identifikation und Interoperabilität verwendet, um zu beschreiben, was die Engine ausführt. Es wird kein Anspruch auf diese Marken erhoben.

Dieses Projekt stellt keine urheberrechtlich geschützten Spieldaten bereit und hilft nicht, sie zu beschaffen. Um es mit echten Spielinhalten zu nutzen, musst du die Datendateien aus einer Kopie von Battlefield 1942 bereitstellen, die du rechtmäßig besitzt.

Wenn du Rechteinhaber bist und der Meinung bist, dass etwas hier nicht verbreitet werden sollte, öffne bitte ein Issue oder kontaktiere den Maintainer, und es wird umgehend bearbeitet.

Die Screenshots zeigen Inhalte von Battlefield 1942, Eigentum von Electronic Arts Inc., nur zur Identifikation.

## Lizenz

[GNU Affero General Public License v3.0](LICENSE). Wer die Engine verbreitet oder eine veränderte Version als Netzwerkserver für andere betreibt, muss ihren Quellcode unter derselben Lizenz anbieten. Bibliotheken von Drittanbietern behalten ihre eigene: SDL2 (zlib), libepoxy (MIT) und Jolt Physics (MIT).

## Danksagungen

- [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [SDL](https://www.libsdl.org/) und [libepoxy](https://github.com/anholt/libepoxy)
- Electronic Arts / DICE, für das Spiel
