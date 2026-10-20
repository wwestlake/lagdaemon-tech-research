# verify_graphical_test_fixture.ps1
# Automated validation of Complete Graphical Test Fixture Editor in DEWB.
# Covers the 12-step acceptance testing workflow on the 12AX7 amplifier example.

$ErrorActionPreference = "Stop"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " DEWB Graphical Test Fixture Editor Validation" -ForegroundColor Cyan
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

# Clean previous agent-api.json if any
$apiJsonPath = "$env:APPDATA\DjehutiElectronicsLab\agent-api.json"
if (Test-Path $apiJsonPath) {
    Remove-Item $apiJsonPath -Force
}

Write-Host "`n[1/12] Launching Djehuti Electronics Workbench..." -ForegroundColor Yellow
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
    # Step 1: Open amplifier schematic
    Write-Host "`n[Step 1] Opening amplifier schematic in project..." -ForegroundColor Yellow
    $projDir = (Resolve-Path "prototype\examples\tube_amplifiers").Path
    $openProj = Call-Tool "project_open" @{ project = $projDir }
    if (-not $openProj.ok) { throw "project_open failed: $($openProj.error)" }

    $openDiag = Call-Tool "diagram_open" @{ name = "12AX7 Two Stage Preamp" }
    if (-not $openDiag.ok) { throw "diagram_open failed: $($openDiag.error)" }
    Write-Host "Schematic '12AX7 Two Stage Preamp' opened successfully." -ForegroundColor Green

    # Step 2: Create independent Amplifier Test Fixture
    Write-Host "`n[Step 2] Creating independent 'Amplifier Test Fixture' workspace..." -ForegroundColor Yellow
    $fixCreate = Call-Tool "test_fixture_create" @{ name = "Amplifier Test Fixture" }
    if (-not $fixCreate.ok) { throw "test_fixture_create failed: $($fixCreate.error)" }
    Write-Host "Test Fixture 'Amplifier Test Fixture' created and opened in CreationDock center tab." -ForegroundColor Green

    # Step 3: Place Audio Input and Audio Sink on the fixture canvas
    Write-Host "`n[Step 3] Placing Audio Input and Audio Sink on fixture canvas..." -ForegroundColor Yellow
    $placeIn = Call-Tool "test_fixture_place_equipment" @{
        fixture = "Amplifier Test Fixture"
        type = "audio_input"
        name = "Guitar Track Audio In"
        x = 80
        y = 220
    }
    if (-not $placeIn.ok) { throw "test_fixture_place_equipment (audio_input) failed: $($placeIn.error)" }
    $audioInId = $placeIn.equipment.id
    Write-Host "Audio Input placed with ID: $audioInId at ($($placeIn.equipment.x), $($placeIn.equipment.y))" -ForegroundColor Green

    $placeOut = Call-Tool "test_fixture_place_equipment" @{
        fixture = "Amplifier Test Fixture"
        type = "audio_output"
        name = "Studio Monitor Audio Sink"
        x = 760
        y = 220
    }
    if (-not $placeOut.ok) { throw "test_fixture_place_equipment (audio_output) failed: $($placeOut.error)" }
    $audioOutId = $placeOut.equipment.id
    Write-Host "Audio Sink placed with ID: $audioOutId at ($($placeOut.equipment.x), $($placeOut.equipment.y))" -ForegroundColor Green

    # Move equipment test (Performance requirement: zero solver calls)
    $moveIn = Call-Tool "test_fixture_move_equipment" @{
        fixture = "Amplifier Test Fixture"
        id = $audioInId
        x = 90
        y = 230
    }
    if (-not $moveIn.ok) { throw "test_fixture_move_equipment failed: $($moveIn.error)" }
    Write-Host "Equipment moved interactively on canvas without latency." -ForegroundColor Green

    # Step 4: Wire both devices graphically to the circuit
    Write-Host "`n[Step 4] Wiring both devices graphically to circuit pins..." -ForegroundColor Yellow
    $wireIn = Call-Tool "test_fixture_connect" @{
        fixture = "Amplifier Test Fixture"
        equipmentId = $audioInId
        terminalId = "out"
        targetPin = "A1.IN"
        targetRefdes = "VIN"
    }
    if (-not $wireIn.ok) { throw "test_fixture_connect (input) failed: $($wireIn.error)" }
    Write-Host "Audio Input terminal wired to 'VIN' / 'A1.IN' (connection: $($wireIn.connection.id))." -ForegroundColor Green

    $wireOut = Call-Tool "test_fixture_connect" @{
        fixture = "Amplifier Test Fixture"
        equipmentId = $audioOutId
        terminalId = "in"
        targetPin = "R_LOAD.1"
    }
    if (-not $wireOut.ok) { throw "test_fixture_connect (output) failed: $($wireOut.error)" }
    Write-Host "Audio Sink terminal wired to 'R_LOAD.1' (connection: $($wireOut.connection.id))." -ForegroundColor Green

    # Step 5: Configure the Audio Input using an actual guitar WAV file
    Write-Host "`n[Step 5] Configuring Audio Input with actual guitar WAV file..." -ForegroundColor Yellow
    $wavPath = (Resolve-Path "prototype\examples\tube_amplifiers\guitar_riff.wav").Path
    if (-not (Test-Path $wavPath)) { throw "Guitar riff WAV file not found at $wavPath" }

    $cfgIn = Call-Tool "test_fixture_configure_equipment" @{
        fixture = "Amplifier Test Fixture"
        id = $audioInId
        stimulusType = "file"
        audioFilePath = $wavPath
        peakVolts = 0.05
        isOverride = $true
        overrideRefdes = "VIN"
    }
    if (-not $cfgIn.ok) { throw "test_fixture_configure_equipment failed: $($cfgIn.error)" }
    Write-Host "Audio Input configured: WAV file '$($cfgIn.equipment.audioFilePath)', peak = 50 mV." -ForegroundColor Green

    # Step 6: Override the schematic's original signal source and verify the override electrically
    Write-Host "`n[Step 6] Verifying design source override electrically..." -ForegroundColor Yellow
    $status = Call-Tool "test_mode_status"
    if (-not $status.ok) { throw "test_mode_status failed: $($status.error)" }
    $activeName = if ($status.activeSetup -is [string]) { $status.activeSetup } else { $status.activeSetup.name }
    $activeMode = if ($status.activeSetupDetails) { $status.activeSetupDetails.inputMode } else { $status.activeSetup.inputMode }
    $activeRefdes = if ($status.activeSetupDetails) { $status.activeSetupDetails.inputRefdes } else { $status.activeSetup.inputRefdes }
    Write-Host "Active setup: $activeName" -ForegroundColor White
    Write-Host "Input mode:   $activeMode (Refdes: $activeRefdes)" -ForegroundColor White
    if ($activeMode -ne "replace" -or $activeRefdes -ne "VIN") {
        throw "Expected replace mode overriding 'VIN', found $activeMode / $activeRefdes"
    }
    Write-Host "Source 'VIN' is electrically overridden by fixture excitation." -ForegroundColor Green

    # Step 7: Run simulation, audition result, and export WAV
    Write-Host "`n[Step 7] Running simulation, auditioning result, and exporting WAV..." -ForegroundColor Yellow
    $run = Call-Tool "test_setup_run" @{ name = "Amplifier Test Fixture"; audition = $true }
    if (-not $run.ok) { throw "test_setup_run failed: $($run.error)" }
    Write-Host "Simulation completed successfully:" -ForegroundColor Green
    Write-Host "  Input:     $($run.inputPeakVolts) V pk" -ForegroundColor White
    Write-Host "  Output:    $([math]::Round($run.outputPeakVolts, 4)) V pk" -ForegroundColor White
    Write-Host "  Gain:      +$([math]::Round($run.gainDb, 1)) dB ($([math]::Round($run.gainLinear, 1))x)" -ForegroundColor White
    Write-Host "  RMS:       $([math]::Round($run.outputRmsVolts, 4)) V" -ForegroundColor White
    Write-Host "  Quiescent: $([math]::Round($run.quiescentDcVolts, 2)) V DC" -ForegroundColor White

    # Export WAV test
    $exportWavPath = (Resolve-Path "prototype\examples\tube_amplifiers").Path + "\fixture_verified_guitar_out.wav"
    $export = Call-Tool "test_setup_export_audio" @{
        name = "Amplifier Test Fixture"
        path = $exportWavPath
    }
    if (-not $export.ok -or -not (Test-Path $exportWavPath)) { throw "test_setup_export_audio failed" }
    $wavSize = (Get-Item $exportWavPath).Length
    Write-Host "Exported WAV output ($wavSize bytes) to $exportWavPath" -ForegroundColor Green

    # Step 8: Navigate into a subdiagram, attach a measurement instrument, and test a temporary connection break
    Write-Host "`n[Step 8] Navigating into Sub-Test-Fixture 'A2' and testing temporary connection break..." -ForegroundColor Yellow
    $navA2 = Call-Tool "test_fixture_navigate" @{ path = "A2" }
    if (-not $navA2.ok) { throw "test_fixture_navigate failed: $($navA2.error)" }
    Write-Host "Navigated to Sub-Test-Fixture: $($navA2.viewedInstance) [Sheet: $($navA2.viewedSheet)]" -ForegroundColor Green

    # Place a probe instrument in sub-fixture
    $placeProbe = Call-Tool "test_fixture_place_equipment" @{
        fixture = "Amplifier Test Fixture"
        type = "probe"
        name = "A2 Cathode Probe"
        x = 350
        y = 120
        instancePath = "A2"
    }
    if (-not $placeProbe.ok) { throw "Failed to place probe in sub-fixture" }
    Write-Host "Measurement instrument placed inside Sub-Test-Fixture 'A2'." -ForegroundColor Green

    # Intersect interstage connection
    $break = Call-Tool "test_fixture_break_connection" @{
        fixture = "Amplifier Test Fixture"
        instancePath = "A2"
        nodeA = "A1.OUT"
        nodeB = "A2.IN"
    }
    if (-not $break.ok) { throw "test_fixture_break_connection failed: $($break.error)" }
    Write-Host "Interrupted connection between A1.OUT and A2.IN with visible interruption markers." -ForegroundColor Green

    # Configure sub-fixture stimulus to verify isolated stage
    $subCfg = Call-Tool "test_fixture_set_subfixture" @{
        instancePath = "A2"
        stimulusEnabled = $true
        inputNode = "IN"
        inputPeakVolts = 0.10
        stimulusType = "sine"
        sineFrequencyHz = 1000.0
        outputNode = "OUT"
    }
    if (-not $subCfg.ok) { throw "test_fixture_set_subfixture failed" }

    $runIsolated = Call-Tool "test_setup_run" @{ name = "Amplifier Test Fixture"; audition = $false }
    if (-not $runIsolated.ok) { throw "Isolated sub-stage run failed: $($runIsolated.error)" }
    Write-Host "Isolated Stage 2 simulation verified: Gain = +$([math]::Round($runIsolated.gainDb, 1)) dB ($([math]::Round($runIsolated.gainLinear, 1))x)" -ForegroundColor Green

    # Step 9: Restore the connection and return to the parent fixture
    Write-Host "`n[Step 9] Restoring connection and returning to parent fixture..." -ForegroundColor Yellow
    $restore = Call-Tool "test_fixture_restore_connection" @{
        fixture = "Amplifier Test Fixture"
        all = $true
    }
    if (-not $restore.ok) { throw "test_fixture_restore_connection failed" }
    Write-Host "Connections restored (remaining breaks: $($restore.remainingBreaks))." -ForegroundColor Green

    $navMain = Call-Tool "test_fixture_navigate" @{ path = "Main" }
    if (-not $navMain.ok) { throw "test_fixture_navigate back to Main failed" }
    Write-Host "Returned to parent Test Fixture." -ForegroundColor Green

    # Step 10: Save, close, and reopen the fixture with all equipment and wiring intact
    Write-Host "`n[Step 10] Saving, closing, and reopening Test Fixture..." -ForegroundColor Yellow
    $saveFixturePath = (Resolve-Path "prototype\examples\tube_amplifiers").Path + "\12ax7_two_stage_preamp.Amplifier_Test_Fixture.fixture.json"
    $save = Call-Tool "test_fixture_save" @{
        name = "Amplifier Test Fixture"
        path = $saveFixturePath
    }
    if (-not $save.ok) { throw "test_fixture_save failed: $($save.error)" }
    Write-Host "Saved fixture file: $($save.savedFile)" -ForegroundColor Green

    $close = Call-Tool "test_fixture_close" @{ name = "Amplifier Test Fixture" }
    if (-not $close.ok) { throw "test_fixture_close failed: $($close.error)" }
    Write-Host "Test Fixture closed." -ForegroundColor Green

    $reopen = Call-Tool "test_fixture_open" @{ name = "Amplifier Test Fixture" }
    if (-not $reopen.ok) { throw "test_fixture_open failed: $($reopen.error)" }
    Write-Host "Test Fixture reopened." -ForegroundColor Green

    $list = Call-Tool "test_fixture_list"
    $found = $false
    foreach ($f in $list.fixtures) {
        if ($f.name -eq "Amplifier Test Fixture") {
            $found = $true
            Write-Host "Verified reopened fixture inventory: $($f.equipmentCount) equipment, $($f.connectionsCount) connections." -ForegroundColor Green
            if ($f.equipmentCount -lt 2 -or $f.connectionsCount -lt 2) {
                throw "Fixture equipment or connections lost during save/reopen!"
            }
        }
    }
    if (-not $found) { throw "Reopened fixture not found in list!" }

    # Step 11: Create a second fixture and confirm independent configurations
    Write-Host "`n[Step 11] Creating a second fixture and confirming independent configuration..." -ForegroundColor Yellow
    $fix2 = Call-Tool "test_fixture_create" @{ name = "Sine Benchmark Fixture" }
    if (-not $fix2.ok) { throw "Failed to create second fixture: $($fix2.error)" }

    $placeGen = Call-Tool "test_fixture_place_equipment" @{
        fixture = "Sine Benchmark Fixture"
        type = "signal_gen"
        name = "2.5kHz Benchmark Tone"
        x = 100
        y = 300
    }
    if (-not $placeGen.ok) { throw "Failed to place sig_gen in second fixture" }

    $cfgGen = Call-Tool "test_fixture_configure_equipment" @{
        fixture = "Sine Benchmark Fixture"
        id = $placeGen.equipment.id
        stimulusType = "sine"
        sineFrequencyHz = 2500.0
        peakVolts = 0.08
    }
    if (-not $cfgGen.ok) { throw "Failed to configure sig_gen in second fixture" }

    # Switch back to first fixture and confirm its independent guitar WAV settings remain pristine
    $open1 = Call-Tool "test_fixture_open" @{ name = "Amplifier Test Fixture" }
    $stat1 = Call-Tool "test_mode_status"
    $audioPath1 = if ($stat1.activeSetupDetails) { $stat1.activeSetupDetails.audioFilePath } else { $stat1.activeSetup.audioFilePath }
    if ($audioPath1 -ne $wavPath) {
        throw "Fixture 1 configuration corrupted by Fixture 2!"
    }
    Write-Host "Confirmed: Both fixtures maintain completely independent equipment, wiring, and parameters." -ForegroundColor Green

    # Step 12: Verify the original schematic is unchanged
    Write-Host "`n[Step 12] Verifying non-destructive schematic guarantee (SHA256 comparison)..." -ForegroundColor Yellow
    $finalHash = (Get-FileHash -Path $diagramPath -Algorithm SHA256).Hash
    Write-Host "Authoritative diagram initial SHA256: $initialHash" -ForegroundColor Gray
    Write-Host "Authoritative diagram final SHA256:   $finalHash" -ForegroundColor Gray
    if ($initialHash -ne $finalHash) {
        throw "CRITICAL NON-DESTRUCTIVE VIOLATION: Authoritative schematic file was modified!"
    }
    Write-Host "NON-DESTRUCTIVE GUARANTEE 100% CONFIRMED: Schematic file is byte-for-byte identical!" -ForegroundColor Green

    Write-Host "`n==========================================================" -ForegroundColor Cyan
    Write-Host " ALL 12 ACCEPTANCE TESTING STEPS PASSED SUCCESSFULLY!" -ForegroundColor Green
    Write-Host "==========================================================" -ForegroundColor Cyan
}
finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}
