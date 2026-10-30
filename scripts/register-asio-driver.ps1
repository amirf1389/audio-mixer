# Registers (or removes) the Audio Mixer ASIO driver so ASIO hosts (DAWs, OBS, ...) list "Audio Mixer".
# Run from an administrator PowerShell, after installing Audio Mixer:
#   powershell -ExecutionPolicy Bypass -File "C:\Program Files\Audio Mixer\scripts\register-asio-driver.ps1"
#   ... to remove it again: add -Unregister
# The 64-bit DLL goes through System32\regsvr32, the 32-bit DLL through SysWOW64\regsvr32, so both registry views get the same keys
# that DllRegisterServer writes (HKLM\SOFTWARE\ASIO\Audio Mixer and the CLSID entries).
#Requires -RunAsAdministrator
param(
  [string]$InstallDir = (Join-Path $env:ProgramFiles 'Audio Mixer'),
  [switch]$Unregister
)

$ErrorActionPreference = 'Stop'
$driverDir = Join-Path $InstallDir 'asio-driver'
if (-not (Test-Path $driverDir)) { throw "ASIO driver files not found in $driverDir. Install Audio Mixer first, or pass -InstallDir." }

$regsvr64 = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$regsvr32 = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'
$targets = @(
  @{ Exe = $regsvr64; Dll = Join-Path $driverDir 'AudioMixerASIO64.dll' },
  @{ Exe = $regsvr32; Dll = Join-Path $driverDir 'AudioMixerASIO32.dll' }
)

foreach ($t in $targets) {
  if (-not (Test-Path $t.Dll)) { Write-Warning "Missing $($t.Dll); skipped"; continue }
  $regArgs = @('/s')
  if ($Unregister) { $regArgs += '/u' }
  $regArgs += $t.Dll
  $p = Start-Process -FilePath $t.Exe -ArgumentList $regArgs -Wait -PassThru
  if ($p.ExitCode -ne 0) { throw "regsvr32 failed for $($t.Dll) (exit code $($p.ExitCode))" }
  Write-Output ($(if ($Unregister) { 'Unregistered' } else { 'Registered' }) + ": $($t.Dll)")
}

if (-not $Unregister) {
  $clsidKey = 'HKLM:\SOFTWARE\ASIO\Audio Mixer'
  if (Test-Path $clsidKey) { Write-Output "ASIO driver key present: $clsidKey" }
  else { throw "Registration finished but $clsidKey is missing." }
}
