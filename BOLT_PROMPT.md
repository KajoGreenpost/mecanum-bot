# Prompt for Bolt.new

Create ONLY the static web interface for an ESP32-based four-wheel Mecanum robot. Do not create the ESP32 firmware, server code, Node backend, Vite project, React app, package.json, build system, or deployment configuration. The ESP32 firmware already exists and serves static files from LittleFS.

## Absolute technical requirements

The final result must be plain static files that can be copied directly into the ESP32 project's `data/` directory and packed into a LittleFS image.

Required files:

- `index.html`
- `style.css`
- `app.js`
- `version.txt`

Optional local assets may be placed in `assets/`.

Do NOT use:

- React
- Vue
- Svelte
- Angular
- Vite
- npm packages
- TypeScript that needs compilation
- external CDNs
- Google Fonts
- external JavaScript libraries
- remote icons/images/fonts
- service workers
- WebSockets

The page must work fully offline because the phone/tablet connects directly to the ESP32 Wi-Fi access point and normally has no internet access.

Use only vanilla HTML, CSS and browser JavaScript.

All HTTP requests must use relative URLs such as `/api/control`. Never hardcode `192.168.4.1` into JavaScript.

Keep the total website reasonably small for ESP32 LittleFS. The LittleFS partition is approximately 960 KB.

## Product context

The ESP32 is installed in a small vehicle with four Mecanum wheels:

- FL = front left
- FR = front right
- RL = rear left
- RR = rear right

The ESP32 creates its own Wi-Fi access point. A phone or tablet opens the control page and controls the vehicle directly.

The UI is primarily intended for mobile phones/tablets in landscape mode, but it must also work well on desktop.

The control page needs two simultaneous touch joysticks. Multi-touch is required: the user must be able to hold one joystick with the left thumb and the second joystick with the right thumb at the same time.

Use Pointer Events and pointer capture, not separate mouse/touch implementations.

## Safety behavior - mandatory

This controls a real moving vehicle. Safety behavior is mandatory.

The page must have a prominent ARM/DISARM control.

Initial state after every page load:

- DISARMED
- both joysticks centered
- all directions and strengths zero

When disarmed, send `en=0`.

When armed, send `en=1`.

The page must send a COMPLETE control frame every 50 ms, even if neither joystick moves. This is the watchdog heartbeat expected by the ESP32.

If a request is already running, do not build up an unlimited request queue. Skip that heartbeat and send again on the next interval.

Immediately DISARM and zero both joysticks when possible if:

- the page becomes hidden
- `visibilitychange` reports hidden
- the page is left/unloaded
- a joystick receives `pointercancel`
- a joystick loses pointer capture

When a joystick pointer is released, that joystick must immediately return to center and its direction/strength must become 0.

Display connection state clearly:

- connected / disarmed
- connected / armed
- connection lost
- ESP error

Do not automatically re-arm after a connection loss or page reload.

## Joystick geometry

Both joysticks are circular and each is divided into 8 direction sectors of 45 degrees.

Direction numbering is fixed and MUST NOT be changed:

- 1 = up
- 2 = up-right
- 3 = right
- 4 = down-right
- 5 = down
- 6 = down-left
- 7 = left
- 8 = up-left
- 0 = centered / inactive

Strength is an integer from 0 to 100 based on distance from the center.

Use a small center dead area in the UI so tiny touch movements do not generate a direction. The firmware also has its own configurable deadzone.

Clamp the joystick knob visually to the circular joystick boundary.

Show the live direction number and strength percentage below each joystick for testing and commissioning.

## Stick 1 meaning

Label Stick 1 clearly as something like `Fahren / Drehen`.

The firmware interprets Stick 1 as:

- 1: forward
- 2: forward + right turn
- 3: rotate right
- 4: reverse + right turn
- 5: reverse
- 6: reverse + left turn
- 7: rotate left
- 8: forward + left turn

The website does NOT calculate motor PWM. It only sends direction and strength.

## Stick 2 meaning

Label Stick 2 clearly as something like `Seite / Diagonal`.

The firmware interprets Stick 2 as:

- 2: diagonal forward-right
- 3: strafe right
- 4: diagonal reverse-right
- 6: diagonal reverse-left
- 7: strafe left
- 8: diagonal forward-left

