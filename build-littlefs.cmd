@echo off
setlocal
cd /d "%~dp0"

if not exist "tools\build-littlefs.ps1" (
  echo.
  echo FEHLER: tools\build-littlefs.ps1 wurde nicht gefunden.
  echo Erwarteter Pfad:
  echo %CD%\tools\build-littlefs.ps1
  echo.
  pause
  exit /b 1
)

echo.
echo ==========================================
echo   MecanumBot - LittleFS BIN erstellen
echo ==========================================
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\build-littlefs.ps1"
set "ERR=%ERRORLEVEL%"

echo.
if not "%ERR%"=="0" (
  echo FEHLER: LittleFS-Build fehlgeschlagen. Exit-Code: %ERR%
  echo.
  pause
  exit /b %ERR%
)

echo ==========================================
echo   Fertig.
echo ==========================================
echo.

if exist "littlefs.bin" (
  echo Erstellt:
  echo %CD%\littlefs.bin
) else (
  echo Der PowerShell-Build wurde ohne Fehler beendet,
  echo aber littlefs.bin wurde im Projektordner nicht gefunden.
)

echo.
pause
exit /b 0
