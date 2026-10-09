# MecanumBot ESP32

Standalone ESP32 Mecanum vehicle controller. No Raspberry Pi is required.

## Included

- ESP32 SoftAP and web server
- Four-motor LEDC control using the existing legacy Arduino-ESP32 LEDC API
- 100 Hz Mecanum control loop
- 300 ms control watchdog/failsafe
- Two 8-zone browser joysticks with 20 Hz heartbeat
- Bluetooth/BLE disabled; both CPU cores remain enabled
- Settings stored in Preferences/NVS
- Settings JSON export/import and factory reset
- Separate firmware OTA update (`firmware.bin`)
- Separate LittleFS web update (`littlefs.bin`)
- SHA-256 checked uploads
- Firmware-embedded `/maintenance` recovery page, independent of LittleFS
- Optional physical recovery input (disabled by default with `RECOVERY_PIN = -1`)

## Project layout

```text
MecanumBot_ESP32/
|-- MecanumBot.ino
|-- partitions.csv
|-- README.md
|-- BOLT_PROMPT.md
|-- data/
|   |-- index.html
|   |-- style.css
|   |-- app.js
|   `-- version.txt
`-- tools/
    |-- build-littlefs.ps1
    `-- hashes.ps1
```

## Important configuration before first flash

### Motor calibration in firmware 1.0.2

The GPIO profile is calibrated for the reported physical wiring, with all
motor and axis inversions off. From the three measured tests (left stick up,
left stick right, right stick right), physical FL was the old RL output with
reversed polarity, physical FR the old RR output, physical RL the old FR output
with reversed polarity, and physical RR the old FL output. The resulting pairs
are FL=4/2, FR=16/17, RL=21/19, RR=5/18 (IN1/IN2).

`python tools/test-motor-mapping.py` checks all sixteen stick sectors against
that measured wiring and the requested drawing. This verifies the inferred
mapping, not actual motor motion or wheel roller installation. Retest with
wheels lifted and all seven inversion switches off: left stick up must drive
all wheels forward; left stick right must drive the left wheels forward and
the right wheels backward; right stick right must drive FL/RR forward and
FR/RL backward. Another vehicle may require different GPIO assignments.

The configured short start boost can now exceed the selected 60%/80% running
power limit. At the default boost=82 and boostTime=40, starting can reach 82%
for 120 ms, then returns to the selected limit. Both the boost percentage and
its duration remain configurable, and the 300 ms control watchdog remains in
effect. This can help overcome breakaway friction; it does not guarantee that
the motors can keep running at 60% under the current battery/load conditions.

Edit these values in `MecanumBot.ino`:

```cpp
const char *AP_SSID = "Mecanum-Bot";
const char *AP_PASSWORD = "Mecanum123";
const char *ADMIN_USER = "admin";
const char *ADMIN_PASSWORD = "ChangeMe123!";
```

Change both passwords before deployment.

The supplied `partitions.csv` is for a 4 MB ESP32 flash and allocates:

- OTA app slot 0: 1.5 MB
- OTA app slot 1: 1.5 MB
- LittleFS: 960 KB
- NVS and OTA metadata

If your exact ESP32 has a different flash size, do not flash this partition table unchanged.

## First USB installation

The first installation must write the custom partition table. Keep `partitions.csv` in the same sketch folder as `MecanumBot.ino`, select the same board/core configuration you already use, compile and upload normally over USB.

This project intentionally keeps the legacy `ledcSetup()` / `ledcAttachPin()` API because the previous motor sketch was tested with that Arduino-ESP32 version. Do not upgrade the core only for this project.

The website can be uploaded separately afterward from `/maintenance` as `littlefs.bin`.

## ESP32 access

Default access point:

```text
SSID: Mecanum-Bot
Password: Mecanum123
IP: 192.168.4.1
```

Main control page:

```text
http://192.168.4.1/
```

Maintenance/recovery:

```text
http://192.168.4.1/maintenance
```

The maintenance page is embedded in firmware and remains available if LittleFS is missing or a web image is bad.

## Creating firmware.bin

In Arduino IDE use:

```text
Sketch -> Export Compiled Binary
```

Use the application image named similar to:

```text
MecanumBot.ino.bin
```

Rename/copy that file to:

```text
firmware.bin
```

Do not upload `bootloader.bin`, `partitions.bin` or a merged full-flash binary through the firmware OTA page.

## Creating littlefs.bin

The `data/` directory is the complete filesystem root. Bolt.new output should replace/update files inside `data/`, not the ESP32 sketch itself.

The supplied partition gives LittleFS 0x0F0000 bytes = 983040 bytes.

Run the supplied PowerShell helper from the project folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-littlefs.ps1
```

It searches the existing Arduino-ESP32 installation for `mklittlefs.exe` and creates:

```text
littlefs.bin
```

No Arduino-ESP32 version change is required.

Manual equivalent as a single PowerShell line:

```powershell
$m=(Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mklittlefs" -Recurse -Filter mklittlefs.exe | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName); & $m -c ".\data" -b 4096 -p 256 -s 983040 ".\littlefs.bin"
```

## SHA-256 locally

The maintenance page automatically calculates a SHA-256 before upload and the ESP32 calculates the SHA-256 again while receiving the image.

