@echo off
setlocal
cd /d "%~dp0"

echo.
echo ==========================================
echo   MecanumBot - LittleFS BIN erstellen
echo ==========================================
echo.

if not exist "tools\build-littlefs.ps1" (
  echo FEHLER: tools\build-littlefs.ps1 wurde nicht gefunden.
  echo Erwarteter Pfad:
  echo %CD%\tools\build-littlefs.ps1
  echo.
  pause
  exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\build-littlefs.ps1"
set "ERR=%ERRORLEVEL%"

echo.
if not "%ERR%"=="0" (
  echo FEHLER: LittleFS-Build fehlgeschlagen. Exit-Code: %ERR%
  echo.
  pause
  exit /b %ERR%
)

if exist "%CD%\littlefs.bin" (
  echo ==========================================
  echo   LITTLEFS BUILD ERFOLGREICH
  echo ==========================================
  echo.
  echo Datei:
  echo %CD%\littlefs.bin
  echo.
  powershell.exe -NoProfile -Command "(Get-FileHash '%CD%\littlefs.bin' -Algorithm SHA256).Hash.ToLower()"
) else (
  echo FEHLER: littlefs.bin wurde nicht gefunden.
)

echo.
pause
exit /b 0
