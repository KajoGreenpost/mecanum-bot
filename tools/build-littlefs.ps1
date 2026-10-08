$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\mklittlefs'
$MkLittleFS = Get-ChildItem $ToolRoot -Recurse -Filter mklittlefs.exe | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $MkLittleFS) { throw 'mklittlefs.exe was not found in the installed Arduino ESP32 tools.' }
$DataDir = Join-Path $ProjectRoot 'data'
$Output = Join-Path $ProjectRoot 'littlefs.bin'
& $MkLittleFS -c $DataDir -b 4096 -p 256 -s 983040 $Output
if ($LASTEXITCODE -ne 0) { throw "mklittlefs failed with exit code $LASTEXITCODE" }
$Hash = (Get-FileHash $Output -Algorithm SHA256).Hash.ToLower()
Write-Host "Created: $Output"
Write-Host "SHA-256: $Hash"
