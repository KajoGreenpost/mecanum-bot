# ESP32 HTTP-API

Die Website verwendet relative Pfade auf dem ESP32, standardmäßig unter `http://192.168.4.1`. Die Firmware liefert die Website aus LittleFS; `/maintenance` ist unabhängig davon in die Firmware eingebettet.

## Endpunkte

| Methode | Pfad | Funktion | Admin-Anmeldung |
| --- | --- | --- | --- |
| POST | `/api/control` | Vollständigen Steuerrahmen übertragen | Nein |
| GET / POST | `/api/settings` | Einstellungen lesen / speichern | Nein |
| GET / POST | `/api/debug` | Diagnose lesen / schalten | Nein |
| POST | `/api/calibration` | Kalibrierung starten, regeln oder stoppen | Nein |
| GET | `/maintenance` | Wartungsseite | Ja |
| GET | `/api/system` | Systeminformationen | Ja |
| GET | `/api/settings/export` | Einstellungen als JSON exportieren | Ja |
| POST | `/api/settings/import` | JSON-Einstellungen importieren | Ja |
| POST | `/api/settings/reset` | Standardwerte wiederherstellen | Ja |
| POST | `/api/reboot` | Neustart | Ja |
| POST | `/api/update/firmware` | Anwendungsimage hochladen | Ja |
| POST | `/api/update/filesystem` | LittleFS-Image hochladen | Ja |

Die Admin-Funktionen verwenden HTTP Basic Authentication. Die Steuerendpunkte sind für Clients im lokalen Bot-WLAN vorgesehen.

## Fahrsteuerung

`POST /api/control`, Content-Type `application/x-www-form-urlencoded`:

```text
s1_dir=1&s1_str=70&s2_dir=0&s2_str=0&en=1
```

Alle fünf Felder sind in jedem Rahmen erforderlich:

| Feld | Bereich | Bedeutung |
| --- | --- | --- |
| `s1_dir`, `s2_dir` | 0–8 | 0 = Mitte; 1 = oben; danach im Uhrzeigersinn bis 8 = oben links |
| `s1_str`, `s2_str` | 0–100 | Stärke; aktuelle Website überträgt außerhalb der Totzone den Rohwert |
| `en` | 0 / 1 | Deaktivieren / Freigabe anfordern |

Antwort: `{"status":"ok"}`. Unvollständige Rahmen werden mit HTTP 400 abgelehnt. Recovery oder ein laufendes Update verhindern die Steuerung mit HTTP 423.

Der linke Stick mischt Vorwärts/Rückwärts und Drehung. Der rechte mischt Seitwärts und die diagonalen Fahrtrichtungen; seine reinen oberen/unteren Sektoren haben keine Fahrfunktion. Die Firmware wendet Totzone, Richtungsinvertierungen, Mecanum-Mischung, Fahrkurve, Geschwindigkeit, Start-Minimum, Boost und Motorinvertierungen an.

Die Fahr-Heartbeat-Rate beträgt bei Freigabe 20 Hz. Ohne gültigen Steuerrahmen für mehr als 300 ms stoppt die Firmware. Bei neutralen Rahmen bleibt die Freigabe höchstens `idleTimeoutSeconds` bestehen; wiederholte neutrale Rahmen starten diese Frist nicht neu und reaktivieren ein bereits deaktiviertes Fahrzeug nicht. `en=0` deaktiviert sofort.

## Einstellungen

`GET /api/settings` gibt ein JSON-Objekt mit `schema: 1` zurück. `POST /api/settings` akzeptiert Teiländerungen als `application/x-www-form-urlencoded` und gibt den vollständigen gespeicherten Stand zurück.

| Feld | Bereich | Standard |
| --- | --- | --- |
| `speedMode` | 0 = 100 %, 1 = 80 %, 2 = 60 % | 0 |
| `startMin` | 0–100 % | 18 |
| `boost` | 0–100 % | 82 |
| `boostTime` | 0–100; entspricht 0–300 ms | 40 = 120 ms |
| `deadzone` | 0–30 % | 8 |
| `idleTimeoutSeconds` | 0–30 Sekunden | 10 |
| `invertFL`, `invertFR`, `invertRL`, `invertRR` | 0 / 1 | 0 |
| `invertMoveY`, `invertMoveX`, `invertTurn` | 0 / 1 | 0 |

