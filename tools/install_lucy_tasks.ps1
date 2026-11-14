# Registers the three pieces Lucy needs as logon scheduled tasks, so they
# survive reboots and are not tied to any terminal window.
#
#   powershell -ExecutionPolicy Bypass -File tools\install_lucy_tasks.ps1
#   powershell -ExecutionPolicy Bypass -File tools\install_lucy_tasks.ps1 -Remove
#
# Tasks:
#   Lucy Ollama    ollama serve (0.0.0.0:11434, skipped if already listening)
#   Lucy Gateway   node gateway/server.js (127.0.0.1:8788)
#   Lucy Tunnel    cloudflared tunnel run esp32-chat
param(
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gatewayDir = Join-Path $root "gateway"

$tasks = @("Lucy Ollama", "Lucy Gateway", "Lucy Tunnel")

if ($Remove) {
    foreach ($name in $tasks) {
        if (Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue) {
            Unregister-ScheduledTask -TaskName $name -Confirm:$false
            Write-Host "removed task: $name"
        }
    }
    exit 0
}

# Persist the bind address for every future Ollama start (including the
# desktop app), so the LAN fallback keeps working.
[Environment]::SetEnvironmentVariable("OLLAMA_HOST", "0.0.0.0:11434", "User")
Write-Host "OLLAMA_HOST=0.0.0.0:11434 (user environment)"

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew

$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
    -LogonType Interactive -RunLevel Limited
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"

function Register-LucyTask($name, $exe, $arguments, $workdir) {
    if (Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $name -Confirm:$false
    }
    $action = New-ScheduledTaskAction -Execute $exe -Argument $arguments -WorkingDirectory $workdir
    Register-ScheduledTask -TaskName $name -Action $action -Trigger $trigger `
        -Principal $principal -Settings $settings `
        -Description "Lucy public model bridge" | Out-Null
    Write-Host "registered task: $name"
}

$ollama = (Get-Command ollama -ErrorAction Stop).Source
$cf = "C:\Users\vikas\esp32-opencode-chat\bin\cloudflared.exe"
if (-not (Test-Path $cf)) { $cf = (Get-Command cloudflared -ErrorAction Stop).Source }
$cfConfig = Join-Path $env:USERPROFILE ".cloudflared\config.yml"

Register-LucyTask "Lucy Ollama" "powershell.exe" `
    "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$root\tools\run_ollama.ps1`"" $root
Register-LucyTask "Lucy Gateway" "powershell.exe" `
    "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$root\tools\run_gateway.ps1`"" $root
Register-LucyTask "Lucy Tunnel" $cf "tunnel --config `"$cfConfig`" run esp32-chat" $root

Write-Host ""
Write-Host "Starting tasks now ..." -ForegroundColor Cyan
foreach ($name in $tasks) {
    Start-ScheduledTask -TaskName $name
}

Start-Sleep -Seconds 8
foreach ($name in $tasks) {
    $info = Get-ScheduledTask -TaskName $name | Get-ScheduledTaskInfo
    Write-Host ("  {0,-14} state={1} lastResult={2}" -f $name, (Get-ScheduledTask -TaskName $name).State, $info.LastTaskResult)
}
Write-Host ""
Write-Host "Verify:  https://lucy.blueflutex.in/health" -ForegroundColor Green
