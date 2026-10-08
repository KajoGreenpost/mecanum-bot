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

1. Generate/edit the static website.
2. Put the output in `data/`.
3. Increase `data/version.txt` independently from `FW_VERSION`.
4. Build `littlefs.bin`.
5. Connect to the ESP32 Wi-Fi.
6. Open `/maintenance`.
7. Upload only `littlefs.bin` under Webinterface Update.

Firmware is not modified by a web update.

## Firmware updates

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
