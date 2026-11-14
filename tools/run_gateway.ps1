# Starts the Lucy model gateway unless something already listens on 8788.
# Used by the "Lucy Gateway" scheduled task.
#
# Output is redirected with Start-Process (raw bytes), so the UTF-8 log stays
# readable. PowerShell's `*>>` writes UTF-16 over the node process's UTF-8 and
# mangles everything.
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
$gatewayDir = Join-Path $root "gateway"

try {
    Invoke-RestMethod "http://127.0.0.1:8788/health" -TimeoutSec 2 | Out-Null
    exit 0
} catch {
}

$node = (Get-Command node -ErrorAction SilentlyContinue).Source
if (-not $node) {
    Write-EventLog -LogName Application -Source "Lucy" -EventId 2 -EntryType Error -Message "node.exe not found" -ErrorAction SilentlyContinue
    exit 1
}

$log = Join-Path $gatewayDir "gateway.log"
$err = Join-Path $gatewayDir "gateway.err.log"

$proc = Start-Process -FilePath $node -ArgumentList "server.js" -WorkingDirectory $gatewayDir `
    -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $err `
    -Wait -PassThru
exit $proc.ExitCode
