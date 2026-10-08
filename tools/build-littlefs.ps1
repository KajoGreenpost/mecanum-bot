$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DataDir = Join-Path $ProjectRoot "data"
$OutputFile = Join-Path $ProjectRoot "littlefs.bin"
$PartitionFile = Join-Path $ProjectRoot "partitions.csv"

Write-Host ""
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  MecanumBot - LittleFS Builder" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
Write-Host ""

# ------------------------------------------------------------
# Projekt pruefen
# ------------------------------------------------------------

if (-not (Test-Path $DataDir)) {
    Write-Host "FEHLER: data-Ordner nicht gefunden:" -ForegroundColor Red
    Write-Host $DataDir -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $PartitionFile)) {
    Write-Host "FEHLER: partitions.csv nicht gefunden:" -ForegroundColor Red
    Write-Host $PartitionFile -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------------
# LittleFS Partitionsgroesse aus partitions.csv lesen
# ------------------------------------------------------------

$PartitionSize = $null

$PartitionLines = Get-Content $PartitionFile

foreach ($Line in $PartitionLines) {
    $Trimmed = $Line.Trim()

    if ($Trimmed.Length -eq 0) {
        continue
    }

    if ($Trimmed.StartsWith("#")) {
        continue
    }

    $Parts = $Trimmed.Split(",")

    if ($Parts.Count -lt 5) {
        continue
    }

    $Name = $Parts[0].Trim()
    $Type = $Parts[1].Trim()
    $SubType = $Parts[2].Trim()
    $SizeValue = $Parts[4].Trim()

    if ($Name -eq "spiffs" -or $Name -eq "littlefs" -or $SubType -eq "spiffs") {
        if ($SizeValue.StartsWith("0x")) {
            $PartitionSize = [Convert]::ToInt64($SizeValue.Substring(2), 16)
        }
        else {
            $PartitionSize = [Int64]$SizeValue
        }

        break
    }
}

if (-not $PartitionSize) {
    Write-Host "FEHLER: LittleFS/SPIFSS-Partition konnte in partitions.csv nicht gefunden werden." -ForegroundColor Red
    exit 1
}

Write-Host "Projekt:       $ProjectRoot"
Write-Host "Data:          $DataDir"
Write-Host "Partition:     $PartitionSize Bytes"
Write-Host ""

# ------------------------------------------------------------
# mklittlefs.exe suchen
# ------------------------------------------------------------

$CandidateRoots = @(
    (Join-Path $ProjectRoot "tools"),
    (Join-Path $env:LOCALAPPDATA "Arduino15"),
    (Join-Path $env:APPDATA "Arduino15"),
    (Join-Path $env:USERPROFILE ".arduino15"),
    (Join-Path $env:USERPROFILE ".platformio"),
    "C:\Program Files\Arduino IDE",
    "C:\Program Files (x86)\Arduino IDE",
    "C:\Program Files\Arduino",
    "C:\Program Files (x86)\Arduino"
)

$MkLittleFS = $null

# Erst PATH pruefen
$PathTool = Get-Command "mklittlefs.exe" -ErrorAction SilentlyContinue

if ($PathTool) {
    $MkLittleFS = $PathTool.Source
}

# Danach typische Installationsorte rekursiv durchsuchen
if (-not $MkLittleFS) {
    foreach ($Root in $CandidateRoots) {
        if (-not $Root) {
            continue
        }

        if (-not (Test-Path $Root)) {
            continue
        }

        Write-Host "Suche mklittlefs unter: $Root" -ForegroundColor DarkGray

        $Found = Get-ChildItem -Path $Root -Filter "mklittlefs.exe" -File -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1

        if ($Found) {
            $MkLittleFS = $Found.FullName
            break
        }
    }
}

if (-not $MkLittleFS) {
    Write-Host ""
    Write-Host "FEHLER: mklittlefs.exe wurde nicht gefunden." -ForegroundColor Red
    Write-Host ""
    Write-Host "Durchsuchte Bereiche:" -ForegroundColor Yellow

    foreach ($Root in $CandidateRoots) {
        Write-Host "  $Root"
    }

    Write-Host ""
    Write-Host "Das bedeutet wahrscheinlich, dass mklittlefs bei deiner" -ForegroundColor Yellow
    Write-Host "Arduino/ESP32-Installation nicht installiert ist." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Dann muessen wir nur noch herausfinden, wo deine ESP32-Core-Dateien liegen." -ForegroundColor Yellow

    exit 1
}

Write-Host ""
Write-Host "mklittlefs gefunden:" -ForegroundColor Green
Write-Host $MkLittleFS -ForegroundColor Green
Write-Host ""

# ------------------------------------------------------------
# Vorhandene BIN entfernen
# ------------------------------------------------------------

if (Test-Path $OutputFile) {
    Remove-Item $OutputFile -Force
}

# ------------------------------------------------------------
# Dateigroesse des data Ordners anzeigen
# ------------------------------------------------------------

$DataSize = (
    Get-ChildItem $DataDir -File -Recurse |
    Measure-Object -Property Length -Sum
).Sum

if (-not $DataSize) {
    $DataSize = 0
}

Write-Host "Website-Dateien: $DataSize Bytes"
Write-Host "LittleFS-Limit:   $PartitionSize Bytes"

if ($DataSize -ge $PartitionSize) {
    Write-Host ""
    Write-Host "FEHLER: data-Ordner ist groesser als die LittleFS-Partition." -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Erzeuge littlefs.bin ..." -ForegroundColor Cyan
Write-Host ""

# ------------------------------------------------------------
# LittleFS Image erzeugen
# ------------------------------------------------------------

& $MkLittleFS `
    -c $DataDir `
    -b 4096 `
    -p 256 `
    -s $PartitionSize `
    $OutputFile

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "FEHLER: mklittlefs wurde mit Exit-Code $LASTEXITCODE beendet." -ForegroundColor Red
    exit $LASTEXITCODE
}

if (-not (Test-Path $OutputFile)) {
    Write-Host ""
    Write-Host "FEHLER: littlefs.bin wurde nicht erzeugt." -ForegroundColor Red
    exit 1
}

$OutputSize = (Get-Item $OutputFile).Length

Write-Host ""
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  LITTLEFS BUILD ERFOLGREICH" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Datei:" -ForegroundColor Green
Write-Host $OutputFile
Write-Host ""
Write-Host "Groesse: $OutputSize Bytes"

# ------------------------------------------------------------
# SHA-256 berechnen
# ------------------------------------------------------------

$Hash = (Get-FileHash $OutputFile -Algorithm SHA256).Hash.ToLower()

Write-Host ""
Write-Host "SHA-256:" -ForegroundColor Cyan
Write-Host $Hash -ForegroundColor Cyan
Write-Host ""

exit 0