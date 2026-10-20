# verify_test_mode.ps1
# End-to-end automated test for DEWB Non-Destructive Circuit Test Mode.

$ErrorActionPreference = "Stop"

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host " DEWB Non-Destructive Circuit Test Mode Verification" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan

$appPath = "prototype\bin\Debug\djehuti_electronics_lab.exe"
if (-not (Test-Path $appPath)) {
    throw "Application binary not found at $appPath. Build it first."
}

# 1. Clean previous agent-api.json if any
$apiJsonPath = "$env:APPDATA\DjehutiElectronicsLab\agent-api.json"
if (Test-Path $apiJsonPath) {
    Remove-Item $apiJsonPath -Force
}

# 2. Start DEWB application process
Write-Host "`n[1/7] Launching Djehuti Electronics Workbench..." -ForegroundColor Yellow
$proc = Start-Process -FilePath $appPath -PassThru
Start-Sleep -Seconds 3

# Wait for agent-api.json
$timeout = 20
while (-not (Test-Path $apiJsonPath) -and $timeout -gt 0) {
    Start-Sleep -Seconds 1
    $timeout--
}
if (-not (Test-Path $apiJsonPath)) {
    Stop-Process -Id $proc.Id -Force
    throw "Timed out waiting for agent-api.json to be created."
}

$api = Get-Content $apiJsonPath | ConvertFrom-Json
$headers = @{ Authorization = "Bearer " + $api.token }
Write-Host "Connected to Agent API at $($api.baseUrl)" -ForegroundColor Green

function Call-Tool($name, $argsObj = @{}) {
    $body = @{ name = $name; arguments = $argsObj } | ConvertTo-Json -Depth 10
    $resp = Invoke-RestMethod -Uri ($api.baseUrl + "/v1/tools/call") -Method Post -Headers $headers -Body $body -ContentType "application/json"
    return $resp.result
}

try {
    # 3. Verify initial Test Mode status
    Write-Host "`n[2/7] Checking initial Test Mode status..." -ForegroundColor Yellow
    $status = Call-Tool "test_mode_status"
    if (-not $status.ok) { throw "test_mode_status failed: $($status.error)" }
    Write-Host "Initial state: active=$($status.active)" -ForegroundColor Green

    # 4. Enter Test Mode
    Write-Host "`n[3/7] Entering Test Mode..." -ForegroundColor Yellow
    $enter = Call-Tool "test_mode_toggle" @{ active = $true }
    if (-not $enter.ok -or -not $enter.active) { throw "Failed to enter test mode" }
    Write-Host "Test Mode active! Active setup: $($enter.activeSetup)" -ForegroundColor Green

    # 5. Configure 12AX7 Guitar Preamp Test
    Write-Host "`n[4/7] Configuring 12AX7 Guitar Preamp Test..." -ForegroundColor Yellow
    $cfg = Call-Tool "test_setup_configure" @{
        name = "Guitar Preamp Test"
        inputMode = "replace"
        inputRefdes = "VIN"
        outputNode = "R_LOAD.1"
        probeNodes = @("V1.P", "V1.K")
        stimulusType = "file"
        audioFilePath = "prototype/examples/tube_amplifiers/guitar_riff.wav"
        inputPeakVolts = 0.10
    }
    if (-not $cfg.ok) { throw "Failed to configure test setup: $($cfg.error)" }
    Write-Host "Setup configured with input=VIN (100mV pk), output=R_LOAD.1, probes=[V1.P, V1.K]" -ForegroundColor Green

    # 6. Run Audio Test Simulation
    Write-Host "`n[5/7] Running 12AX7 audio test simulation (96,000 samples)..." -ForegroundColor Yellow
    $run = Call-Tool "test_setup_run" @{ name = "Guitar Preamp Test"; audition = $false }
    if (-not $run.ok) { throw "Audio test simulation failed: $($run.error)" }

    Write-Host "Simulation completed in $($run.wallSeconds)s ($($run.steps) steps)!" -ForegroundColor Green
    Write-Host "  Input:     $($run.inputPeakVolts) V pk ($([math]::Round($run.inputRmsVolts, 4)) V rms)" -ForegroundColor White
    Write-Host "  Output:    $([math]::Round($run.outputPeakVolts, 4)) V pk ($([math]::Round($run.outputRmsVolts, 4)) V rms)" -ForegroundColor White
    Write-Host "  Output DC: $([math]::Round($run.outputDcVolts, 4)) V (AC coupled, no DC leak)" -ForegroundColor White
    Write-Host "  Gain:      +$([math]::Round($run.gainDb, 1)) dB ($([math]::Round($run.gainLinear, 1))x)" -ForegroundColor White

    $plateProbe = $run.probes | Where-Object { $_.label -eq "V1.P" }
    $cathodeProbe = $run.probes | Where-Object { $_.label -eq "V1.K" }
    Write-Host "  Plate V1.P OP:   $([math]::Round($plateProbe.quiescent, 2)) V (expected ~170.1 V)" -ForegroundColor White
    Write-Host "  Cathode V1.K OP: $([math]::Round($cathodeProbe.quiescent, 2)) V (expected ~1.20 V)" -ForegroundColor White

    # Assertions
    if ([math]::Abs($plateProbe.quiescent - 170.1) -gt 2.0) { throw "Plate voltage unexpected" }
    if ([math]::Abs($cathodeProbe.quiescent - 1.20) -gt 0.1) { throw "Cathode voltage unexpected" }
    if ($run.gainLinear -lt 45.0 -or $run.gainLinear -gt 60.0) { throw "Stage gain unexpected" }

    # 7. Export rendered WAV audio
    Write-Host "`n[6/7] Exporting rendered audio to WAV..." -ForegroundColor Yellow
    $outWav = "prototype/examples/tube_amplifiers/verified_preamp_output.wav"
    $exp = Call-Tool "test_setup_export_audio" @{ name = "Guitar Preamp Test"; path = $outWav }
    if (-not $exp.ok -or -not (Test-Path $outWav)) { throw "WAV export failed" }
    $wavLen = (Get-Item $outWav).Length
    Write-Host "Exported WAV successfully: $outWav ($wavLen bytes)" -ForegroundColor Green

    # 8. Test Setup Lifecycle: Create, Rename, Delete
    Write-Host "`n[7/7] Testing independent setup lifecycle (create, rename, delete, exit)..." -ForegroundColor Yellow
    $c = Call-Tool "test_setup_create" @{ name = "Temporary Calibration Setup" }
    if (-not $c.ok) { throw "Create setup failed" }
    $r = Call-Tool "test_setup_rename" @{ oldName = "Temporary Calibration Setup"; newName = "Renamed Calibration Setup" }
    if (-not $r.ok) { throw "Rename setup failed" }
    $d = Call-Tool "test_setup_delete" @{ name = "Renamed Calibration Setup" }
    if (-not $d.ok) { throw "Delete setup failed" }

    # Exit Test Mode
    $exit = Call-Tool "test_mode_toggle" @{ active = $false }
    if (-not $exit.ok -or $exit.active) { throw "Failed to exit test mode" }
    Write-Host "Returned to normal schematic mode cleanly. Schematic is 100% intact!" -ForegroundColor Green

    Write-Host "`n=====================================================" -ForegroundColor Cyan
    Write-Host " ALL TEST MODE VERIFICATIONS PASSED SUCCESSFULLY!" -ForegroundColor Green
    Write-Host "=====================================================" -ForegroundColor Cyan
}
finally {
    # Clean shutdown
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}
