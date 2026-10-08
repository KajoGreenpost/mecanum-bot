param([string]$WebsiteRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) '..\mecanum-bot-website'))

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$WebsiteRoot = (Resolve-Path -LiteralPath $WebsiteRoot).Path
$DataDir = Join-Path $ProjectRoot 'data'

Push-Location $WebsiteRoot
try {
    & npm.cmd test
    if ($LASTEXITCODE -ne 0) { throw 'Website regression tests failed.' }
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Website build failed.' }
} finally { Pop-Location }

$DistDir = Join-Path $WebsiteRoot 'dist'
if (-not (Test-Path -LiteralPath (Join-Path $DistDir 'index.html'))) { throw 'Missing dist/index.html.' }

# Remove only generated Vite bundles inside this project's assets directory.
$AssetsDir = [IO.Path]::GetFullPath((Join-Path $DataDir 'assets'))
if ($AssetsDir -ne [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'data\assets'))) { throw 'Unexpected assets path.' }
if (Test-Path -LiteralPath $AssetsDir) {
    Get-ChildItem -LiteralPath $AssetsDir -File | Where-Object { $_.Name -match '^index-[\w-]+\.(js|css)$' } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName }
}
Copy-Item -Path (Join-Path $DistDir '*') -Destination $DataDir -Recurse -Force
$Version = (Get-Content -LiteralPath (Join-Path $WebsiteRoot 'package.json') -Raw | ConvertFrom-Json).version
Set-Content -LiteralPath (Join-Path $DataDir 'version.txt') -Value $Version -Encoding ascii
& (Join-Path $PSScriptRoot 'build-littlefs.ps1')
if ($LASTEXITCODE -ne 0) { throw 'LittleFS build failed.' }
