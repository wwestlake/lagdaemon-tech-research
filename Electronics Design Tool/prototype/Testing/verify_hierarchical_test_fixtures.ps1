# verify_hierarchical_test_fixtures.ps1
# Automated validation of Hierarchical Test Fixtures and Subdiagram Integration in DEWB.

$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " DEWB Hierarchical Test Fixtures Validation" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

$appPath = "prototype\bin\Debug\djehuti_electronics_lab.exe"
if (-not (Test-Path $appPath)) {
    throw "Application binary not found at $appPath. Build it first."
}

$diagramPath = "prototype\examples\tube_amplifiers\diagrams\12ax7_two_stage_preamp.diagram.json"
if (-not (Test-Path $diagramPath)) {
    throw "Test circuit not found at $diagramPath."
}
$initialHash = (Get-FileHash -Path $diagramPath -Algorithm SHA256).Hash
Write-Host "Authoritative diagram initial SHA256: $initialHash" -ForegroundColor Gray

# 1. Clean previous agent-api.json if any
$apiJsonPath = "$env:APPDATA\DjehutiElectronicsLab\agent-api.json"
if (Test-Path $apiJsonPath) {
    Remove-Item $apiJsonPath -Force
}

# 2. Start DEWB application process
Write-Host "`n[1/8] Launching Djehuti Electronics Workbench..." -ForegroundColor Yellow
$proc = Start-Process -FilePath $appPath -PassThru
Start-Sleep -Seconds 3

