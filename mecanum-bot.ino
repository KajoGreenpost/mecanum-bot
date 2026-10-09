#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Update.h>
#include <math.h>
#include <ctype.h>
#include <mbedtls/sha256.h>

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif

// =====================================================
// VERSION
// =====================================================

const char *FW_VERSION = "1.0.2";

// =====================================================
// WIFI ACCESS POINT
// =====================================================

const char *AP_SSID = "Mecanum-Bot";
const char *AP_PASSWORD = "Mecanum123";

// Change these before deployment.
const char *ADMIN_USER = "admin";
const char *ADMIN_PASSWORD = "ChangeMe123!";

IPAddress apIP(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

WebServer server(80);
Preferences preferences;

// =====================================================
// OPTIONAL HARDWARE RECOVERY INPUT
// =====================================================
//
// Set RECOVERY_PIN to a free GPIO if you want a physical
// recovery switch. Keep it at -1 if you do not use one.
//
// When enabled, hold the pin LOW while the ESP32 starts.
// The controller then keeps all motors disabled and serves
// only recovery/maintenance functionality.

const int RECOVERY_PIN = -1;
bool recoveryMode = false;

// =====================================================
// MOTOR PINS
// =====================================================

// Calibrated from the reported Y+, turn+, X+ wheel tests (all inversions off).
// Physical FL = old RL reversed, FR = old RR, RL = old FR reversed, RR = old FL.
// Swapping the left IN pairs corrects polarity without requiring UI inversions.
const int FL_IN1 = 4;
const int FL_IN2 = 2;
const int FR_IN1 = 16;
const int FR_IN2 = 17;
const int RL_IN1 = 21;
const int RL_IN2 = 19;
const int RR_IN1 = 5;
const int RR_IN2 = 18;
const int STBY_PIN = 27;

// =====================================================
// PWM
// =====================================================

const int PWM_FREQ = 15000;
const int PWM_RES = 8;
const int PWM_MAX = 255;

const int CH_FL_1 = 0;
const int CH_FL_2 = 1;
const int CH_FR_1 = 2;
const int CH_FR_2 = 3;
const int CH_RL_1 = 4;
const int CH_RL_2 = 5;
const int CH_RR_1 = 6;
const int CH_RR_2 = 7;

// =====================================================
// CONTROL VALUES
// =====================================================

int movey = 0;
int movex = 0;
int turn = 0;
int car_enable = 0;

// 0 = 100%, 1 = 80%, 2 = 60%
int speedMode = 0;

// =====================================================
// MOTOR SETTINGS
// =====================================================

int startMin = 18;
int boost = 82;
int boostTime = 40;
int deadzone = 8;

int invertFL = 0;
int invertFR = 0;
int invertRL = 0;
int invertRR = 0;

int invertMoveY = 0;
int invertMoveX = 0;
int invertTurn = 0;

// =====================================================
// FAILSAFE / MOTOR LOOP
// =====================================================

const unsigned long COMMAND_TIMEOUT_MS = 300;
const unsigned long CONTROL_INTERVAL_MS = 10;

unsigned long lastControlPacketMs = 0;
unsigned long lastControlUpdateMs = 0;
bool validControlPacketReceived = false;

// Bounded diagnostics: no flash writes, no logging while disabled.
const size_t DEBUG_LINES = 32;
const size_t DEBUG_LINE_LENGTH = 160;
const unsigned long DEBUG_LEASE_MS = 10000;
struct DebugEntry { uint32_t id; unsigned long ms; char text[DEBUG_LINE_LENGTH]; };
DebugEntry debugEntries[DEBUG_LINES];
uint32_t debugSequence = 0;
uint32_t debugBootId = 0;
bool debugEnabled = false;
unsigned long debugLastReadMs = 0;
unsigned long debugLastStateMs = 0;
int debugPWM[4] = {0, 0, 0, 0};

void debugLog(const char *message) {
  if (!debugEnabled) return;
  DebugEntry &entry = debugEntries[debugSequence % DEBUG_LINES];
  entry.id = ++debugSequence;
  entry.ms = millis();
  snprintf(entry.text, sizeof(entry.text), "%s", message);
}

void expireDebug() {
  if (debugEnabled && millis() - debugLastReadMs > DEBUG_LEASE_MS) {
    debugLog("DEBUG stopped: no diagnostic reader for 10 seconds");
    debugEnabled = false;
  }
}

// =====================================================
// BOOST STATE
// =====================================================

unsigned long globalBoostUntilMs = 0;
bool vehicleWasMoving = false;

// =====================================================
// FILESYSTEM / UPDATE STATE
// =====================================================

bool fsMounted = false;
bool updateInProgress = false;
bool updateStarted = false;
bool updateFailed = false;
bool updateSuccess = false;

String updateMessage = "";
String expectedUpdateHash = "";
String calculatedUpdateHash = "";
int activeUpdateCommand = -1;
unsigned long uploadedBytes = 0;

mbedtls_sha256_context sha256Context;
bool sha256Active = false;

bool rebootPending = false;
unsigned long rebootAtMs = 0;

// =====================================================
// EMBEDDED MAINTENANCE / RECOVERY PAGE
// =====================================================
// This page remains available even if LittleFS is broken.

const char MAINTENANCE_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mecanum Bot Maintenance</title>
<style>
*{box-sizing:border-box}body{margin:0;padding:20px;font-family:Arial,sans-serif;background:#161616;color:#f5f5f5}main{max-width:900px;margin:0 auto}h1{margin-top:0}.card{background:#252525;border:1px solid #3b3b3b;border-radius:12px;padding:18px;margin-bottom:18px}button{padding:11px 15px;border:0;border-radius:7px;cursor:pointer;font-size:14px;margin:4px}input[type=file]{width:100%;margin:10px 0}progress{width:100%;height:22px}pre{white-space:pre-wrap;word-break:break-word;background:#111;padding:10px;border-radius:7px}.ok{color:#6cff7b}.error{color:#ff6b6b}.small{font-size:12px;color:#bbb}a{color:#79bfff}
</style>
</head>
<body>
<main>
<h1>Mecanum Bot Maintenance</h1>
<div class="card">
<h2>System</h2>
<pre id="systemInfo">Lade...</pre>
<button onclick="loadSystemInfo()">Aktualisieren</button>
<button onclick="rebootDevice()">ESP32 neu starten</button>
<p><a href="/">Zur Fahrzeugsteuerung</a></p>
</div>
<div class="card">
<h2>Firmware Update</h2>
<p>Hier nur die eigentliche OTA Firmware-BIN hochladen.</p>
<input type="file" id="firmwareFile" accept=".bin">
<button onclick="uploadFirmware()">Firmware installieren</button>
<progress id="firmwareProgress" value="0" max="100"></progress>
<pre id="firmwareStatus">Bereit.</pre>
</div>
<div class="card">
<h2>Webinterface Update</h2>
<p>Hier nur das erzeugte LittleFS Image littlefs.bin hochladen.</p>
<input type="file" id="filesystemFile" accept=".bin">
<button onclick="uploadFilesystem()">Webinterface installieren</button>
<progress id="filesystemProgress" value="0" max="100"></progress>
<pre id="filesystemStatus">Bereit.</pre>
</div>
<div class="card">
<h2>Einstellungen</h2>
<button onclick="exportSettings()">Einstellungen exportieren</button>
<input type="file" id="settingsFile" accept=".json,application/json">
<button onclick="importSettings()">Einstellungen importieren</button>
<button onclick="factoryReset()">Einstellungen auf Werkseinstellungen</button>
<pre id="settingsStatus">Bereit.</pre>
</div>
<div class="card">
<h2>Hinweise</h2>
<p class="small">SHA-256 wird vor jedem Update im Browser berechnet. Der ESP32 berechnet beim Empfang denselben SHA-256 erneut. Nur wenn beide Werte identisch sind, wird das Update abgeschlossen.</p>
<p class="small">Firmware und Webinterface werden bewusst getrennt aktualisiert.</p>
</div>
</main>
<script>
function rotr(n,x){return(x>>>n)|(x<<(32-n));}
function sha256Fallback(buffer){
  const bytes=new Uint8Array(buffer);const words=[];for(let i=0;i<bytes.length;i++)words[i>>2]|=bytes[i]<<(24-(i%4)*8);words[bytes.length>>2]|=0x80<<(24-(bytes.length%4)*8);words[(((bytes.length+8)>>6)+1)*16-1]=bytes.length*8;
  const k=[0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
  let h=[0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
  const w=new Array(64);
  for(let j=0;j<words.length;j+=16){for(let i=0;i<16;i++)w[i]=words[j+i]|0;for(let i=16;i<64;i++){const s0=rotr(7,w[i-15])^rotr(18,w[i-15])^(w[i-15]>>>3);const s1=rotr(17,w[i-2])^rotr(19,w[i-2])^(w[i-2]>>>10);w[i]=(w[i-16]+s0+w[i-7]+s1)|0;}let a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];for(let i=0;i<64;i++){const S1=rotr(6,e)^rotr(11,e)^rotr(25,e);const ch=(e&f)^(~e&g);const t1=(hh+S1+ch+k[i]+w[i])|0;const S0=rotr(2,a)^rotr(13,a)^rotr(22,a);const maj=(a&b)^(a&c)^(b&c);const t2=(S0+maj)|0;hh=g;g=f;f=e;e=(d+t1)|0;d=c;c=b;b=a;a=(t1+t2)|0;}h[0]=(h[0]+a)|0;h[1]=(h[1]+b)|0;h[2]=(h[2]+c)|0;h[3]=(h[3]+d)|0;h[4]=(h[4]+e)|0;h[5]=(h[5]+f)|0;h[6]=(h[6]+g)|0;h[7]=(h[7]+hh)|0;}
  return h.map(v=>(v>>>0).toString(16).padStart(8,'0')).join('');
}
async function calculateSHA256(file){const data=await file.arrayBuffer();if(window.crypto&&window.crypto.subtle){try{const digest=await window.crypto.subtle.digest('SHA-256',data);return Array.from(new Uint8Array(digest)).map(b=>b.toString(16).padStart(2,'0')).join('');}catch(e){}}return sha256Fallback(data);}
async function uploadBinary(inputId,url,progressId,statusId){const input=document.getElementById(inputId);const progress=document.getElementById(progressId);const status=document.getElementById(statusId);status.className='';if(!input.files.length){status.textContent='Keine BIN-Datei ausgewaehlt.';return;}const file=input.files[0];if(!file.name.toLowerCase().endsWith('.bin')){status.textContent='Datei muss auf .bin enden.';return;}progress.value=0;status.textContent='Berechne SHA-256...';let hash;try{hash=await calculateSHA256(file);}catch(err){status.textContent='SHA-256 Fehler: '+err;return;}status.textContent='SHA-256: '+hash+'\n\nUpload startet...';const form=new FormData();form.append('update',file,file.name);const xhr=new XMLHttpRequest();xhr.open('POST',url+'?sha256='+encodeURIComponent(hash));xhr.upload.onprogress=function(event){if(event.lengthComputable)progress.value=Math.round((event.loaded/event.total)*100);};xhr.onload=function(){progress.value=100;status.textContent=xhr.responseText;if(xhr.status>=200&&xhr.status<300)status.className='ok';else status.className='error';};xhr.onerror=function(){status.className='error';status.textContent='Netzwerkfehler beim Upload.';};xhr.send(form);}
function uploadFirmware(){if(!confirm('Firmware wirklich installieren? Die Motoren werden abgeschaltet und der ESP32 startet danach neu.'))return;uploadBinary('firmwareFile','/api/update/firmware','firmwareProgress','firmwareStatus');}
function uploadFilesystem(){if(!confirm('Webinterface wirklich ersetzen?'))return;uploadBinary('filesystemFile','/api/update/filesystem','filesystemProgress','filesystemStatus');}
async function loadSystemInfo(){const output=document.getElementById('systemInfo');try{const response=await fetch('/api/system',{cache:'no-store'});output.textContent=await response.text();}catch(err){output.textContent='Fehler: '+err;}}
function exportSettings(){window.location.href='/api/settings/export';}
async function importSettings(){const input=document.getElementById('settingsFile');const status=document.getElementById('settingsStatus');status.className='';if(!input.files.length){status.textContent='Keine JSON-Datei ausgewaehlt.';return;}try{const text=await input.files[0].text();const response=await fetch('/api/settings/import',{method:'POST',headers:{'Content-Type':'application/json'},body:text});status.textContent=await response.text();status.className=response.ok?'ok':'error';}catch(err){status.className='error';status.textContent='Fehler: '+err;}}
async function factoryReset(){if(!confirm('Alle gespeicherten Motor-Einstellungen wirklich zuruecksetzen?'))return;const status=document.getElementById('settingsStatus');try{const response=await fetch('/api/settings/reset',{method:'POST'});status.textContent=await response.text();status.className=response.ok?'ok':'error';}catch(err){status.className='error';status.textContent='Fehler: '+err;}}
async function rebootDevice(){if(!confirm('ESP32 wirklich neu starten?'))return;try{await fetch('/api/reboot',{method:'POST'});}catch(err){}document.getElementById('systemInfo').textContent='ESP32 startet neu...';}
loadSystemInfo();
</script>
</body>
</html>
)rawliteral";

// =====================================================
// BASIC HELPERS
// =====================================================

int clampInt(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

void setDefaultSettings() {
  speedMode = 0;
  startMin = 18;
  boost = 82;
  boostTime = 40;
  deadzone = 8;
  invertFL = 0;
  invertFR = 0;
  invertRL = 0;
  invertRR = 0;
  invertMoveY = 0;
  invertMoveX = 0;
  invertTurn = 0;
}

void loadSettings() {
  setDefaultSettings();
  preferences.begin("car-settings", true);
  speedMode = preferences.getInt("speedMode", speedMode);

  if (preferences.isKey("startMin")) {
    startMin = preferences.getInt("startMin", startMin);
  } else if (preferences.isKey("smMin")) {
    startMin = preferences.getInt("smMin", startMin);
  }

  boost = preferences.getInt("boost", preferences.getInt("bst", boost));
  boostTime = preferences.getInt("boostTime", preferences.getInt("bTime", boostTime));
  deadzone = preferences.getInt("deadzone", preferences.getInt("dz", deadzone));
  invertFL = preferences.getInt("invFL", invertFL);
  invertFR = preferences.getInt("invFR", invertFR);
  invertRL = preferences.getInt("invRL", invertRL);
  invertRR = preferences.getInt("invRR", invertRR);
  invertMoveY = preferences.getInt("invMY", invertMoveY);
  invertMoveX = preferences.getInt("invMX", invertMoveX);
  invertTurn = preferences.getInt("invT", invertTurn);
  preferences.end();

  speedMode = clampInt(speedMode, 0, 2);
  startMin = clampInt(startMin, 0, 100);
  boost = clampInt(boost, 0, 100);
  boostTime = clampInt(boostTime, 0, 100);
  deadzone = clampInt(deadzone, 0, 30);
}

void saveSettings() {
  preferences.begin("car-settings", false);
  preferences.putInt("speedMode", speedMode);
  preferences.putInt("startMin", startMin);
  preferences.putInt("smMin", startMin);
  preferences.putInt("boost", boost);
  preferences.putInt("bst", boost);
  preferences.putInt("boostTime", boostTime);
  preferences.putInt("bTime", boostTime);
  preferences.putInt("deadzone", deadzone);
  preferences.putInt("dz", deadzone);
  preferences.putInt("invFL", invertFL);
  preferences.putInt("invFR", invertFR);
  preferences.putInt("invRL", invertRL);
  preferences.putInt("invRR", invertRR);
  preferences.putInt("invMY", invertMoveY);
  preferences.putInt("invMX", invertMoveX);
  preferences.putInt("invT", invertTurn);
  preferences.end();
}

float getSpeedFactor(int mode) {
  switch (mode) {
    case 0: return 1.0f;
    case 1: return 0.8f;
    case 2: return 0.6f;
    default: return 1.0f;
  }
}

int applyDeadzone(int value, int dz) {
  value = clampInt(value, -100, 100);
  dz = clampInt(dz, 0, 30);
  int magnitude = abs(value);
  if (magnitude <= dz) return 0;
  int usableRange = 100 - dz;
  if (usableRange <= 0) return 0;
  int scaled = ((magnitude - dz) * 100) / usableRange;
  scaled = clampInt(scaled, 0, 100);
  return value < 0 ? -scaled : scaled;
}

float shapeInput(float value) {
  value = clampFloat(value, 0.0f, 1.0f);
  return 0.35f * value + 0.65f * value * value;
}

float getInputMagnitude(float y, float x, float t) {
  float result = fabsf(y);
  if (fabsf(x) > result) result = fabsf(x);
  if (fabsf(t) > result) result = fabsf(t);
  return result;
}

float getMaxWheelMagnitude(float fl, float fr, float rl, float rr) {
  float result = fabsf(fl);
  if (fabsf(fr) > result) result = fabsf(fr);
  if (fabsf(rl) > result) result = fabsf(rl);
  if (fabsf(rr) > result) result = fabsf(rr);
  return result;
}

int normalizedToPWM(float value) {
  value = clampFloat(value, -1.0f, 1.0f);
  return (int)roundf(value * PWM_MAX);
}

void stopAllMotors() {
  for (int i = 0; i < 4; i++) debugPWM[i] = 0;
  ledcWrite(CH_FL_1, 0);
  ledcWrite(CH_FL_2, 0);
  ledcWrite(CH_FR_1, 0);
  ledcWrite(CH_FR_2, 0);
  ledcWrite(CH_RL_1, 0);
  ledcWrite(CH_RL_2, 0);
  ledcWrite(CH_RR_1, 0);
  ledcWrite(CH_RR_2, 0);
  digitalWrite(STBY_PIN, LOW);
  globalBoostUntilMs = 0;
  vehicleWasMoving = false;
}

void driveOneMotor(int channel1, int channel2, int speedValue, bool invert) {
  speedValue = clampInt(speedValue, -PWM_MAX, PWM_MAX);
  if (invert) speedValue = -speedValue;

  if (speedValue > 0) {
    ledcWrite(channel1, speedValue);
    ledcWrite(channel2, 0);
  } else if (speedValue < 0) {
    ledcWrite(channel1, 0);
    ledcWrite(channel2, -speedValue);
  } else {
    ledcWrite(channel1, 0);
    ledcWrite(channel2, 0);
  }
}

void applyStartMinimum(float &fl, float &fr, float &rl, float &rr, float maximumAllowed) {
  float currentMaximum = getMaxWheelMagnitude(fl, fr, rl, rr);
  if (currentMaximum <= 0.0f) return;

  float minimum = clampFloat(startMin / 100.0f, 0.0f, 1.0f);
  if (minimum > maximumAllowed) minimum = maximumAllowed;
  if (currentMaximum >= minimum) return;

  float scale = minimum / currentMaximum;
  fl *= scale;
  fr *= scale;
  rl *= scale;
  rr *= scale;
}

void applyGlobalBoost(float &fl, float &fr, float &rl, float &rr, float maximumAllowed) {
  float currentMaximum = getMaxWheelMagnitude(fl, fr, rl, rr);
  bool moving = currentMaximum > 0.001f;
  unsigned long now = millis();

  if (moving && !vehicleWasMoving) {
    unsigned long duration = ((unsigned long)clampInt(boostTime, 0, 100) * 300UL) / 100UL;
    globalBoostUntilMs = now + duration;
  }

  vehicleWasMoving = moving;

  if (!moving) {
    globalBoostUntilMs = 0;
    return;
  }

  if (now >= globalBoostUntilMs) return;

  float boostLevel = clampFloat(boost / 100.0f, 0.0f, 1.0f);
  if (boostLevel > maximumAllowed) boostLevel = maximumAllowed;
  if (currentMaximum >= boostLevel) return;

  float scale = boostLevel / currentMaximum;
  fl *= scale;
  fr *= scale;
  rl *= scale;
  rr *= scale;
}

void driveMecanum() {
  if (recoveryMode || updateInProgress || !car_enable) {
    stopAllMotors();
    return;
  }

  int mY = applyDeadzone(movey, deadzone);
  int mX = applyDeadzone(movex, deadzone);
  int tCmd = applyDeadzone(turn, deadzone);

  if (invertMoveY) mY = -mY;
  if (invertMoveX) mX = -mX;
  if (invertTurn) tCmd = -tCmd;

  float fY = mY / 100.0f;
  float fX = mX / 100.0f;
  float fT = tCmd / 100.0f;

  float commandMagnitude = getInputMagnitude(fY, fX, fT);
  if (commandMagnitude <= 0.0f) {
    stopAllMotors();
    return;
  }

  float directionY = fY / commandMagnitude;
  float directionX = fX / commandMagnitude;
  float directionT = fT / commandMagnitude;

  float fl = directionY + directionX + directionT;
  float fr = directionY - directionX - directionT;
  float rl = directionY - directionX + directionT;
  float rr = directionY + directionX - directionT;

  float wheelMaximum = getMaxWheelMagnitude(fl, fr, rl, rr);
  if (wheelMaximum > 1.0f) {
    fl /= wheelMaximum;
    fr /= wheelMaximum;
    rl /= wheelMaximum;
    rr /= wheelMaximum;
  }

  float shapedMagnitude = shapeInput(commandMagnitude);
  fl *= shapedMagnitude;
  fr *= shapedMagnitude;
  rl *= shapedMagnitude;
  rr *= shapedMagnitude;

  float speedFactor = getSpeedFactor(speedMode);
  fl *= speedFactor;
  fr *= speedFactor;
  rl *= speedFactor;
  rr *= speedFactor;

  applyStartMinimum(fl, fr, rl, rr, speedFactor);
  // Breakaway torque is independent of the selected running power limit.
  // Only the short configured boost may exceed 60%/80%; afterwards the cap resumes.
  applyGlobalBoost(fl, fr, rl, rr, 1.0f);
  bool boostActive = vehicleWasMoving && (int32_t)(globalBoostUntilMs - millis()) > 0;
  float outputLimit = boostActive ? fmaxf(speedFactor, clampFloat(boost / 100.0f, 0.0f, 1.0f)) : speedFactor;

  fl = clampFloat(fl, -outputLimit, outputLimit);
  fr = clampFloat(fr, -outputLimit, outputLimit);
  rl = clampFloat(rl, -outputLimit, outputLimit);
  rr = clampFloat(rr, -outputLimit, outputLimit);

  digitalWrite(STBY_PIN, HIGH);
  debugPWM[0] = normalizedToPWM(fl) * (invertFL ? -1 : 1);
  debugPWM[1] = normalizedToPWM(fr) * (invertFR ? -1 : 1);
  debugPWM[2] = normalizedToPWM(rl) * (invertRL ? -1 : 1);
  debugPWM[3] = normalizedToPWM(rr) * (invertRR ? -1 : 1);
  driveOneMotor(CH_FL_1, CH_FL_2, normalizedToPWM(fl), invertFL);
  driveOneMotor(CH_FR_1, CH_FR_2, normalizedToPWM(fr), invertFR);
  driveOneMotor(CH_RL_1, CH_RL_2, normalizedToPWM(rl), invertRL);
  driveOneMotor(CH_RR_1, CH_RR_2, normalizedToPWM(rr), invertRR);
}

void checkFailsafe() {
  if (!car_enable) return;

  if (!validControlPacketReceived || millis() - lastControlPacketMs > COMMAND_TIMEOUT_MS) {
    debugLog("FAILSAFE: control timeout; motors disabled");
    movey = 0;
    movex = 0;
    turn = 0;
    car_enable = 0;
    stopAllMotors();
  }
}

// =====================================================
// JOYSTICK MAPPING
// =====================================================

void mapStick1(int direction, int strength, int &y, int &t) {
  y = 0;
  t = 0;
  strength = clampInt(strength, 0, 100);

  switch (direction) {
    case 1: y = strength; break;
    case 2: y = strength; t = strength; break;
    case 3: t = strength; break;
    case 4: y = -strength; t = strength; break;
    case 5: y = -strength; break;
    case 6: y = -strength; t = -strength; break;
    case 7: t = -strength; break;
    case 8: y = strength; t = -strength; break;
    default: break;
  }
}

void mapStick2(int direction, int strength, int &y, int &x) {
  y = 0;
  x = 0;
  strength = clampInt(strength, 0, 100);

  switch (direction) {
    case 2: y = strength; x = strength; break;
    case 3: x = strength; break;
    case 4: y = -strength; x = strength; break;
    case 6: y = -strength; x = -strength; break;
    case 7: x = -strength; break;
    case 8: y = strength; x = -strength; break;
    default: break;
  }
}

// =====================================================
// AUTH / STATIC FILES
// =====================================================

bool requireMaintenanceAuth() {
  if (server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

String getContentType(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".txt")) return "text/plain";
  return "application/octet-stream";
}

bool serveFile(String path) {
  if (!fsMounted) return false;
  if (path.indexOf("..") >= 0) return false;
  if (path.endsWith("/")) path += "index.html";
  if (!LittleFS.exists(path)) return false;

  File file = LittleFS.open(path, "r");
  if (!file) return false;

  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.streamFile(file, getContentType(path));
  file.close();
  return true;
}

String readWebVersion() {
  if (!fsMounted || !LittleFS.exists("/version.txt")) return "not-installed";
  File file = LittleFS.open("/version.txt", "r");
  if (!file) return "unknown";
  String version = file.readString();
  file.close();
  version.trim();
  return version.length() ? version : "unknown";
}

// =====================================================
// CONTROL API
// =====================================================

void handleControl() {
  if (recoveryMode) {
    server.send(423, "text/plain", "Recovery mode active");
    return;
  }

  if (updateInProgress) {
    server.send(423, "text/plain", "Update in progress");
    return;
  }

  if (!server.hasArg("s1_dir") || !server.hasArg("s1_str") ||
      !server.hasArg("s2_dir") || !server.hasArg("s2_str") ||
      !server.hasArg("en")) {
    server.send(400, "text/plain", "Incomplete control frame");
    debugLog("CONTROL rejected: incomplete frame");
    return;
  }

  int s1Dir = clampInt(server.arg("s1_dir").toInt(), 0, 8);
  int s1Strength = clampInt(server.arg("s1_str").toInt(), 0, 100);
  int s2Dir = clampInt(server.arg("s2_dir").toInt(), 0, 8);
  int s2Strength = clampInt(server.arg("s2_str").toInt(), 0, 100);
  int enable = server.arg("en").toInt() != 0;

  int stick1Y = 0;
  int stick1Turn = 0;
  int stick2Y = 0;
  int stick2X = 0;

  mapStick1(s1Dir, s1Strength, stick1Y, stick1Turn);
  mapStick2(s2Dir, s2Strength, stick2Y, stick2X);

  movey = clampInt(stick1Y + stick2Y, -100, 100);
  movex = clampInt(stick2X, -100, 100);
  turn = clampInt(stick1Turn, -100, 100);
  if (car_enable != enable) debugLog(enable ? "CONTROL: motors armed" : "CONTROL: motors disarmed");
  car_enable = enable;

  if (!car_enable) {
    movey = 0;
    movex = 0;
    turn = 0;
  }

  lastControlPacketMs = millis();
  validControlPacketReceived = true;
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// =====================================================
// SETTINGS JSON / API
// =====================================================

String buildSettingsJson() {
  String json;
  json.reserve(512);
  json += "{";
  json += "\"schema\":1,";
  json += "\"speedMode\":" + String(speedMode) + ",";
  json += "\"startMin\":" + String(startMin) + ",";
  json += "\"boost\":" + String(boost) + ",";
  json += "\"boostTime\":" + String(boostTime) + ",";
  json += "\"deadzone\":" + String(deadzone) + ",";
  json += "\"invertFL\":" + String(invertFL) + ",";
  json += "\"invertFR\":" + String(invertFR) + ",";
  json += "\"invertRL\":" + String(invertRL) + ",";
  json += "\"invertRR\":" + String(invertRR) + ",";
  json += "\"invertMoveY\":" + String(invertMoveY) + ",";
  json += "\"invertMoveX\":" + String(invertMoveX) + ",";
  json += "\"invertTurn\":" + String(invertTurn);
  json += "}";
  return json;
}

void handleSettingsGet() {
  server.send(200, "application/json", buildSettingsJson());
}

void handleSettingsPost() {
  if (server.hasArg("speedMode")) speedMode = clampInt(server.arg("speedMode").toInt(), 0, 2);
  if (server.hasArg("startMin")) startMin = clampInt(server.arg("startMin").toInt(), 0, 100);
  if (server.hasArg("boost")) boost = clampInt(server.arg("boost").toInt(), 0, 100);
  if (server.hasArg("boostTime")) boostTime = clampInt(server.arg("boostTime").toInt(), 0, 100);
  if (server.hasArg("deadzone")) deadzone = clampInt(server.arg("deadzone").toInt(), 0, 30);
  if (server.hasArg("invertFL")) invertFL = server.arg("invertFL").toInt() != 0;
  if (server.hasArg("invertFR")) invertFR = server.arg("invertFR").toInt() != 0;
  if (server.hasArg("invertRL")) invertRL = server.arg("invertRL").toInt() != 0;
  if (server.hasArg("invertRR")) invertRR = server.arg("invertRR").toInt() != 0;
  if (server.hasArg("invertMoveY")) invertMoveY = server.arg("invertMoveY").toInt() != 0;
  if (server.hasArg("invertMoveX")) invertMoveX = server.arg("invertMoveX").toInt() != 0;
  if (server.hasArg("invertTurn")) invertTurn = server.arg("invertTurn").toInt() != 0;
  saveSettings();
  debugLog("SETTINGS saved");
  server.send(200, "application/json", buildSettingsJson());
}

// Incremental log reads are capped at eight lines per HTTP response.
void handleDebugGet() {
  expireDebug();
  if (debugEnabled) debugLastReadMs = millis();
  uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
  if (after > debugSequence) after = 0; // device restarted
  uint32_t oldest = debugSequence > DEBUG_LINES ? debugSequence - DEBUG_LINES + 1 : 1;
  uint32_t first = after + 1;
  uint32_t dropped = first < oldest ? oldest - first : 0;
  if (first < oldest) first = oldest;
  uint32_t next = after;
  String json;
  json.reserve(1800);
  json += "{\"enabled\":";
  json += debugEnabled ? "true" : "false";
  json += ",\"bootId\":" + String(debugBootId) + ",\"dropped\":" + String(dropped) + ",\"lines\":[";
  int count = 0;
  for (uint32_t id = first; id <= debugSequence && count < 8; id++, count++) {
    DebugEntry &entry = debugEntries[(id - 1) % DEBUG_LINES];
    if (count) json += ',';
    json += "{\"id\":" + String(id) + ",\"ms\":" + String(entry.ms) + ",\"text\":\"";
    for (const char *p = entry.text; *p; p++) {
      if (*p == '"' || *p == '\\') json += '\\';
      if ((unsigned char)*p >= 32) json += *p;
    }
    json += "\"}";
    next = id;
  }
  json += "],\"next\":" + String(next) + "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleDebugPost() {
  if (!server.hasArg("enabled") || (server.arg("enabled") != "0" && server.arg("enabled") != "1")) {
    server.send(400, "text/plain", "enabled must be 0 or 1");
    return;
  }
  bool enable = server.arg("enabled") == "1";
  if (enable && !debugEnabled) {
    debugEnabled = true;
    char message[DEBUG_LINE_LENGTH];
    snprintf(message, sizeof(message), "DEBUG enabled; firmware=%s heap=%u recovery=%d", FW_VERSION, ESP.getFreeHeap(), recoveryMode);
    debugLog(message);
    snprintf(message, sizeof(message), "SETTINGS speed=%d start=%d boost=%d boostTime=%d deadzone=%d", speedMode, startMin, boost, boostTime, deadzone);
    debugLog(message);
    snprintf(message, sizeof(message), "INVERT motors FL=%d FR=%d RL=%d RR=%d axes Y=%d X=%d turn=%d", invertFL, invertFR, invertRL, invertRR, invertMoveY, invertMoveX, invertTurn);
    debugLog(message);
    debugLastStateMs = millis() - 1000;
  } else if (!enable && debugEnabled) {
    debugLog("DEBUG disabled");
    debugEnabled = false;
  }
  debugLastReadMs = millis();
  handleDebugGet();
}

void recordDebugState() {
  expireDebug();
  if (!debugEnabled || millis() - debugLastStateMs < 1000) return;
  debugLastStateMs = millis();
  char message[DEBUG_LINE_LENGTH];
  snprintf(message, sizeof(message), "STATE en=%d Y=%d X=%d turn=%d PWM FL=%d FR=%d RL=%d RR=%d heap=%u wifi=%u age=%lu ms",
    car_enable, movey, movex, turn, debugPWM[0], debugPWM[1], debugPWM[2], debugPWM[3],
    ESP.getFreeHeap(), WiFi.softAPgetStationNum(), millis() - lastControlPacketMs);
  debugLog(message);
}

bool extractJsonInt(const String &json, const char *key, int &result) {
  String search = "\"" + String(key) + "\"";
  int position = json.indexOf(search);
  if (position < 0) return false;

  position = json.indexOf(':', position + search.length());
  if (position < 0) return false;
  position++;

  while (position < json.length() && isspace((unsigned char)json[position])) position++;

  bool quoted = false;
  if (position < json.length() && json[position] == '"') {
    quoted = true;
    position++;
  }

  int end = position;
  if (end < json.length() && (json[end] == '-' || json[end] == '+')) end++;
  while (end < json.length() && isdigit((unsigned char)json[end])) end++;
  if (end <= position) return false;

  String number = json.substring(position, end);

  if (quoted) {
    while (end < json.length() && isspace((unsigned char)json[end])) end++;
    if (end >= json.length() || json[end] != '"') return false;
  }

  result = number.toInt();
  return true;
}

void handleSettingsExport() {
  if (!requireMaintenanceAuth()) return;
  server.sendHeader("Content-Disposition", "attachment; filename=\"mecanum-settings.json\"");
  server.send(200, "application/json", buildSettingsJson());
}

void handleSettingsImport() {
  if (!requireMaintenanceAuth()) return;

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No JSON body received");
    return;
  }

  String json = server.arg("plain");
  int value = 0;
  int valuesFound = 0;

  if (extractJsonInt(json, "speedMode", value)) { speedMode = clampInt(value, 0, 2); valuesFound++; }
  if (extractJsonInt(json, "startMin", value)) { startMin = clampInt(value, 0, 100); valuesFound++; }
  if (extractJsonInt(json, "boost", value)) { boost = clampInt(value, 0, 100); valuesFound++; }
  if (extractJsonInt(json, "boostTime", value)) { boostTime = clampInt(value, 0, 100); valuesFound++; }
  if (extractJsonInt(json, "deadzone", value)) { deadzone = clampInt(value, 0, 30); valuesFound++; }
  if (extractJsonInt(json, "invertFL", value)) { invertFL = value != 0; valuesFound++; }
  if (extractJsonInt(json, "invertFR", value)) { invertFR = value != 0; valuesFound++; }
  if (extractJsonInt(json, "invertRL", value)) { invertRL = value != 0; valuesFound++; }
  if (extractJsonInt(json, "invertRR", value)) { invertRR = value != 0; valuesFound++; }
  if (extractJsonInt(json, "invertMoveY", value)) { invertMoveY = value != 0; valuesFound++; }
  if (extractJsonInt(json, "invertMoveX", value)) { invertMoveX = value != 0; valuesFound++; }
  if (extractJsonInt(json, "invertTurn", value)) { invertTurn = value != 0; valuesFound++; }

  if (valuesFound == 0) {
    server.send(400, "text/plain", "No valid settings found");
    return;
  }

  car_enable = 0;
  stopAllMotors();
  saveSettings();
  server.send(200, "text/plain", "Settings imported successfully.");
}

void handleSettingsReset() {
  if (!requireMaintenanceAuth()) return;
  car_enable = 0;
  stopAllMotors();

  preferences.begin("car-settings", false);
  preferences.clear();
  preferences.end();

  setDefaultSettings();
  saveSettings();
  server.send(200, "text/plain", "Settings reset to defaults.");
}

// =====================================================
// SYSTEM INFO
// =====================================================

void handleSystemInfo() {
  if (!requireMaintenanceAuth()) return;

  String output;
  output.reserve(800);
  output += "Firmware: " + String(FW_VERSION);
  output += "\nWebinterface: " + readWebVersion();
  output += "\nIP: " + WiFi.softAPIP().toString();
  output += "\nConnected clients: " + String(WiFi.softAPgetStationNum());
  output += "\nUptime: " + String(millis() / 1000UL) + " s";
  output += "\nFree heap: " + String(ESP.getFreeHeap()) + " bytes";
  output += "\nFlash size: " + String(ESP.getFlashChipSize()) + " bytes";
  output += "\nSketch size: " + String(ESP.getSketchSize()) + " bytes";
  output += "\nFree sketch space: " + String(ESP.getFreeSketchSpace()) + " bytes";
  output += "\nLittleFS mounted: " + String(fsMounted ? "yes" : "no");
  output += "\nRecovery mode: " + String(recoveryMode ? "yes" : "no");

  if (fsMounted) {
    output += "\nLittleFS total: " + String(LittleFS.totalBytes()) + " bytes";
    output += "\nLittleFS used: " + String(LittleFS.usedBytes()) + " bytes";
  }

  output += "\nVehicle enabled: " + String(car_enable ? "yes" : "no");
  output += "\nUpdate running: " + String(updateInProgress ? "yes" : "no");

  server.send(200, "text/plain", output);
}

// =====================================================
// SHA-256 / UPDATE
// =====================================================

bool validSha256String(const String &hash) {
  if (hash.length() != 64) return false;
  for (int i = 0; i < 64; i++) {
    if (!isxdigit((unsigned char)hash[i])) return false;
  }
  return true;
}

String sha256ToHex(const unsigned char *hash) {
  static const char hex[] = "0123456789abcdef";
  String result;
  result.reserve(64);

  for (int i = 0; i < 32; i++) {
    result += hex[(hash[i] >> 4) & 0x0F];
    result += hex[hash[i] & 0x0F];
  }

  return result;
}

void resetUpdateState() {
  updateStarted = false;
  updateFailed = false;
  updateSuccess = false;
  updateMessage = "";
  expectedUpdateHash = "";
  calculatedUpdateHash = "";
  uploadedBytes = 0;
  activeUpdateCommand = -1;

  if (sha256Active) {
    mbedtls_sha256_free(&sha256Context);
    sha256Active = false;
  }
}

bool beginUpdate(int command) {
  resetUpdateState();
  updateInProgress = true;
  car_enable = 0;
  movey = 0;
  movex = 0;
  turn = 0;
  stopAllMotors();

  expectedUpdateHash = server.arg("sha256");
  expectedUpdateHash.toLowerCase();

  if (!validSha256String(expectedUpdateHash)) {
    updateFailed = true;
    updateMessage = "Missing or invalid SHA-256.";
    return false;
  }

  activeUpdateCommand = command;

  if (command == U_SPIFFS && fsMounted) {
    LittleFS.end();
    fsMounted = false;
  }

  if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
    updateFailed = true;
    updateMessage = "Update.begin() failed. Error code: " + String(Update.getError());
    return false;
  }

  mbedtls_sha256_init(&sha256Context);
  if (mbedtls_sha256_starts_ret(&sha256Context, 0) != 0) {
    Update.abort();
    updateFailed = true;
    updateMessage = "SHA-256 initialization failed.";
    return false;
  }

  sha256Active = true;
  updateStarted = true;
  return true;
}

void writeUpdateChunk(uint8_t *data, size_t length) {
  if (!updateStarted || updateFailed) return;

  if (sha256Active && mbedtls_sha256_update_ret(&sha256Context, data, length) != 0) {
    updateFailed = true;
    updateMessage = "SHA-256 update failed.";
    Update.abort();
    return;
  }

  size_t written = Update.write(data, length);
  if (written != length) {
    updateFailed = true;
    updateMessage = "Flash write failed. Error code: " + String(Update.getError());
    Update.abort();
    return;
  }

  uploadedBytes += written;
}

void finishUpdate() {
  if (!updateStarted || updateFailed) return;

  unsigned char hash[32];

  if (!sha256Active) {
    updateFailed = true;
    updateMessage = "SHA-256 context missing.";
    Update.abort();
    return;
  }

  if (mbedtls_sha256_finish_ret(&sha256Context, hash) != 0) {
    updateFailed = true;
    updateMessage = "SHA-256 finalization failed.";
    Update.abort();
    return;
  }

  mbedtls_sha256_free(&sha256Context);
  sha256Active = false;

  calculatedUpdateHash = sha256ToHex(hash);
  calculatedUpdateHash.toLowerCase();

  if (calculatedUpdateHash != expectedUpdateHash) {
    Update.abort();
    updateFailed = true;
    updateMessage = "SHA-256 mismatch.\nExpected: " + expectedUpdateHash + "\nReceived: " + calculatedUpdateHash;
    return;
  }

  if (!Update.end(true)) {
    updateFailed = true;
    updateMessage = "Update.end() failed. Error code: " + String(Update.getError());
    return;
  }

  updateSuccess = true;
  updateMessage = "Update successful.\nSHA-256: " + calculatedUpdateHash + "\nBytes: " + String(uploadedBytes) + "\nESP32 will reboot.";
}

void abortUpdate() {
  if (updateStarted) Update.abort();

  if (sha256Active) {
    mbedtls_sha256_free(&sha256Context);
    sha256Active = false;
  }

  updateFailed = true;
  updateMessage = "Upload aborted.";
  updateStarted = false;
}

void handleUpdateUpload(int command) {
  if (!server.authenticate(ADMIN_USER, ADMIN_PASSWORD)) return;

  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    beginUpdate(command);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    writeUpdateChunk(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    finishUpdate();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    abortUpdate();
  }
}

void handleFirmwareUpload() {
  handleUpdateUpload(U_FLASH);
}

void handleFilesystemUpload() {
  handleUpdateUpload(U_SPIFFS);
}

void handleUpdateResult() {
  if (!requireMaintenanceAuth()) {
    updateInProgress = false;
    return;
  }

  if (updateSuccess) {
    server.send(200, "text/plain", updateMessage);
    rebootPending = true;
    rebootAtMs = millis() + 1500UL;
    return;
  }

  if (activeUpdateCommand == U_SPIFFS) {
    fsMounted = LittleFS.begin(false);
  }

  updateInProgress = false;
  String error = updateMessage.length() ? updateMessage : "Unknown update error.";
  server.send(500, "text/plain", error);
}

// =====================================================
// MAINTENANCE / REBOOT / RECOVERY
// =====================================================

void handleMaintenance() {
  if (!requireMaintenanceAuth()) return;

  car_enable = 0;
  movey = 0;
  movex = 0;
  turn = 0;
  stopAllMotors();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", MAINTENANCE_PAGE);
}

void handleReboot() {
  if (!requireMaintenanceAuth()) return;

  car_enable = 0;
  stopAllMotors();
  server.send(200, "text/plain", "Rebooting...");
  rebootPending = true;
  rebootAtMs = millis() + 1000UL;
}

void handleNotFound() {
  String path = server.uri();

  if (recoveryMode && path != "/maintenance") {
    server.sendHeader("Location", "/maintenance");
    server.send(302, "text/plain", "Recovery mode");
    return;
  }

  if (path == "/") path = "/index.html";

  if (serveFile(path)) return;

  if (path == "/index.html") {
    String response =
      "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Mecanum Recovery</title></head><body>"
      "<h1>Mecanum Bot Recovery</h1>"
      "<p>Das Webinterface ist nicht installiert oder LittleFS konnte nicht geladen werden.</p>"
      "<p><a href='/maintenance'>Maintenance / Recovery</a></p>"
      "</body></html>";

    server.send(200, "text/html", response);
    return;
  }

  server.send(404, "text/plain", "Not found");
}

// =====================================================
// BLUETOOTH / WIFI
// =====================================================

void disableBluetooth() {
#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  esp_bt_controller_status_t status = esp_bt_controller_get_status();

  if (status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_bt_controller_disable();
  }

  status = esp_bt_controller_get_status();

  if (status == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_bt_controller_deinit();
  }

  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
#endif
}

void setupWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD, 6, false, 4);
}