Zones 1 and 5 currently have no movement function in firmware. They may still be displayed as sectors, but if the user moves into those zones the browser must still send their real direction number. Do not remap them to another direction.

Both sticks may be active at the same time. The ESP32 firmware combines them.

## Control API contract

Endpoint:

`POST /api/control`

Content-Type:

`application/x-www-form-urlencoded`

Every request MUST contain all five fields:

- `s1_dir` = integer 0..8
- `s1_str` = integer 0..100
- `s2_dir` = integer 0..8
- `s2_str` = integer 0..100
- `en` = 0 or 1

Example armed control frame:

`s1_dir=1&s1_str=70&s2_dir=3&s2_str=20&en=1`

Example safe stop frame:

`s1_dir=0&s1_str=0&s2_dir=0&s2_str=0&en=0`

The ESP32 watchdog stops the motors if complete control packets stop arriving for about 300 ms, so the 50 ms browser heartbeat is mandatory.

A HTTP 200 response means accepted.

## Settings API

The UI must contain a settings/calibration section.

Read current settings with:

`GET /api/settings`

Response JSON example:

```json
{
  "schema": 1,
  "speedMode": 0,
  "startMin": 18,
  "boost": 82,
  "boostTime": 40,
  "deadzone": 8,
  "invertFL": 0,
  "invertFR": 0,
  "invertRL": 0,
  "invertRR": 0,
  "invertMoveY": 0,
  "invertMoveX": 0,
  "invertTurn": 0
}
```

Save settings with:

`POST /api/settings`

Content-Type:

`application/x-www-form-urlencoded`

Supported fields:

- `speedMode`: 0, 1 or 2
  - 0 = 100%
  - 1 = 80%
  - 2 = 60%
- `startMin`: 0..100
- `boost`: 0..100
- `boostTime`: 0..100
- `deadzone`: 0..30
- `invertFL`: 0/1
- `invertFR`: 0/1
- `invertRL`: 0/1
- `invertRR`: 0/1
- `invertMoveY`: 0/1
- `invertMoveX`: 0/1
- `invertTurn`: 0/1

Use clear controls, validation and useful explanations. Inversion values should be checkboxes/toggles, not numeric fields.

## Maintenance / update page

Do NOT build firmware-update or filesystem-update logic into this website.

The ESP32 firmware already provides a protected, firmware-embedded maintenance/recovery page at:

`/maintenance`

The main UI only needs a clearly accessible `Maintenance` link/button to that URL.

That firmware page provides:

- firmware `.bin` OTA upload
- LittleFS `.bin` upload
- SHA-256 verification
- settings export/import
- factory reset
- system information
- reboot

Do not duplicate these functions in the LittleFS website.

## Visual design

Make the interface look like a polished control panel rather than a generic form.

Requirements:

- dark UI suitable for a workshop/robot environment
- strong contrast
- large touch targets
- landscape-first control layout
- two large joysticks side by side on wide screens
- sensible stacked layout on narrow screens
- prominent vehicle ARM/DISARM state
- obvious connection/watchdog state
- clean settings panel
- no unnecessary animation that could reduce input responsiveness
- no scroll or selection behavior while manipulating the joysticks
- use only local CSS/assets

Avoid tiny controls because this will be used from phones/tablets while standing next to the vehicle.

## Version file

Create `version.txt` containing only the web UI version, for example:

`1.0.0`

The firmware reads this file and displays the installed website version independently of the firmware version.

## Delivery requirements

Return the final static files, not a mockup and not an explanation-only answer.

Before finishing, verify all of the following:

1. The project does not require npm or a build step.
2. Opening `index.html` does not depend on internet resources.
3. Both joysticks can be used simultaneously with Pointer Events.
4. Direction numbering exactly matches 1=up, 2=up-right, 3=right, 4=down-right, 5=down, 6=down-left, 7=left, 8=up-left.
5. A complete `/api/control` frame is attempted every 50 ms.
6. The page starts DISARMED.
7. Releasing/canceling a joystick returns that stick to zero.
8. Hiding/leaving the page disarms the vehicle as far as the browser allows.
9. Settings use the exact API field names above.
10. There is a link to `/maintenance` but no duplicate updater implementation.
11. All assets are local and the result can be copied directly into the ESP32 `data/` folder.