For an independent Windows check:

```powershell
(Get-FileHash .\firmware.bin -Algorithm SHA256).Hash.ToLower()
```

or:

```powershell
(Get-FileHash .\littlefs.bin -Algorithm SHA256).Hash.ToLower()
```

You can also run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\hashes.ps1
```

## Website updates

For the React website in the sibling `mecanum-bot-website` checkout, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-web.ps1
```

This runs the regression tests, builds the website, copies the current bundles
into `data/`, writes the package version, and creates `littlefs.bin`. Install
dependencies once with `npm ci` in the website checkout. Upload the resulting
`littlefs.bin` at `/maintenance` under **Webinterface Update**, then reload the
control page. Web UI 1.1.1 fixes repeated settings reloads and reduces disarmed
control traffic to one request per second. The armed heartbeat remains 20 Hz;
the existing firmware's 300 ms watchdog remains in effect. This fix requires
only a website update.

Web UI 1.1.2 adds a phone landscape controller with a single compact toolbar,
height-scaled sticks and a dedicated center activation/stop button. The brand
header is hidden on short landscape screens. Driving has no scroll container;
settings and system keep touch scrolling. Phones in portrait show a rotation
prompt and automatically disarm. Layout checks (including two simultaneous
touches) can be run with `npm run test:layout` in the website checkout; install
the browser once with `npx playwright install chromium` if needed.

Web UI 1.2.2 automatically enables motors when a connected, visible landscape
controller receives a stick deflection. Centering/releasing both sticks stops
the motors; releasing only one leaves the other stick active. The middle
button is now a stop button, with **BEREIT / STICK BEWEGEN** shown while idle.
After manual stop, connection loss, pointer cancellation, hiding the page or
portrait rotation, any held gestures must be released before fresh input can
enable motors. Switching tabs also stops driving. This behavior needs only a
website update and works with the existing firmware control-frame API.

1. Generate/edit the static website.
2. Put the output in `data/`.
3. Increase `data/version.txt` independently from `FW_VERSION`.
4. Build `littlefs.bin`.
5. Connect to the ESP32 Wi-Fi.
6. Open `/maintenance`.
7. Upload only `littlefs.bin` under Webinterface Update.

Firmware is not modified by a web update.

## Firmware updates

### Diagnostics (firmware 1.0.1 / website 1.2.0)

The **Diagnose** tab enables/disables firmware debugging and shows a live
terminal. Logs include enable/disable events, settings, failsafe stops, input
axes, signed motor PWM, free heap, connected Wi-Fi clients and command age.
This is application diagnostics, not a raw serial-console stream.

Debugging starts disabled after boot and is not stored in NVS. The firmware
keeps 32 entries of at most 159 text bytes and serves at most eight per request.
The page polls once a second with no overlapping requests and keeps at most
240 terminal rows. Polling stops while disabled, hidden or outside the tab;
without a reader the firmware disables logging after ten seconds. Terminal
output can be cleared; scrolling up pauses automatic following.

`GET /api/debug?after=<last id>` returns `enabled`, `bootId`, `dropped`, `lines`
(`id`, `ms`, `text`) and `next`. `POST /api/debug?after=<last id>` with a
form-encoded `enabled=0|1` toggles debugging and returns the same structure.
The preview labels all generated logs as simulation. Older firmware displays
a clear firmware-update message instead of a functioning debug switch.

Install both `firmware.bin` and `littlefs.bin` via their separate maintenance
upload fields, starting with firmware. The firmware was compiled for ESP32 Dev
Module with Arduino-ESP32 2.0.17 in an isolated build environment; the existing
legacy LEDC API and custom partition table are retained.

1. Change `FW_VERSION` in `MecanumBot.ino`.
2. Compile/export the application binary in Arduino IDE.
3. Use the application `.bin` as `firmware.bin`.
4. Open `/maintenance`.
5. Upload only `firmware.bin` under Firmware Update.

The active website and NVS settings are not intentionally erased by a normal application OTA update.

## Settings backup

Use `/maintenance` to export `mecanum-settings.json`. The export contains motor/calibration settings, not AP/admin passwords.

The same file can be imported later or applied to another vehicle with compatible hardware.

## Safety behavior

The browser sends a complete control frame every 50 ms. The firmware requires complete control frames and disables all motors if no valid frame arrives for more than 300 ms.

When a firmware or filesystem update starts, the firmware sets motor PWM to zero and drives `STBY_PIN` LOW for the duration of the update.

## Web API used by Bolt UI

### POST `/api/control`

Content type: `application/x-www-form-urlencoded`

Required every frame:

```text
s1_dir=0..8
s1_str=0..100
s2_dir=0..8
s2_str=0..100
en=0|1
```

### GET `/api/settings`

Returns JSON settings.

### POST `/api/settings`

Content type: `application/x-www-form-urlencoded`

Supported names:

```text
speedMode
startMin
boost
boostTime
deadzone
invertFL
invertFR
invertRL
invertRR
invertMoveY
invertMoveX
invertTurn
```

### `/maintenance`

Firmware-embedded maintenance UI. Bolt must link to this route but must not replace it.

## Bolt.new

Copy the complete contents of `BOLT_PROMPT.md` into Bolt.new. Bolt should generate only the static files for `data/`.
