# RJ45-Netzwerkkabeltester auf ESP32

Zwei ESP32-Boards prüfen ein RJ45-Kabel an beiden Enden:

- **Node A = Master (ESP32-S3)**: spannt das WLAN für die Web-UI auf, steuert
  den Test und wertet beide Messseiten aus.
- **Node B = Slave (ESP32-S3)**: sitzt am gegenüberliegenden Kabelende, misst
  passiv die Pegel und antwortet über ESP-NOW.

Beide Nodes laufen auf ESP32-S3 und werden mit demselben Target `esp32s3`
gebaut; unterschieden werden sie nur durch die Rolle (Kconfig).

Die Funkstrecke zwischen den Boards ist **ESP-NOW** (WLAN-Funk ohne Router, ohne
IP/DHCP). Der Master läuft dafür im APSTA-Modus: Access Point für Handy/Laptop,
STA-Schnittstelle für ESP-NOW. Zusätzlich gibt es eine serielle Konsole.

## Wichtig: gemeinsame Masse

Beide Tester müssen dieselbe elektrische Masse haben, sonst kann der Slave die
vom Master getriebenen Pegel nicht sehen und meldet alle Adern als offen.

- Beide USB-Kabel am selben Rechner bzw. an einem aktiven USB-Hub betreiben,
  **oder** eine GND-Leitung zwischen den Boards vorsehen.
