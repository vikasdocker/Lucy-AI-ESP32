# Starts Ollama on 0.0.0.0:11434 unless something already answers there.
# Used by the "Lucy Ollama" scheduled task, so a running desktop app is not
# duplicated and a fresh logon always has a model server.
$ErrorActionPreference = "Continue"

$env:OLLAMA_HOST = "0.0.0.0:11434"
try {
    Invoke-RestMethod "http://127.0.0.1:11434/api/tags" -TimeoutSec 2 | Out-Null
    exit 0
} catch {
}

$ollama = (Get-Command ollama -ErrorAction SilentlyContinue).Source
if (-not $ollama) {
    $ollama = Join-Path $env:LOCALAPPDATA "Programs\Ollama\ollama.exe"
}
if (-not (Test-Path $ollama)) {
    Write-EventLog -LogName Application -Source "Lucy" -EventId 1 -EntryType Error -Message "ollama.exe not found" -ErrorAction SilentlyContinue
    exit 1
}

& $ollama serve
