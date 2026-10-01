param(
    [string]$XyceExe = "",
    [string]$Netlist = ""
)

$ErrorActionPreference = "Stop"

$prototypeRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$configPath = Join-Path $prototypeRoot "config\xyce.local.json"

if (-not $Netlist) {
    $Netlist = Join-Path $prototypeRoot "sim\xyce\examples\resistor_divider.cir"
}

if (-not $XyceExe) {
    if (Test-Path -LiteralPath $configPath) {
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        $XyceExe = $config.xyceExe
    }
}

if (-not $XyceExe -or -not (Test-Path -LiteralPath $XyceExe)) {
    & (Join-Path $PSScriptRoot "setup_xyce.ps1")
    if (Test-Path -LiteralPath $configPath) {
        $config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
        $XyceExe = $config.xyceExe
    }
}

if (-not $XyceExe -or -not (Test-Path -LiteralPath $XyceExe)) {
    throw "Xyce.exe is not configured. Run tools\xyce\setup_xyce.ps1 first."
}

if (-not (Test-Path -LiteralPath $Netlist)) {
    throw "Netlist not found: $Netlist"
}

$runDir = Join-Path $prototypeRoot "sim\xyce\runs\smoke"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$netlistFile = Get-Item -LiteralPath $Netlist
$localNetlist = Join-Path $runDir $netlistFile.Name
Copy-Item -LiteralPath $netlistFile.FullName -Destination $localNetlist -Force

Push-Location $runDir
try {
    Write-Host "Running Xyce:"
    Write-Host "  $XyceExe"
    Write-Host "Netlist:"
    Write-Host "  $localNetlist"
    & $XyceExe $localNetlist
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "Xyce failed with exit code $code"
    }

    Write-Host "Smoke run complete. Output files:"
    Get-ChildItem -LiteralPath $runDir | Select-Object Name, Length | Format-Table -AutoSize
}
finally {
    Pop-Location
}
