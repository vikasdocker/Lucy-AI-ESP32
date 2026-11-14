# Bring up everything Lucy needs for internet chat:
#   * Ollama on this PC (127.0.0.1:11434)
#   * the token-protected model gateway (127.0.0.1:8788)
#   * the Cloudflare tunnel (https://lucy.blueflutex.in)
#
# Lucy itself only needs its Wi-Fi configured once:
#   python tools\provision_lucy_ai.py COM5
#
# Run from an elevated PowerShell (firewall hardening needs admin):
#   powershell -ExecutionPolicy Bypass -File tools\start_lucy_public.ps1
#   powershell -ExecutionPolicy Bypass -File tools\start_lucy_public.ps1 -Stop
param(
    [int]$GatewayPort = 8788,
    [string]$Model = "qwen2.5:7b",
    [string]$PublicHost = "lucy.blueflutex.in",
    [switch]$Stop
)

$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$gatewayDir = Join-Path $root "gateway"
$configPath = Join-Path $gatewayDir "config.json"

function Test-Json($url, $timeout = 3) {
    try {
        Invoke-RestMethod $url -TimeoutSec $timeout | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Get-GatewayProcesses {
    # The process is started as `node server.js` (no path), so match the
    # listener instead of the command line.
    Get-NetTCPConnection -LocalPort $GatewayPort -State Listen -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty OwningProcess -Unique |
        ForEach-Object {
            Get-CimInstance Win32_Process -Filter "ProcessId=$_" -ErrorAction SilentlyContinue
        }
}

if ($Stop) {
    $procs = Get-GatewayProcesses
    if ($procs) {
        $procs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        Write-Host "gateway stopped"
    } else {
        Write-Host "gateway was not running"
    }
    Write-Host "the Cloudflare tunnel and Ollama were left alone; stop them from Task Manager if needed."
    exit 0
}

Write-Host "Lucy public model bridge" -ForegroundColor Cyan

# 0. Gateway config with a real token.
if (-not (Test-Path $configPath)) {
    Write-Host "  config: generating gateway/config.json with a random token"
    node (Join-Path $gatewayDir "setup.js")
}
$token = (Get-Content $configPath -Raw | ConvertFrom-Json).token
if (-not $token -or $token.Length -lt 24 -or $token -like "CHANGE_ME*") {
    Write-Host "  ERROR: gateway token is missing/short. Run: node gateway/setup.js --rotate" -ForegroundColor Red
    exit 1
}
Write-Host "  config: token present ($($token.Length) chars, hidden)"

# 1. Ollama on loopback (0.0.0.0 keeps the LAN fallback working too).
$env:OLLAMA_HOST = "0.0.0.0:11434"
if (-not (Test-Json "http://127.0.0.1:11434/api/tags")) {
    Write-Host "  ollama: starting ..."
    Start-Process -FilePath "ollama" -ArgumentList "serve" -WindowStyle Hidden
    $up = $false
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        if (Test-Json "http://127.0.0.1:11434/api/tags") { $up = $true; break }
    }
    if (-not $up) {
        Write-Host "  ERROR: Ollama did not start." -ForegroundColor Red
        exit 1
    }
}
$models = (Invoke-RestMethod "http://127.0.0.1:11434/api/tags").models
if ($Model -notin $models.name) {
    Write-Host "  WARNING: '$Model' is not pulled. Run: ollama pull $Model" -ForegroundColor Yellow
}
Write-Host "  ollama: up ($($models.name -join ', '))"

# 2. Firewall: keep 11434 reachable from private networks only, never the internet.
if (-not (Get-NetFirewallRule -DisplayName "Lucy AI (Ollama)" -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -DisplayName "Lucy AI (Ollama)" -Direction Inbound -Protocol TCP `
        -LocalPort 11434 -Action Allow -Profile Any `
        -RemoteAddress 192.168.0.0/16,10.0.0.0/8,172.16.0.0/12 | Out-Null
    Write-Host "  firewall: rule added (private networks only)"
} else {
    try {
        Set-NetFirewallRule -DisplayName "Lucy AI (Ollama)" `
            -RemoteAddress 192.168.0.0/16,10.0.0.0/8,172.16.0.0/12 -ErrorAction Stop
        Write-Host "  firewall: restricted to private networks"
    } catch {
        Write-Host "  firewall: could not restrict rule (run as Administrator)" -ForegroundColor Yellow
    }
}

# 3. Gateway on loopback only. The tunnel is the sole public entrance.
if (-not (Test-Json "http://127.0.0.1:$GatewayPort/health")) {
    Write-Host "  gateway: starting on 127.0.0.1:$GatewayPort ..."
    Start-Process -FilePath "node" -ArgumentList "server.js" -WorkingDirectory $gatewayDir `
        -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $gatewayDir "gateway.log") `
        -RedirectStandardError (Join-Path $gatewayDir "gateway.err.log")
    $up = $false
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 300
        if (Test-Json "http://127.0.0.1:$GatewayPort/health") { $up = $true; break }
    }
    if (-not $up) {
        Write-Host "  ERROR: gateway did not start; see gateway\gateway.err.log" -ForegroundColor Red
        exit 1
    }
}
Write-Host "  gateway: up on http://127.0.0.1:$GatewayPort (model $Model)"

# 4. Cloudflare tunnel.
$publicUrl = "https://$PublicHost"
if (-not (Test-Json "$publicUrl/health" 15)) {
    Write-Host "  tunnel: not reachable, starting cloudflared ..."
    $cf = "C:\Users\vikas\esp32-opencode-chat\bin\cloudflared.exe"
    if (-not (Test-Path $cf)) { $cf = "cloudflared" }
    $cfConfig = Join-Path $env:USERPROFILE ".cloudflared\config.yml"
    Start-Process -FilePath $cf `
        -ArgumentList @("tunnel", "--config", $cfConfig, "run", "esp32-chat") `
        -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $env:TEMP "lucy-tunnel.log") `
        -RedirectStandardError (Join-Path $env:TEMP "lucy-tunnel.err.log")
    $up = $false
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Seconds 1
        if (Test-Json "$publicUrl/health" 10) { $up = $true; break }
    }
    if (-not $up) {
        Write-Host "  ERROR: tunnel not reachable; see $env:TEMP\lucy-tunnel.err.log" -ForegroundColor Red
        exit 1
    }
}
$health = Invoke-RestMethod "$publicUrl/health" -TimeoutSec 10
Write-Host "  tunnel: $publicUrl -> gateway (model $($health.model))"

Write-Host ""
Write-Host "Ready." -ForegroundColor Green
Write-Host "  Lucy:    python tools\provision_lucy_ai.py COM5   (one time only)"
Write-Host "  Test:    python tools\test_lucy_ai.py COM5"
Write-Host "  Stop:    tools\start_lucy_public.ps1 -Stop"
Write-Host ""
Write-Host "Keep this PC awake and online: it hosts the model and the tunnel." -ForegroundColor Yellow
Write-Host "Ollama is bound to all interfaces (LAN fallback) but the firewall only"
Write-Host "accepts private addresses. Port 11434 is never forwarded by the tunnel."