$timeout = 25
while (-not (Test-Path $apiJsonPath) -and $timeout -gt 0) {
    Start-Sleep -Seconds 1
    $timeout--
}
if (-not (Test-Path $apiJsonPath)) {
    Stop-Process -Id $proc.Id -Force
    throw "Timed out waiting for agent-api.json."
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
    # 3. Open project and two-stage preamp diagram
    Write-Host "`n[2/8] Opening Vacuum Tube Amplifiers project and diagram..." -ForegroundColor Yellow
    $projDir = (Resolve-Path "prototype\examples\tube_amplifiers").Path
    $openProj = Call-Tool "project_open" @{ project = $projDir }
    if (-not $openProj.ok) { throw "project_open failed: $($openProj.error)" }

    $openDiag = Call-Tool "diagram_open" @{ name = "12AX7 Two Stage Preamp" }
    if (-not $openDiag.ok) { throw "diagram_open failed: $($openDiag.error)" }
    Write-Host "Diagram '12AX7 Two Stage Preamp' opened successfully." -ForegroundColor Green

    # 4. Enter Test Mode
    Write-Host "`n[3/8] Entering Test Mode..." -ForegroundColor Yellow
    $enter = Call-Tool "test_mode_toggle" @{ active = $true }
    if (-not $enter.ok -or -not $enter.active) { throw "Failed to enter test mode" }
    Write-Host "Test Mode activated: $($enter.active)" -ForegroundColor Green

    # 5. Query Fixture Hierarchy
    Write-Host "`n[4/8] Querying Test Fixture Hierarchy..." -ForegroundColor Yellow
    $hier = Call-Tool "test_fixture_get_hierarchy"
    if (-not $hier.ok) { throw "test_fixture_get_hierarchy failed: $($hier.error)" }
    Write-Host "Hierarchy found $($hier.subdiagramInstances.Count) subdiagram instance(s):" -ForegroundColor Green
    foreach ($inst in $hier.subdiagramInstances) {
        Write-Host "  - Instance '$($inst.refdes)' [Sheet: $($inst.childSheet), Model: $($inst.name)]" -ForegroundColor White
        Write-Host "    Ports: $($inst.ports -join ', ')" -ForegroundColor Gray
    }
    if ($hier.subdiagramInstances.Count -ne 2) {
        throw "Expected 2 subdiagram instances (A1 and A2), found $($hier.subdiagramInstances.Count)"
    }

    # 6. Run Cascaded 2-Stage Simulation
    Write-Host "`n[5/8] Running unified 2-stage cascaded amplifier test simulation..." -ForegroundColor Yellow
    $create = Call-Tool "test_setup_create" @{ name = "Two-Stage Cascaded" }
    if (-not $create.ok) { throw "test_setup_create failed: $($create.error)" }
    $sel = Call-Tool "test_setup_select" @{ name = "Two-Stage Cascaded" }
    if (-not $sel.ok) { throw "test_setup_select failed: $($sel.error)" }

    $cfg = Call-Tool "test_setup_configure" @{
        name = "Two-Stage Cascaded"
        inputMode = "replace"
        inputRefdes = "VIN"
        outputNode = "R_LOAD.1"
        probeNodes = @("A1.V1_S1.P", "A1.OUT", "A2.V1_S2.P")
        stimulusType = "sine"
        sineFrequencyHz = 1000.0
        inputPeakVolts = 0.05
    }
    if (-not $cfg.ok) { throw "test_setup_configure failed: $($cfg.error)" }

    $runCascaded = Call-Tool "test_setup_run" @{ name = "Two-Stage Cascaded"; audition = $false }
    if (-not $runCascaded.ok) { throw "Cascaded run failed: $($runCascaded.error)" }
    Write-Host "Cascaded simulation completed:" -ForegroundColor Green
    Write-Host "  Input:     $($runCascaded.inputPeakVolts) V pk" -ForegroundColor White
    Write-Host "  Output:    $([math]::Round($runCascaded.outputPeakVolts, 4)) V pk" -ForegroundColor White
    Write-Host "  Gain:      +$([math]::Round($runCascaded.gainDb, 1)) dB ($([math]::Round($runCascaded.gainLinear, 1))x)" -ForegroundColor White
    foreach ($p in $runCascaded.probes) {
        Write-Host "  Probe $($p.label): Quiescent = $([math]::Round($p.quiescent, 2)) V, Peak = $([math]::Round($p.peak, 3)) V, RMS = $([math]::Round($p.rms, 3)) V" -ForegroundColor White
    }

    # 7. Sub-Test-Fixture Navigation & Connection Interruption
    Write-Host "`n[6/8] Navigating into Sub-Test-Fixture 'A2' and interrupting interstage connection..." -ForegroundColor Yellow
    $navA2 = Call-Tool "test_fixture_navigate" @{ path = "A2" }
    if (-not $navA2.ok) { throw "Navigate to A2 failed: $($navA2.error)" }
    Write-Host "Navigated to: $($navA2.viewedInstance) [Sheet: $($navA2.viewedSheet)]" -ForegroundColor Green

    # Break connection between A1.OUT and A2.IN
    Write-Host "Breaking connection between A1.OUT and A2.IN..." -ForegroundColor Yellow
    $break = Call-Tool "test_fixture_break_connection" @{
        nodeA = "A1.OUT"
        nodeB = "A2.IN"
    }
    if (-not $break.ok) { throw "test_fixture_break_connection failed: $($break.error)" }
    Write-Host "Connection interrupted cleanly without modifying schematic diagram." -ForegroundColor Green

    # Configure A2 Sub-Fixture with independent stimulus
    Write-Host "Configuring Sub-Fixture A2 with dedicated virtual 100 mV stimulus..." -ForegroundColor Yellow
    $subCfg = Call-Tool "test_fixture_set_subfixture" @{
        instancePath = "A2"
        stimulusEnabled = $true
        inputNode = "IN"
        inputPeakVolts = 0.10
        stimulusType = "sine"
        sineFrequencyHz = 1000.0
        outputNode = "OUT"
        probeNodes = @("V1_S2.P", "V1_S2.K")
    }
    if (-not $subCfg.ok) { throw "test_fixture_set_subfixture failed: $($subCfg.error)" }
    Write-Host "Sub-fixture A2 configured with isolated stimulus on 'IN'." -ForegroundColor Green

    # Run isolated Stage 2 test simulation
    Write-Host "`n[7/8] Running isolated Stage 2 test simulation..." -ForegroundColor Yellow
    $runStage2 = Call-Tool "test_setup_run" @{ audition = $false }
    if (-not $runStage2.ok) { throw "Stage 2 isolated run failed: $($runStage2.error)" }

    Write-Host "Isolated Stage 2 simulation completed:" -ForegroundColor Green
    Write-Host "  Sub-Fixture Input:  $($runStage2.inputPeakVolts) V pk" -ForegroundColor White
    Write-Host "  Sub-Fixture Output: $([math]::Round($runStage2.outputPeakVolts, 4)) V pk" -ForegroundColor White
    Write-Host "  Single-Stage Gain:  +$([math]::Round($runStage2.gainDb, 1)) dB ($([math]::Round($runStage2.gainLinear, 1))x)" -ForegroundColor White

    # Assert Stage 2 alone has single-stage gain (~53x / ~34.5 dB)
    if ($runStage2.gainLinear -lt 45.0 -or $runStage2.gainLinear -gt 60.0) {
        throw "Unexpected single-stage gain on isolated A2: $($runStage2.gainLinear)"
    }
    Write-Host "Stage 2 independently verified! (Gain = $([math]::Round($runStage2.gainLinear, 1))x within expected [45..60] range)" -ForegroundColor Green

    # Restore connection
    Write-Host "`n[8/8] Restoring connection and verifying schematic non-destructive guarantee..." -ForegroundColor Yellow
    $restore = Call-Tool "test_fixture_restore_connection" @{ all = $true }
    if (-not $restore.ok) { throw "test_fixture_restore_connection failed: $($restore.error)" }
    Write-Host "Restored all connections. Remaining breaks: $($restore.remainingBreaks)" -ForegroundColor Green

    # Navigate back to Main
    $navMain = Call-Tool "test_fixture_navigate" @{ path = "Main" }
    if (-not $navMain.ok) { throw "Navigate to Main failed" }
    Write-Host "Navigated back to Main fixture." -ForegroundColor Green

    # Exit Test Mode
    $exit = Call-Tool "test_mode_toggle" @{ active = $false }
    if (-not $exit.ok -or $exit.active) { throw "Failed to exit test mode" }
    Write-Host "Test Mode deactivated." -ForegroundColor Green

    # Verify SHA256 of schematic file
    $finalHash = (Get-FileHash -Path $diagramPath -Algorithm SHA256).Hash
    Write-Host "Authoritative diagram final SHA256:   $finalHash" -ForegroundColor Gray
    if ($initialHash -ne $finalHash) {
        throw "CRITICAL FAILURE: Authoritative schematic file was modified during testing!"
    }
    Write-Host "NON-DESTRUCTIVE GUARANTEE CONFIRMED: Schematic file is 100% byte-for-byte identical!" -ForegroundColor Green

    Write-Host "`n==========================================================" -ForegroundColor Cyan
    Write-Host " ALL HIERARCHICAL TEST FIXTURE CHECKS PASSED!" -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Cyan
}
finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}
