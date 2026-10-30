# Prepares a Windows laptop to build the Audio Mixer installers.
# The installers (.msi, setup .exe) are built with wixl and MinGW-w64, which run on Linux. So this script installs:
#   Git for Windows and Node.js LTS (winget), and WSL with Ubuntu, then runs scripts/setup-build-tools.sh inside Ubuntu.
# Run in an elevated (Administrator) PowerShell:
#   Set-ExecutionPolicy -Scope Process Bypass; .\scripts\setup-build-tools.ps1

$ErrorActionPreference = 'Stop'

function Require-Winget {
  if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'winget not found. Install "App Installer" from the Microsoft Store, then run this script again.'
  }
}

function Install-WingetPackage($id) {
  Write-Host "Installing $id ..."
  winget install --id $id --exact --silent --accept-package-agreements --accept-source-agreements
  # winget returns non-zero when the package is already installed; treat that as success
}

Require-Winget
Install-WingetPackage 'Git.Git'
Install-WingetPackage 'OpenJS.NodeJS.LTS'

# WSL with Ubuntu for the MinGW-w64 and wixl tools
$wslOk = $false
try { wsl --status *> $null; $wslOk = ($LASTEXITCODE -eq 0) } catch { $wslOk = $false }
$distros = @()
if ($wslOk) { $distros = (wsl --list --quiet) -replace "`0", '' | Where-Object { $_.Trim() } }
if (-not ($distros | Where-Object { $_ -match 'Ubuntu' })) {
  Write-Host 'Installing WSL with Ubuntu (a restart may be required) ...'
  wsl --install -d Ubuntu
  Write-Host 'When Ubuntu opens, create a user, then run this script again.'
  exit 0
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$wslRepo = (wsl wslpath -a ($repo -replace '\\', '/')).Trim()
Write-Host "Installing MinGW-w64, wixl and osslsigncode inside Ubuntu (repo: $wslRepo) ..."
wsl -d Ubuntu -- bash -lc "cd '$wslRepo' && bash scripts/setup-build-tools.sh"
if ($LASTEXITCODE -ne 0) { throw 'Setup inside Ubuntu failed. See the messages above.' }

Write-Host ''
Write-Host 'Done. Build the installers from Ubuntu (WSL):'
Write-Host "  wsl -d Ubuntu -- bash -lc `"cd '$wslRepo' && npm run build && npm run build:installers`""