- Beide Boards vor dem Test mit Strom versorgen.
- Nur **spannungsfreie, nicht angeschlossene** Kabel testen. Die GPIOs arbeiten
  mit 3,3 V und sind nicht 5-V-tolerant; nicht an aktive Netzwerkgeräte oder
  PoE anschließen. Die Schutzvarianten im
  [Schaltplan](#schaltplan-und-schutzbeschaltung) ändern daran nichts
  Grundsätzliches: Sie begrenzen Ströme und erkennen Fremdspannung, sind aber
  kein PoE-Isolationsfrontend.
- Die RJ45-Buchsen müssen direkt (ohne PHY/Magnetics), höchstens über die
  Serienwiderstände aus dem Schaltplan, mit den GPIOs verbunden sein.

## Funktionen

- 8×8-Messmatrix: welche Ader an A mit welcher Ader an B verbunden ist
- Unterbrechungen und Kurzschlüsse (auch innerhalb eines Steckers)
- Kabeltyp-Erkennung: 1:1-Patchkabel, Crossover (Gigabit und 10/100),
  benutzerdefinierte Belegung, fehlerhaft
- Experimentelle Längenmessung pro Paar (Kapazitätsverfahren)
- Web-UI im WLAN mit Verdrahtungsgrafik, Matrix, Diagnose und Rohdaten
- Serielle Konsole mit `test`, `status`, `wifi`, `reboot`, `help`
- Automatisches Wieder-Pairing (Slave sendet alle 3 s ein HELLO)

## Pin-Mapping

Die Arrays sind pro Node getrennt anpassbar und werden über das Build-Target
ausgewählt:

| RJ45-Kontakt | Node A (ESP32-S3 Master) | Node B (ESP32-S3 Slave) |
| --- | --- | --- |
| 1 | GPIO 4 | GPIO 4 |
| 2 | GPIO 5 | GPIO 5 |
| 3 | GPIO 6 | GPIO 6 |
| 4 | GPIO 7 | GPIO 7 |
| 5 | GPIO 8 | GPIO 8 |
| 6 | GPIO 9 | GPIO 9 |
| 7 | GPIO 10 | GPIO 10 |
| 8 | GPIO 11 | GPIO 11 |

Beide Nodes verwenden dieselbe RJ45-Verdrahtung auf GPIO 4–11; die Rolle wird
nur über die Firmware gewählt. Damit sind identische Platinen und Kabelbäume
für Master und Slave möglich. Ein Display am Master nutzt davon getrennte,
reservierte Pins (siehe unten).

Die Kontaktzählung entspricht der üblichen RJ45-/T568-Zählung; bitte einmal mit
einem Durchgangsprüfer gegen die eigene Platine verifizieren. Wenn eine Platine
anders verdrahtet ist, muss der jeweilige Pin-Array in `pin_map.hpp`
(`kNodeAPins`/`kNodeBPins`) entsprechend angepasst werden — die Tabelle ist nur
der sichere Default.

Ändern: `components/rj45_port/include/rj45_port/pin_map.hpp` (`kNodeAPins` und
`kNodeBPins`). Danach die betroffene Rolle neu bauen und flashen. Für den S3
vermeiden: GPIO 0/3/45/46 (Strapping), 19/20 (USB), 26–37 (Flash/PSRAM),
39–42 (JTAG), 43/44 (UART0). Nur falls doch noch ein Classic-ESP32 als Node
verwendet wird: GPIO 0, 2, 5, 12, 15 (Strapping), 6–11 (Flash), 34–39 (nur
Eingang), 1 und 3 (UART0) vermeiden.

### SPI-Display am Master (reserviert)

Für ein ILI9488-Modul sind am Master folgende GPIOs reserviert; sie
kollidieren nicht mit den RJ45-Pins 4–11 (der Slave ist eine eigene Platine
und davon nicht betroffen):

| Funktion | GPIO |
| --- | --- |
| SPI SCLK | 12 |
| SPI MOSI | 13 |
| SPI MISO (optional, Touch) | 14 |
| Display CS | 15 |
| Display DC/RS | 16 |
| Display RST | 17 |
| Backlight (LED/PWM) | 18 |
| Touch CS (XPT2046, optional) | 21 |
| Touch IRQ (XPT2046, optional) | 47 |

Die Konstanten stehen in `pin_map.hpp` im Namensraum `display`; ein
`static_assert` verhindert, dass sie mit `kNodeAPins` kollidieren. Der
SPI-Bus läuft über die GPIO-Matrix — für ein ILI9488 ist das unkritisch.
Wenn das Display anders verdrahtet wird, nur die `display::…`-Konstanten
anpassen; die RJ45-Pins bleiben unverändert.

## Schaltplan und Schutzbeschaltung

Beide Nodes sind elektrisch identisch; die Rolle steckt nur in der Firmware.
Jede der acht RJ45-Adern führt über einen **1-kΩ-Serienwiderstand** auf den
GPIO (GPIO 4–11). Der Widerstand sitzt auf der Platine zwischen RJ45-Kontakt
und GPIO.

**Wirkung der 1-kΩ-Widerstände:**

- Ein Kurzschluss zwischen zwei Adern oder nach GND begrenzt den Treiberstrom
  auf ca. 3,3 mA — der GPIO bleibt auch im Fehlerfall innerhalb seiner
  Spezifikation.
- ESD- und Fremdspannungsströme werden begrenzt.
- Die digitalen Messungen bleiben unverändert: Der interne Pull-down des
  Empfängers (~45 kΩ) bildet mit den Serienwiderständen beider Seiten
  (2 × 1 kΩ) einen Teiler. Am Slave-Pin liegen im guten Fall noch ca. 3,16 V,
  klar über V<sub>IH</sub> = 0,75 × 3,3 V = 2,475 V.
- Die Längenmessung lädt über 45 kΩ + 1 kΩ, also ca. 2 % länger;
  `LengthConfig::pullup_ohm` kann auf `46000` gesetzt werden. Das liegt weit
  innerhalb der experimentellen Toleranz von ±20–30 %.
- Die Widerstände ersetzen **nicht** die gemeinsame Masse. Beide Boards
  weiterhin über denselben USB-Host/Hub oder eine GND-Leitung verbinden.

Die 1 kΩ sind bewusst klein gewählt: Mehr Serienwiderstand erhöht den Schutz,
senkt aber den Pegel am Slave. Mit 4,7 kΩ je Seite lägen noch ca. 2,73 V an
(besserer Kompromiss), mit 10 kΩ nur noch 2,28 V — unter
V<sub>IH</sub> = 2,475 V.

> Beide Varianten sind für **spannungsfreie, nicht angeschlossene** Kabel
> gedacht. Variante A erkennt Fremdspannung und schützt gegen
> ESD/Transienten, ist aber **kein PoE-Isolationsfrontend**. 48–57 V dürfen
> nie direkt an den GPIOs anliegen.

Versorgung: Beide Dev-Boards werden über USB mit 5 V versorgt; die 3,3-V-Rail
entsteht auf dem Board. Die USB-GNDs beider Nodes müssen verbunden sein
(gleicher Host/Hub oder eine GND-Leitung), sonst funktioniert die
Pegelmessung nicht. Die Schaltbilder sind Prinzipschaltbilder (SVG), keine
Layoutvorlage. Beide Schaltungen sind dokumentierte Entwürfe; auf echter
Hardware ist noch keine der beiden Varianten erprobt.

### Variante B — nur 1-kΩ-Serienwiderstände

Minimalaufbau für passive Kabel: pro Ader ein 1-kΩ-Widerstand je Node, sonst
keine Bauteile am RJ45-Kontakt.

![Variante B — nur 1-kΩ-Serienwiderstände](docs/schematic-variante-b.svg)

- Bestückung: 8 × 1 kΩ pro Board; im Bild heißen die Slave-Widerstände zur
  Unterscheidung R1′–R8′.
- Schützt gegen Kurzschluss und begrenzt ESD; Fremdspannung wird nicht erkannt.
- Aktive Ethernet-Pegel (±2,5 V) werden nur über die internen ESD-Dioden
  abgeleitet — nicht für den Dauerbetrieb an aktiven Ports geeignet.

### Variante A — Aktivkabelschutz

„Aktivkabelschutz“ heißt hier: ESD-/Transientenschutz an den Kontakten plus
Erkennung einer Fremdspannung, damit der Test gar nicht erst startet. Es ist
kein Isolationsfrontend.

Zusätzlich zum 1-kΩ-Serienwiderstand:

- **D1/D2 (SRV05-4 oder TPD4E05U06, low-cap):** ESD-Array direkt an den
  RJ45-Kontakten, also vor dem 1-kΩ-Widerstand. Zwei ICs pro Board decken die
  acht Adern ab. Die Kapazität (ca. 0,5–3 pF) liegt innerhalb der
  Messstreuung der Kapazitäts-/Längenmessung.
- **Fremdspannungsdetektor** (auf beiden Boards bestückbar): hochohmiger
  **differentieller** Teiler von den PoE-Paarpaaren auf zwei ADC1-Eingänge.
  Der Master wertet seinen Detektor vor dem Test aus; der Slave kann seinen
  lokal auswerten. Für eine Warnung vom Slave an den Master wäre das
  ESP-NOW-Protokoll zu erweitern.
  - Mode A (Datenpaare, 802.3af/at/bt): Kontakt 1 (Paar 1–2) und Kontakt 3
    (Paar 3–6).
  - Mode B (Spare-Paare): Kontakt 4 (Paar 4–5) und Kontakt 7 (Paar 7–8) —
    dafür einen zweiten Detektor vorsehen oder den Teiler umverdrahten.
  - Die Differenzmessung ist wichtig: Eine Spannungsmessung gegen die lokale
    Masse würde bei einem schwebenden PoE-Netz nur den Common-Mode-Anteil
    sehen. Der Teiler bildet deshalb die Spannung **zwischen** den Paaren.
- Optional: selbstheilende PTC-Sicherung pro Ader (z. B. 20 mA Hold), um einen
  versehentlichen PoE-Kontakt strommäßig zu begrenzen. Auslösezeit und
  TVS-Belastung müssen getestet werden; kein Ersatz für ein
  Isolationsfrontend.

![Variante A — Aktivkabelschutz](docs/schematic-variante-a.svg)

Der Detektor im Detail (beide Kanäle; die 10-MΩ-Taps sind hochohmig genug, um
die übrigen Messungen nicht zu stören):

![Fremdspannungsdetektor — Detail](docs/schematic-detektor.svg)

**Was der Schutz leistet:**

| Fall | Variante B | Variante A |
| --- | --- | --- |
| Kurzschluss zwischen Adern | 3,3 mA, GPIO geschützt | 3,3 mA, GPIO geschützt |
| ESD beim Stecken/Anfassen | nur über 1 kΩ begrenzt | ESD-Array + 1 kΩ |
| Aktives Ethernet (±2,5 V) | grenzwertig, interne Dioden | geclampt, aber **nicht sicher erkannt** |
| PoE/passives PoE (≥ 24 V) | **nicht geschützt** | Detektor warnt, TVS/1 kΩ nur kurzzeitig belastbar — **nicht PoE-sicher** |
| Fehlende gemeinsame Masse | wird nicht erkannt | wird nicht erkannt |

**Bauteile Variante A (pro Board):**

| Bauteil | Typ/Wert | Anzahl | Funktion |
| --- | --- | --- | --- |
| R1–R8 | 1 kΩ, 1 %, 0,25 W | 8 | Serienwiderstand, Strombegrenzung |
| D1, D2 | SRV05-4 (SOT-23-6) oder TPD4E05U06 | 2 | 4-Kanal-ESD-Array, 5 V Standoff, low-cap |
| C1, C2 | 100 nF | 2 | Entkopplung des ESD-Arrays an 3,3 V |
| R9, R10 | 10 MΩ | 2 | hochohmiger Teiler (Fremdspannung) |
| R11, R12 | 470 kΩ | 2 | Teilerfuß |
| D3, D4 | BAV99 | 2 | ADC-Klemmung gegen GND/3,3 V |
| R13, R14 | 10 kΩ | 2 | ADC-Serienwiderstand |
| C3, C4 | 100 nF | 2 | ADC-Stützkapazität |
| optional | PTC 20 mA (Hold) | 8 | PoE-Fehlerstrom begrenzen (Test nötig) |

**Firmware-Stand:** Der Detektor ist im Schaltplan vorgesehen, aber noch nicht
implementiert. Sinnvoller Ablauf am Master: vor jedem Test alle Leitungen
hochohmig schalten, ca. 100 ms warten, beide ADC-Kanäle lesen, `|U_A − U_B|`
bilden und bei Überschreitung von ca. 0,5 V den Test mit Warnung in
Web-UI/Konsole verweigern. Die ADC-Pins (z. B. GPIO 1/2; GPIO 3 ist Strapping-Pin und bleibt
frei) als Konstanten in `pin_map.hpp` ergänzen. Vor dem Layout prüfen, ob
GPIO 1/2 auf der eigenen Platine frei sind.

## Bauen und flashen

Voraussetzung: **ESP-IDF 6.1.x** (getestet mit 6.1.1). Die Komponentenstruktur
von IDF 6 wird vorausgesetzt (`esp_driver_gpio`, kein gebündeltes cJSON).

Das Target steht pro Rolle in den Defaults: Master = `esp32s3`
(`sdkconfig.master.defaults`), Slave = `esp32s3` (`sdkconfig.slave.defaults`).
Die beiden Images unterscheiden sich nur durch die Rolle und sind **nicht**
austauschbar.

Auf diesem Rechner:

```sh
source /home/agerf/.espressif/tools/activate_idf_v6.1.1.sh
cd /home/agerf/develop/network_cable_tester
```

Bei anderer Installation die ESP-IDF-Umgebung wie üblich laden
(z. B. `. $HOME/esp/esp-idf/export.sh`).

Beide Rollen bauen (getrennte Build-Verzeichnisse, sdkconfigs und Targets):

```sh
scripts/idf_role.sh master build
scripts/idf_role.sh slave build
```

Falls ein bestehendes Build-Verzeichnis noch auf ein anderes Target zeigt
(`build-slave/` war früher auf `esp32` konfiguriert):

```sh
scripts/idf_role.sh master set-target esp32s3
scripts/idf_role.sh slave  set-target esp32s3
```

Flashen — **Master-Image auf Node A (ESP32-S3), Slave-Image auf Node B (ESP32-S3)**:

```sh
scripts/idf_role.sh master flash -p /dev/ttyACM0
scripts/idf_role.sh slave  flash -p /dev/ttyACM1
```

Welcher Port zu welchem Board gehört, zeigt `ls -l /dev/serial/by-path/`; die
Portnamen (`ttyACM0` ↔ `ttyACM1`) können sich beim Neu-Enumerieren ändern.
Beide Boards erscheinen in dieser Konfiguration als CH343-USB-UART-Brücke
(`1a86:55d3`) unter `/dev/ttyACM*`.

Monitor:

```sh
scripts/idf_role.sh master monitor -p /dev/ttyACM0
scripts/idf_role.sh slave  monitor -p /dev/ttyACM1
```

Die Rolle wird zur Compile-Zeit über Kconfig gewählt
(`CONFIG_CABLE_TESTER_ROLE_MASTER`/`..._SLAVE`, siehe
`main/Kconfig.projbuild`). Beide Boards brauchen also unterschiedliche Images.
Nach Protokolländerungen immer beide Rollen neu bauen und flashen.

Generierte Dateien (`sdkconfig.master`, `sdkconfig.slave`, `build-master/`,
`build-slave/`) sind per `.gitignore` ausgeschlossen.

## VS Code

Die Workspace-Konfiguration liegt unter `.vscode/` und ist eingecheckt:

- **Extensions** (auf diesem Rechner bereits installiert):
  `espressif.esp-idf-extension` und `ms-vscode.cpptools`.
- **IntelliSense** nutzt die `compile_commands.json` aus den Build-Verzeichnissen.
  Drei Konfigurationen stehen bereit: `ESP32 Master`, `ESP32 Slave` und
  `Host Tests` (umschalten über die Statusleiste unten rechts oder
  `C/C++: Select a Configuration…`).
- **Terminal**: Das Profil `ESP-IDF` lädt die ESP-IDF-Umgebung automatisch;
  neue integrierte Terminals starten damit.
- **Tasks** (`Terminal → Run Task…`): Master/Slave bauen, flashen (fragt nach
  dem seriellen Port), Monitor und Host-Tests. `Ctrl+Shift+B` baut beide Rollen.
- Die Espressif-Erweiterung ist auf den **Master-Build** voreingestellt
  (`build-master`, `sdkconfig.master`). Für den Slave die Tasks verwenden. Fragt
  die Erweiterung nach einem Setup, die vorhandene Installation
  `~/.espressif/v6.1.1/esp-idf` auswählen.
- Nach dem ersten Öffnen ggf. das Fenster neu laden
  (`Developer: Reload Window`), damit die Extensions aktiv werden.
- Falls IntelliSense nach Änderungen hakt: einmal bauen (aktualisiert
  `compile_commands.json`) und `C/C++: Reset IntelliSense Database` ausführen.
- JTAG-Debugging ist nicht vorkonfiguriert.

Auf diesem Rechner liegt VS Code unter
`~/develop/vscode/VSCode-linux-x64/bin/code` (nicht im `PATH`). Öffnen mit:

```sh
~/develop/vscode/VSCode-linux-x64/bin/code ~/develop/network_cable_tester
```

## Bedienung

1. Beide Tester mit gemeinsamer Masse mit Strom versorgen.
2. Das zu prüfende Kabel zwischen die beiden RJ45-Buchsen stecken.
3. Mit dem WLAN **`CableTester`** verbinden (Passwort **`cabletest123`**) und
   <http://192.168.4.1/> öffnen.
4. Oben muss „Slave verbunden“ stehen; dann **Kabel testen** drücken.
5. Alternativ seriell mit 115200 8N1: `test` startet einen Test, `status`
   zeigt den Link, `wifi` zeigt/ändert SSID/Passwort, `reboot` startet das
   Board neu, `help` listet die Kommandos.

Auf dem ESP32-S3 läuft die serielle Konsole standardmäßig über **UART0**
(UART-USB-Port des Dev-Boards). Für den **nativen USB-Port** des S3 in
`menuconfig` `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` setzen (oder in
`sdkconfig.master` eintragen) und neu bauen; die Firmware unterstützt beide
Konsolentypen. Die Web-UI funktioniert unabhängig davon.

### WLAN-Zugangsdaten ändern

Die Defaults stehen in `main/app_config.hpp` (`CableTester`/`cabletest123`).
Über die serielle Konsole des Masters lassen sie sich ändern; die Werte werden
im NVS gespeichert und überleben einen Neustart:

```text
wifi [status]           SSID, Passwort, Quelle, AP-IP, Kanal, MACs, Clients anzeigen
wifi ssid "Werkstatt"   SSID setzen (1-32 Zeichen, Leerzeichen in "...")
wifi pass "geheim123"   Passwort setzen (8-63 druckbare Zeichen)
wifi reset              Defaults wiederherstellen
```

`wifi` ohne Argument entspricht `wifi status` und zeigt zusätzlich die
AP-IPv4-Adresse samt Netzmaske/Gateway, den Kanal, die AP-/STA-MAC sowie alle
verbundenen WLAN-Clients mit ihrer per DHCP/ARP zugewiesenen IPv4-Adresse.

Die Änderung wird sofort angewendet; der AP startet dabei neu, verbundene
WLAN-Clients müssen sich also neu verbinden. Ohne gespeicherte Werte gelten die
Defaults. Bei vergessenem Passwort hilft `wifi reset` über die serielle
Konsole. Mit `reboot` startet das Board neu (auf Master und Slave verfügbar).

Der WLAN-Kanal ist fest **1** (`protocol::kChannel`); Master und Slave müssen
denselben Kanal verwenden.

Die Web-UI hat außer dem WPA2-Passwort keine weitere Authentifizierung — für den
lokalen Werkstattbetrieb gedacht.

## Messprinzip

Im Ruhezustand sind alle acht Leitungen Eingänge mit internem Pulldown. Ein
HIGH-Pegel ohne Ansteuerung wird als Warnung gemeldet (Fremdspannung/Defekt).

Für jede der acht Adern:

1. Der Master treibt genau diese Ader push-pull auf HIGH, alle anderen bleiben
   Eingänge mit Pulldown.
2. Nach 2 ms Settle-Zeit liest der Master seine acht Pegel und der Slave seine
   acht Pegel; der Slave schickt sein Bitmuster per ESP-NOW zurück.
3. Aus beiden Bitmustern entsteht die 8×8-Matrix.

Auswertung:

- Genau ein HIGH-Pegel am Slave → eindeutige Verbindung A<i> → B<j>.
- Mehrere HIGH-Pegel am Slave oder ein zusätzlicher HIGH-Pegel auf der
  Master-Seite → Kurzschluss.
- Kein HIGH-Pegel am Slave → Ader offen.
- Ein reiner Aderntausch ist **kein** Kurzschluss und wird als benutzerdefinierte
  Belegung erkannt.
- 1:1-Belegung → Patchkabel; vollständiger bzw. 10/100-Paartausch → Crossover;
  alles andere → benutzerdefiniert; Lücken/Mehrfachverbindungen → fehlerhaft.

### Längenmessung (experimentell)

Nur bei elektrisch sauberer Verdrahtung: Der Slave geht hochohmig, der Master
lädt jede Ader über den internen Pull-up auf und misst die Ladezeit. Aus
`t = R·C·ln(4)` wird die Kapazität und daraus mit ca. 50 pF/m eine Länge
geschätzt. Die interne Pull-up-Resistenz streut stark; die Werte sind nur grobe
Anhaltspunkte (±20–30 %). Kalibrierkonstanten stehen in `LengthConfig` in
`components/rj45_port/include/rj45_port/rj45_port.hpp`.

Mit den 1-kΩ-Serienwiderständen aus dem Schaltplan steigt die effektive
Ladezeit um ca. 2 % (`pullup_ohm` dann auf `46000` setzen). Die low-cap
ESD-Arrays der Schutzvariante liegen mit ca. 0,5–3 pF innerhalb der
Messstreuung.

### Grenzen

- **T568A und T568B sind elektrisch identisch** (gleiche Paar-Pin-Zuordnung).
  Ein Kontinuitätstester kann sie nicht unterscheiden. Erkannt werden 1:1,
  Crossover, offene Adern, Kurzschlüsse und abweichende Belegungen.
- Kurzschlüsse können eine offene Ader überdecken; bei mehreren Fehlern ist die
  Topologie nicht immer eindeutig. Die Rohmatrix zeigt alle Beobachtungen.
- Die internen Pull-downs sind schwach; sehr lange oder hochohmige Kabel können
  als offen erscheinen.

## Projektstruktur

```
components/
  cable_test/    reine C++-Analyse-Logik (ohne ESP-IDF, host-testbar)
  rj45_port/     GPIO-Schicht + Pin-Mapping pro Node
  cable_link/    ESP-NOW-Protokoll, Master-/Slave-Link
  test_runner/   Ablaufsteuerung der Messung
  report/        JSON- und Text-Report
  web_ui/        HTTP-Server + eingebettete Single-Page-UI
main/            Rollenwahl, WLAN-AP/STA, Konsole, app_main
host_tests/      Host-Tests für die Analyse
scripts/         idf_role.sh (Build/Flash pro Rolle)
.vscode/         VS-Code-Workspace (IntelliSense, Tasks, Extension-Empfehlungen)
docs/            SVG-Schaltbilder für das README
```

Datenfluss Master: Web-UI/Konsole → `TestRunner` → `Rj45Port` (treiben/lesen) +
`MasterLink` (Slave-Abfrage) → `cable_test::analyze` → `report` → HTTP/Seriell.

## Tests

```sh
cmake -S host_tests -B host_tests/build
cmake --build host_tests/build
ctest --test-dir host_tests/build --output-on-failure
```

Die Host-Tests decken ab: 1:1, Crossover (Gigabit und 10/100), offene Ader,
Kurzschluss am fernen und nahen Ende, Tausch vs. Kurzschluss, doppelte
Zuordnung, Ruhezustand mit Fremdspannung, komplett offenes Kabel.

Zusätzlich müssen beide Firmware-Rollen fehlerfrei bauen:

```sh
scripts/idf_role.sh master build
scripts/idf_role.sh slave build
```

## Fehlersuche

| Symptom | Ursache/Abhilfe |
| --- | --- |
| Alle 8 Adern offen | Kabel nicht gesteckt, **fehlende gemeinsame Masse**, falsches Pin-Mapping |
| „Slave getrennt“ | Slave ohne Strom, anderer Kanal, unterschiedliche Firmware; nach 3 s sendet der Slave erneut ein HELLO |
| Web-UI nicht erreichbar | Mit der aktuellen SSID verbunden? Master-Logs prüfen, <http://192.168.4.1/>; bei vergessenem Passwort seriell `wifi reset` |
| SSID/Passwort vergessen | Seriell am Master `wifi reset` (stellt die Defaults aus `app_config.hpp` wieder her) |
| Test bricht mit Verbindungsfehler ab | Funkstrecke/Power des Slaves prüfen; Master und Slave neu starten |
| Serielle Ausgabe unlesbar | 115200 8N1 einstellen |
| Monitor: `-- WARNING: Writing to serial is timing out …` | Der Host kann nicht in den Port schreiben; meist hängt die USB-UART-Brücke (z. B. CH343), oder es ist der falsche Port. USB-Kabel abziehen/neu anstecken, anderen Port/Kabel testen, `journalctl -k -f` prüfen. Die REPL läuft auf UART0 (UART-USB-Port), nicht auf dem nativen USB-Port. |
| Monitor: „device reports readiness … multiple access on port“ / Port verschwindet | USB-Kabel/-Port/Stromversorgung prüfen. Das Kernel-Log (`journalctl -k -f`) zeigt dann `USB disconnect`, `error -71` und Neu-Erkennung. Laufenden Monitor beenden und den stabilen Pfad `/dev/serial/by-path/…` verwenden. Der Portname (`ttyACM0` ↔ `ttyACM1`) kann sich beim Neu-Enumerieren ändern; `idf.py monitor` folgt ihm nicht. |
| S3: keine Logs/Konsole über den nativen USB-Port | Die Konsole läuft per Default über UART0. Für USB Serial/JTAG `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` setzen und neu bauen; die Web-UI ist davon unabhängig. |
| `phy_init: failed to load RF calibration data` | Normal beim ersten Boot nach dem Flashen (vollständige RF-Kalibrierung). Erst kritisch, wenn das Board dabei resetet oder der USB-Port verschwindet – dann Stromversorgung/Cable prüfen. |
| Build-Fehler `cjson`/`driver/gpio.h` | ESP-IDF 6.1.x verwenden (dieses Projekt nutzt `esp_driver_gpio` und kein cJSON) |

## Status

Build beider Rollen (Master und Slave, jeweils `esp32s3`) mit ESP-IDF 6.1.1 und
die Host-Tests sind verifiziert. Die Inbetriebnahme auf echter Hardware (beide
Boards, reales Kabel) steht noch aus.
