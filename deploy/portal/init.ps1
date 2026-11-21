# Windows (testing without Docker): prepares this folder like init.sh. Needs Node.js 22.13+ on PATH. Secrets are never printed.
#   powershell -ExecutionPolicy Bypass -File deploy\portal\init.ps1
$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot
function New-Secret { node -e "process.stdout.write(require('node:crypto').randomBytes(48).toString('base64url'))" }
function Restrict($path) {   # only the current user may read it (the Windows form of chmod 600)
  & icacls $path /inheritance:r /grant:r "$($env:USERNAME):(F)" | Out-Null
}
if (-not (Test-Path .env)) { Copy-Item .env.example .env }
$lines = Get-Content .env
foreach ($k in 'LICENSE_ADMIN_TOKEN','OTA_ADMIN_TOKEN','PORTAL_JWT_SECRET','PORTAL_CRM_SECRET') {
  $i = [Array]::IndexOf($lines, "$k=")
  if ($i -ge 0) { $lines[$i] = "$k=" + (New-Secret); Write-Host "made $k" } else { Write-Host "kept $k" }
}
Set-Content -Path .env -Value $lines -Encoding ascii
Restrict .env
foreach ($d in 'secrets','data','data\licence','data\staff','data\ota','site-releases','backups') { New-Item -ItemType Directory -Force -Path $d | Out-Null }
Restrict secrets
if (-not (Test-Path secrets\licence-data.key) -or (Get-Item secrets\licence-data.key).Length -eq 0) { New-Secret | Set-Content -NoNewline -Encoding ascii secrets\licence-data.key; Write-Host 'made secrets\licence-data.key' }
Restrict secrets\licence-data.key
if (-not (Test-Path secrets\vendor-private.pem)) { Write-Host "NEXT: put your vendor private key at $PSScriptRoot\secrets\vendor-private.pem (or a test key, see README)." }
Write-Host 'done. Start with: node deploy\portal\start.js'
