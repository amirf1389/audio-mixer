# AmF X1 quick setup for Windows. Run once, in PowerShell, from this folder:
#   powershell -ExecutionPolicy Bypass -File .\setup.ps1
# It checks for Git, Node.js, the .NET 8 SDK, Visual Studio 2022 with the C++ and .NET desktop workloads, and the WebView2 runtime,
# installs anything missing with winget, restores the app's packages and opens the solution (AmFX1.sln) in Visual Studio.
$ErrorActionPreference = 'Stop'

function Test-Command($name) { [bool](Get-Command $name -ErrorAction SilentlyContinue) }
function Install-Package($label, $wingetId, [string]$override = '') {
    Write-Host "[..] installing $label" -ForegroundColor Yellow
    $wingetArgs = @('install', '--id', $wingetId, '-e', '--accept-package-agreements', '--accept-source-agreements')
    if ($override) { $wingetArgs += @('--override', $override) }
    winget @wingetArgs
}

if (-not (Test-Command 'winget')) {
    throw 'winget is needed (it comes with Windows 10 1809+ and Windows 11). Install "App Installer" from the Microsoft Store, then run this again.'
}

if (Test-Command 'git') { Write-Host '[ok] Git' } else { Install-Package 'Git' 'Git.Git' }
if (Test-Command 'node') { Write-Host '[ok] Node.js' } else { Install-Package 'Node.js LTS' 'OpenJS.NodeJS.LTS' }
if (Test-Command 'dotnet') { Write-Host '[ok] .NET SDK' } else { Install-Package '.NET 8 SDK' 'Microsoft.DotNet.SDK.8' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
if (Test-Path $vswhere) { $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop Microsoft.VisualStudio.Workload.ManagedDesktop -property productPath }
if ($vs) { Write-Host '[ok] Visual Studio 2022 with the C++ and .NET desktop workloads' } else {
    Install-Package 'Visual Studio 2022 Community (C++ and .NET desktop)' 'Microsoft.VisualStudio.2022.Community' '--quiet --wait --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Workload.ManagedDesktop --includeRecommended'
}

if (Test-Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}') { Write-Host '[ok] WebView2 runtime' } else { Install-Package 'WebView2 runtime' 'Microsoft.EdgeWebView2Runtime' }

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
if (Test-Path (Join-Path $root 'bridge\server.js')) { Write-Host "[ok] repository found: $root" } else { throw "bridge\server.js not found under $root. Keep this folder inside the audio-mixer repository." }

Set-Location $PSScriptRoot
dotnet restore 'AmFX1.WinShell\AmFX1.WinShell.csproj'
Write-Host ''
Write-Host 'Setup finished. Opening the solution in Visual Studio: set AmFX1.WinShell as the startup project, then press F5.' -ForegroundColor Green
Start-Process (Resolve-Path 'AmFX1.sln')