Einstellungen werden in Preferences/NVS gespeichert. Export und Import verwenden dieselben Einstellungsnamen; der Import akzeptiert JSON im Request-Body. WLAN- und Admin-Passwörter sind kein Bestandteil dieses Exports. Änderungen, Import und Reset beenden einen laufenden Kalibrierungstest.

## Diagnose

`GET /api/debug?after=<letzte-id>` liefert:

```json
{
  "enabled": true,
  "bootId": 123,
  "dropped": 0,
  "lines": [{"id": 7, "ms": 1234, "text": "STATE ..."}],
  "next": 7
}
```

`POST /api/debug?after=<letzte-id>` mit dem Form-Feld `enabled=0|1` schaltet Debugging und gibt dieselbe Struktur zurück. `bootId` erkennt Neustarts; `dropped` weist auf überschriebene Einträge hin. Bei einer neuen Boot-ID muss der Cursor zurückgesetzt werden.

Der RAM-Puffer umfasst 32 Einträge mit höchstens 159 Textbytes; eine Antwort enthält höchstens acht Zeilen. Ohne Diagnose-Leser endet Logging nach zehn Sekunden. Debugging startet nach einem Neustart ausgeschaltet und schreibt keine Logs in Flash. Die Website fragt einmal pro Sekunde ab und zeigt höchstens 240 Zeilen.

## Direkte Motorkalibrierung

`POST /api/calibration`, Content-Type `application/x-www-form-urlencoded`.

1. `action=start` öffnet eine neutrale Sitzung und liefert `active`, `session` und `done`.
2. `action=drive` überträgt alle unten genannten Felder mit dem erhaltenen Sitzungstoken.
3. `action=stop` stoppt jederzeit und macht das Sitzungstoken ungültig.

| Feld für `action=drive` | Bedeutung |
| --- | --- |
| `session` | Sitzungstoken aus `action=start` |
| `sequence` | Fortlaufende Testnummer; eine neue Nummer startet einen neuen Test |
| `mode` | `live` oder `pulse` |
| `pwm` | Direkte Motorleistung, 0–100 % |
| `boost` | Anfangsimpuls, 0–100 % |
| `boostMs` | Dauer des Anfangsimpulses, 0–300 ms |
| `wheel` | 0 = alle, 1 = FL, 2 = FR, 3 = RL, 4 = RR |
| `reverse` | 0 = vorwärts, 1 = rückwärts |

Beispiel für einen Test mit 82 % Boost für 120 ms und anschließend 18 % Leistung:

```text
action=drive&session=123&sequence=1&mode=pulse&pwm=18&boost=82&boostMs=120&wheel=0&reverse=0
```

Die Antwort enthält `active`, `session` und `done`. Wiederholte Rahmen mit derselben `sequence` aktualisieren die laufende Leistung und halten die Sitzung am Leben, starten aber weder den Boost noch die Testdauer neu. Ungültige oder abgelaufene Sitzungen werden mit HTTP 409 abgelehnt; unvollständige Rahmen stoppen die Sitzung und liefern HTTP 400.

`live` läuft maximal 15 Sekunden. `pulse` endet nach dem Anfangsimpuls und 600 ms bei `pwm`. Beide werden alle 50 ms durch die Website versorgt und stoppen nach mehr als 300 ms ohne Kalibrierungsrahmen. Stoppen invalidiert das Token, sodass verspätete Regelpakete keinen neuen Test auslösen können.

Kalibrierung umgeht Fahrkurve, Geschwindigkeitsbegrenzung, Mecanum-Mischer und normale Mindestleistung. Motorinvertierungen gelten weiterhin. Normale Fahrrahmen beenden eine Kalibrierung; die Website pausiert deshalb die normalen Fahr-Heartbeats, solange der Assistent geöffnet ist. Firmware-/Website-Updates und Neustart beenden Motortests ebenfalls.

## Updates und Wiederherstellung

Uploads werden durch die eingebettete Wartungsseite vorbereitet. Sie berechnet den SHA-256-Hash und überträgt das Image; der ESP32 prüft den empfangenen Inhalt nochmals. Firmware-OTA erwartet ausschließlich ein Anwendungsimage, Filesystem-OTA ein LittleFS-Image mit passender Partitionsgröße.

Die Wartungsseite bleibt bei fehlendem LittleFS verfügbar. Wenn ein physischer Recovery-Pin konfiguriert ist, hält LOW beim Booten die Motoren deaktiviert und aktiviert die Wartungsfunktionen.
