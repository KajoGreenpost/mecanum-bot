@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo.
echo ==========================================
echo   MecanumBot - Firmware BIN erstellen
echo ==========================================
echo.

set "SKETCH=%CD%\MecanumBot.ino"
set "BUILD_DIR=%CD%\build"
set "OUT_BIN=%CD%\firmware.bin"

rem Standard fuer klassisches ESP32 Dev Module.
rem Falls ihr in der Arduino IDE ein anderes Board nutzt,
rem nur diese eine Zeile entsprechend anpassen.
set "FQBN=esp32:esp32:esp32"

if not exist "%SKETCH%" (
  echo FEHLER: MecanumBot.ino wurde nicht gefunden:
  echo %SKETCH%
  echo.
  pause
  exit /b 1
)

rem -----------------------------------------------------
rem arduino-cli.exe finden
rem -----------------------------------------------------

set "ARDUINO_CLI="

for /f "delims=" %%I in ('where arduino-cli.exe 2^>nul') do (
  if not defined ARDUINO_CLI set "ARDUINO_CLI=%%I"
)

if not defined ARDUINO_CLI if exist "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" (
  set "ARDUINO_CLI=C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

if not defined ARDUINO_CLI if exist "%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" (
  set "ARDUINO_CLI=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
)

if not defined ARDUINO_CLI (
  for /f "usebackq delims=" %%I in (`powershell.exe -NoProfile -Command "$roots=@('C:\Program Files\Arduino IDE',$env:LOCALAPPDATA+'\Programs\Arduino IDE'); foreach($r in $roots){if(Test-Path $r){$f=Get-ChildItem $r -Filter arduino-cli.exe -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1; if($f){$f.FullName; break}}}"`) do (
    if not defined ARDUINO_CLI set "ARDUINO_CLI=%%I"
  )
)

if not defined ARDUINO_CLI (
  echo FEHLER: arduino-cli.exe wurde nicht gefunden.
  echo.
  echo Installiere Arduino IDE 2.x oder Arduino CLI,
  echo oder fuege arduino-cli.exe zum PATH hinzu.
  echo.
  pause
  exit /b 1
)

echo Arduino CLI:
echo %ARDUINO_CLI%
echo.
echo Board FQBN:
echo %FQBN%
echo.

rem -----------------------------------------------------
rem Alten Build entfernen
rem -----------------------------------------------------

if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%" >nul 2>&1

if exist "%OUT_BIN%" del /f /q "%OUT_BIN%"

rem -----------------------------------------------------
rem Kompilieren
rem -----------------------------------------------------

echo Kompiliere Firmware...
echo.

"%ARDUINO_CLI%" compile --fqbn "%FQBN%" --output-dir "%BUILD_DIR%" "%SKETCH%"
set "ERR=%ERRORLEVEL%"

if not "%ERR%"=="0" (
  echo.
  echo FEHLER: Firmware-Build fehlgeschlagen. Exit-Code: %ERR%
  echo.
  echo Falls in der Arduino IDE ein anderes ESP32-Board ausgewaehlt ist,
  echo passe oben in dieser Datei die Variable FQBN an.
  echo.
  pause
  exit /b %ERR%
)

rem -----------------------------------------------------
rem Application BIN finden
rem -----------------------------------------------------

set "APP_BIN="

for %%F in ("%BUILD_DIR%\*.ino.bin") do (
  if exist "%%~fF" (
    if /I not "%%~nxF"=="MecanumBot.ino.bootloader.bin" (
      if /I not "%%~nxF"=="MecanumBot.ino.partitions.bin" (
        set "APP_BIN=%%~fF"
      )
    )
  )
)

if not defined APP_BIN (
  for %%F in ("%BUILD_DIR%\*.bin") do (
    if exist "%%~fF" (
      echo %%~nxF | findstr /I /V "bootloader partitions merged" >nul
      if not errorlevel 1 if not defined APP_BIN set "APP_BIN=%%~fF"
    )
  )
)

if not defined APP_BIN (
  echo.
  echo FEHLER: Die erzeugte Application-BIN wurde nicht gefunden.
  echo Build-Ordner:
  echo %BUILD_DIR%
  echo.
  pause
  exit /b 1
)

copy /y "%APP_BIN%" "%OUT_BIN%" >nul

if not exist "%OUT_BIN%" (
  echo.
  echo FEHLER: firmware.bin konnte nicht erstellt werden.
  echo.
  pause
  exit /b 1
)

echo.
echo ==========================================
echo   FIRMWARE BUILD ERFOLGREICH
echo ==========================================
echo.
echo Quelle:
echo %APP_BIN%
echo.
echo OTA-Datei:
echo %OUT_BIN%
echo.
echo SHA-256:
powershell.exe -NoProfile -Command "(Get-FileHash '%OUT_BIN%' -Algorithm SHA256).Hash.ToLower()"
echo.
echo HINWEIS:
echo Fuer OTA nur firmware.bin hochladen.
echo Bootloader- und partitions.bin NICHT ueber OTA hochladen.
echo.
pause
exit /b 0
