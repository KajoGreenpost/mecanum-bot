'use strict';

let armed = false;
let s1Dir = 0;
let s1Strength = 0;
let s2Dir = 0;
let s2Strength = 0;
let controlRequestRunning = false;

const CONTROL_INTERVAL_MS = 50;
const armButton = document.getElementById('armButton');
const connectionState = document.getElementById('connectionState');

function updateArmButton() {
  if (armed) {
    armButton.textContent = 'FAHRZEUG AKTIV';
    armButton.classList.remove('arm-off');
    armButton.classList.add('arm-on');
  } else {
    armButton.textContent = 'FAHRZEUG AKTIVIEREN';
    armButton.classList.remove('arm-on');
    armButton.classList.add('arm-off');
  }
}

function resetControlState() {
  s1Dir = 0;
  s1Strength = 0;
  s2Dir = 0;
  s2Strength = 0;
  document.getElementById('stick1Info').textContent = 'Richtung 0 / 0 %';
  document.getElementById('stick2Info').textContent = 'Richtung 0 / 0 %';
}

armButton.addEventListener('click', () => {
  armed = !armed;
  if (!armed) resetControlState();
  updateArmButton();
  sendControl();
});

function initJoystick(stickId, knobId, infoId, callback) {
  const stick = document.getElementById(stickId);
  const knob = document.getElementById(knobId);
  const info = document.getElementById(infoId);
  let active = false;
  let pointerId = null;

  function reset() {
    active = false;
    pointerId = null;
    knob.style.transform = 'translate(0px, 0px)';
    info.textContent = 'Richtung 0 / 0 %';
    callback(0, 0);
    sendControl();
  }

  function move(event) {
    if (!active) return;

    const rect = stick.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const maxDistance = (rect.width / 2) - (knob.offsetWidth / 2) - 5;

    let dx = event.clientX - centerX;
    let dy = event.clientY - centerY;
    const rawDistance = Math.sqrt(dx * dx + dy * dy);
    let distance = rawDistance;

    if (distance > maxDistance) {
      dx = (dx / distance) * maxDistance;
      dy = (dy / distance) * maxDistance;
      distance = maxDistance;
    }

    knob.style.transform = `translate(${dx}px, ${dy}px)`;

    if (rawDistance < 8) {
      info.textContent = 'Richtung 0 / 0 %';
      callback(0, 0);
      return;
    }

    let angle = Math.atan2(dy, dx) * (180 / Math.PI);
    angle += 90;
    if (angle < 0) angle += 360;

    const direction = (Math.floor((angle + 22.5) / 45) % 8) + 1;
    const strength = Math.max(0, Math.min(100, Math.round((Math.min(rawDistance, maxDistance) / maxDistance) * 100)));

    info.textContent = `Richtung ${direction} / ${strength} %`;
    callback(direction, strength);
  }

  stick.addEventListener('pointerdown', event => {
    active = true;
    pointerId = event.pointerId;
    stick.setPointerCapture(pointerId);
    move(event);
  });

  stick.addEventListener('pointermove', event => {
    if (event.pointerId !== pointerId) return;
    move(event);
  });

  stick.addEventListener('pointerup', reset);
  stick.addEventListener('pointercancel', reset);
  stick.addEventListener('lostpointercapture', reset);
}

initJoystick('stick1', 'knob1', 'stick1Info', (direction, strength) => {
  s1Dir = direction;
  s1Strength = strength;
});

initJoystick('stick2', 'knob2', 'stick2Info', (direction, strength) => {
  s2Dir = direction;
  s2Strength = strength;
});

async function sendControl() {
  if (controlRequestRunning) return;
  controlRequestRunning = true;

  const body = new URLSearchParams();
  body.set('s1_dir', s1Dir);
  body.set('s1_str', s1Strength);
  body.set('s2_dir', s2Dir);
  body.set('s2_str', s2Strength);
  body.set('en', armed ? '1' : '0');

  try {
    const response = await fetch('/api/control', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: body.toString(),
      cache: 'no-store'
    });

    if (response.ok) {
      connectionState.textContent = armed ? 'Verbunden / Fahrzeug aktiv' : 'Verbunden / Fahrzeug deaktiviert';
    } else {
      connectionState.textContent = 'ESP32 Fehler: ' + response.status;
    }
  } catch (error) {
    connectionState.textContent = 'Verbindung zum ESP32 verloren';
  } finally {
    controlRequestRunning = false;
  }
}

setInterval(sendControl, CONTROL_INTERVAL_MS);

document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    armed = false;
    resetControlState();
    updateArmButton();
    sendControl();
  }
});

window.addEventListener('pagehide', () => {
  armed = false;
});

async function loadSettings() {
  try {
    const response = await fetch('/api/settings', {cache: 'no-store'});
    const settings = await response.json();

    document.getElementById('speedMode').value = settings.speedMode;
    document.getElementById('startMin').value = settings.startMin;
    document.getElementById('boost').value = settings.boost;
    document.getElementById('boostTime').value = settings.boostTime;
    document.getElementById('deadzone').value = settings.deadzone;
    document.getElementById('invertFL').checked = !!settings.invertFL;
    document.getElementById('invertFR').checked = !!settings.invertFR;
    document.getElementById('invertRL').checked = !!settings.invertRL;
    document.getElementById('invertRR').checked = !!settings.invertRR;
    document.getElementById('invertMoveY').checked = !!settings.invertMoveY;
    document.getElementById('invertMoveX').checked = !!settings.invertMoveX;
    document.getElementById('invertTurn').checked = !!settings.invertTurn;
  } catch (error) {
    document.getElementById('settingsStatus').textContent = 'Einstellungen konnten nicht geladen werden.';
  }
}

document.getElementById('saveSettingsButton').addEventListener('click', async () => {
  const status = document.getElementById('settingsStatus');
  const body = new URLSearchParams();

  body.set('speedMode', document.getElementById('speedMode').value);
  body.set('startMin', document.getElementById('startMin').value);
  body.set('boost', document.getElementById('boost').value);
  body.set('boostTime', document.getElementById('boostTime').value);
  body.set('deadzone', document.getElementById('deadzone').value);
  body.set('invertFL', document.getElementById('invertFL').checked ? '1' : '0');
  body.set('invertFR', document.getElementById('invertFR').checked ? '1' : '0');
  body.set('invertRL', document.getElementById('invertRL').checked ? '1' : '0');
  body.set('invertRR', document.getElementById('invertRR').checked ? '1' : '0');
  body.set('invertMoveY', document.getElementById('invertMoveY').checked ? '1' : '0');
  body.set('invertMoveX', document.getElementById('invertMoveX').checked ? '1' : '0');
  body.set('invertTurn', document.getElementById('invertTurn').checked ? '1' : '0');

  try {
    const response = await fetch('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: body.toString()
    });
    status.textContent = response.ok ? 'Einstellungen gespeichert.' : 'Fehler beim Speichern.';
  } catch (error) {
    status.textContent = 'Verbindungsfehler.';
  }
});

updateArmButton();
loadSettings();
sendControl();