// =====================================================
// WEB SERVER
// =====================================================

void setupWebServer() {
  server.on("/api/debug", HTTP_GET, handleDebugGet);
  server.on("/api/debug", HTTP_POST, handleDebugPost);
  server.on("/api/control", HTTP_POST, handleControl);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/maintenance", HTTP_GET, handleMaintenance);
  server.on("/api/system", HTTP_GET, handleSystemInfo);
  server.on("/api/settings/export", HTTP_GET, handleSettingsExport);
  server.on("/api/settings/import", HTTP_POST, handleSettingsImport);
  server.on("/api/settings/reset", HTTP_POST, handleSettingsReset);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/update/firmware", HTTP_POST, handleUpdateResult, handleFirmwareUpload);
  server.on("/api/update/filesystem", HTTP_POST, handleUpdateResult, handleFilesystemUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

// =====================================================
// MOTOR HARDWARE
// =====================================================

void setupMotorHardware() {
  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(STBY_PIN, LOW);

  pinMode(FL_IN1, OUTPUT);
  pinMode(FL_IN2, OUTPUT);
  pinMode(FR_IN1, OUTPUT);
  pinMode(FR_IN2, OUTPUT);
  pinMode(RL_IN1, OUTPUT);
  pinMode(RL_IN2, OUTPUT);
  pinMode(RR_IN1, OUTPUT);
  pinMode(RR_IN2, OUTPUT);

  // Existing tested Arduino-ESP32 LEDC API intentionally unchanged.
  ledcSetup(CH_FL_1, PWM_FREQ, PWM_RES);
  ledcSetup(CH_FL_2, PWM_FREQ, PWM_RES);
  ledcSetup(CH_FR_1, PWM_FREQ, PWM_RES);
  ledcSetup(CH_FR_2, PWM_FREQ, PWM_RES);
  ledcSetup(CH_RL_1, PWM_FREQ, PWM_RES);
  ledcSetup(CH_RL_2, PWM_FREQ, PWM_RES);
  ledcSetup(CH_RR_1, PWM_FREQ, PWM_RES);
  ledcSetup(CH_RR_2, PWM_FREQ, PWM_RES);

  ledcAttachPin(FL_IN1, CH_FL_1);
  ledcAttachPin(FL_IN2, CH_FL_2);
  ledcAttachPin(FR_IN1, CH_FR_1);
  ledcAttachPin(FR_IN2, CH_FR_2);
  ledcAttachPin(RL_IN1, CH_RL_1);
  ledcAttachPin(RL_IN2, CH_RL_2);
  ledcAttachPin(RR_IN1, CH_RR_1);
  ledcAttachPin(RR_IN2, CH_RR_2);

  stopAllMotors();
}

void detectRecoveryMode() {
  if (RECOVERY_PIN < 0) return;

  pinMode(RECOVERY_PIN, INPUT_PULLUP);
  delay(10);

  if (digitalRead(RECOVERY_PIN) == LOW) {
    recoveryMode = true;
    car_enable = 0;
    stopAllMotors();
  }
}

// =====================================================
// SETUP / LOOP
// =====================================================

void setup() {
  Serial.begin(115200);
  debugBootId = esp_random();
  setupMotorHardware();
  detectRecoveryMode();
  disableBluetooth();
  loadSettings();

  // Never auto-format on mount failure. The embedded recovery page
  // must remain usable without destroying existing filesystem data.
  fsMounted = LittleFS.begin(false);

  setupWiFi();
  setupWebServer();

  Serial.println();
  Serial.println("Mecanum Bot started");
  Serial.print("Firmware: ");
  Serial.println(FW_VERSION);
  Serial.print("Web version: ");
  Serial.println(readWebVersion());
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("LittleFS: ");
  Serial.println(fsMounted ? "mounted" : "not mounted");
  Serial.print("Recovery mode: ");
  Serial.println(recoveryMode ? "yes" : "no");

  lastControlUpdateMs = millis();
}

void loop() {
  server.handleClient();
  checkFailsafe();

  unsigned long now = millis();

  if (now - lastControlUpdateMs >= CONTROL_INTERVAL_MS) {
    lastControlUpdateMs = now;
    driveMecanum();
  }

  recordDebugState();

  if (rebootPending && now >= rebootAtMs) {
    stopAllMotors();
    delay(50);
    ESP.restart();
  }

  delay(1);
}
