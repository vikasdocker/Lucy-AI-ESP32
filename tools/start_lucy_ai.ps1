# Start the local Lucy AI bridge (Ollama on this PC).
#
# Run from an elevated PowerShell:
#   powershell -ExecutionPolicy Bypass -File tools\start_lucy_ai.ps1
#
# It binds Ollama to all interfaces (0.0.0.0:11434), opens the firewall port,
# and prints the address Lucy needs. Lucy itself never touches the internet.
param(
    [int]$Port = 11434,
    [string]$Model = "qwen2.5:7b"
)

$ErrorActionPreference = "Continue"
$env:OLLAMA_HOST = "0.0.0.0:$Port"

Write-Host "Lucy AI bridge" -ForegroundColor Cyan
Write-Host "  model:    $Model"
Write-Host "  endpoint: http://0.0.0.0:$Port"

# 1. Firewall: allow inbound Ollama from Lucy's AP/LAN.
if (-not (Get-NetFirewallRule -DisplayName "Lucy AI (Ollama)" -ErrorAction SilentlyContinue)) {
    try {
        New-NetFirewallRule -DisplayName "Lucy AI (Ollama)" -Direction Inbound `
            -Protocol TCP -LocalPort $Port -Action Allow -Profile Any | Out-Null
        Write-Host "  firewall: rule added for TCP $Port"
    } catch {
        Write-Host "  firewall: could not add rule (run as Administrator)" -ForegroundColor Yellow
    }
} else {
    Write-Host "  firewall: rule present for TCP $Port"
}

# 2. Start Ollama bound to all interfaces if it is not already answering.
$running = $false
try {
    Invoke-RestMethod "http://127.0.0.1:$Port/api/tags" -TimeoutSec 2 | Out-Null
    $running = $true
} catch { }

if (-not $running) {
    Write-Host "  starting ollama serve ..."
    Start-Process -FilePath "ollama" -ArgumentList "serve" -WindowStyle Hidden
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            Invoke-RestMethod "http://127.0.0.1:$Port/api/tags" -TimeoutSec 2 | Out-Null
            $running = $true
            break
        } catch { }
    }
}

if (-not $running) {
    Write-Host "  ERROR: Ollama did not answer on port $Port." -ForegroundColor Red
    Write-Host "  Try: ollama serve   (with OLLAMA_HOST=0.0.0.0:$Port)"
    exit 1
}

$models = (Invoke-RestMethod "http://127.0.0.1:$Port/api/tags").models
Write-Host "  API: up ($($models.Count) model(s): $($models.name -join ', '))"

# 3. Show this PC's addresses so the user can point Lucy at it.
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Connect this PC's Wi-Fi to the 'Lucy-Setup' access point."
Write-Host "  2. Run: ipconfig    (or read the table below)"
Write-Host "  3. In Lucy's serial console:  AI host 192.168.4.x $Port"
Write-Host "     then:                      AI on"
Write-Host "     first reply may take ~6 s while the model loads; then it is fast."
Write-Host ""
Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -ne "127.0.0.1" -and $_.IPAddress -notlike "169.254.*" } |
    Select-Object InterfaceAlias, IPAddress |
    Format-Table -AutoSize

Write-Host "Ollama stays running in the background. Reconnect to your normal Wi-Fi when done."
