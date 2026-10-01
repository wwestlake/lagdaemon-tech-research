param(
    [string]$XyceExe = ""
)

$ErrorActionPreference = "Stop"

$prototypeRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$configDir = Join-Path $prototypeRoot "config"
$configPath = Join-Path $configDir "xyce.local.json"

function Find-Xyce {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path -LiteralPath $ExplicitPath)) {
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $candidates = @(
        "C:\Program Files\Xyce 7.10 NORAD\bin\Xyce.exe",
        "C:\Program Files\Xyce 7.9 NORAD\bin\Xyce.exe",
        "C:\Program Files\Xyce\bin\Xyce.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    $cmd = Get-Command Xyce.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    return ""
}

$found = Find-Xyce -ExplicitPath $XyceExe
if (-not $found) {
    Write-Host "Xyce.exe was not found."
    Write-Host "Install Xyce for Windows from the official Xyce downloads page, then rerun:"
    Write-Host "  .\tools\xyce\setup_xyce.ps1 -XyceExe 'C:\Program Files\Xyce 7.10 NORAD\bin\Xyce.exe'"
    exit 1
}

New-Item -ItemType Directory -Force -Path $configDir | Out-Null
$json = [ordered]@{
    backend = "xyce"
    xyceExe = $found
    configuredAt = (Get-Date).ToString("o")
}

$json | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $configPath -Encoding UTF8
Write-Host "Xyce configured:"
Write-Host "  $found"
Write-Host "Config written:"
Write-Host "  $configPath"
