$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Files = @('firmware.bin','littlefs.bin')
foreach ($Name in $Files) {
  $Path = Join-Path $ProjectRoot $Name
  if (Test-Path $Path) {
    $Hash = (Get-FileHash $Path -Algorithm SHA256).Hash.ToLower()
    Write-Host "$Name  $Hash"
  } else {
    Write-Host "$Name  NOT FOUND"
  }
}
