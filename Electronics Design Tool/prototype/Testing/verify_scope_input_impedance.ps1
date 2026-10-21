param(
    [string]$Repository = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$WorkRoot
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
if (Get-Process -Name djehuti_electronics_lab -ErrorAction SilentlyContinue) {
    throw 'An existing DEWB session is running. This check only launches and closes its own isolated session.'
}
$checks = [System.Collections.Generic.List[object]]::new()
$records = [ordered]@{}
$app = $null
$qaProjectOpened = $false
$cleanup = @{ terminated = $false; forced = $false; normalCloseAttempted = $false }
function Check([bool]$ok, [string]$name) {
    $checks.Add([pscustomobject]@{ pass = $ok; name = $name })
    if (-not $ok) { throw "Check failed: $name" }
}
function Call([string]$name, [hashtable]$arguments = @{}) {
    $body = @{ name = $name; arguments = $arguments } | ConvertTo-Json -Depth 15
    try { $response = Invoke-RestMethod -Uri ($api.baseUrl + '/v1/tools/call') -Method Post -Headers $headers -Body $body -ContentType 'application/json' -TimeoutSec 45 }
    catch { if (-not $_.ErrorDetails.Message) { throw }; $response = $_.ErrorDetails.Message | ConvertFrom-Json; if (-not $response.result) { throw } }
    $r = $response.result
    if ($r -is [string]) { $r = $r | ConvertFrom-Json }
    return $r
}
function Scope([string]$action, [hashtable]$settings = @{}) {
    $argsObj = @{ refdes = $script:scopeId; action = $action }
    if ($script:fixture) { $argsObj.fixture = $script:fixture }
    if ($settings.Count) { $argsObj.settings = $settings }
    return Call 'oscilloscope_control' $argsObj
}
function Gain([double]$ohms) {
    if ($ohms -eq 0) { return 0.5 } # Off: genuinely absent load, not a large resistor.
    $lower = 1.0 / (1.0 / 1000.0 + 1.0 / $ohms)
    return $lower / (1000.0 + $lower)
}
function Capture([string]$key, [double]$z1, [double]$z2) {
    Check (Scope 'run').ok "$key acquisition solves the actual circuit"
    $r = Scope 'read'
    $records[$key] = $r
    Check $r.ok "$key produces scope measurements"
    $tolerance = 0.000003
    for ($ch = 0; $ch -lt 2; ++$ch) {
        $z = @($z1, $z2)[$ch]
        $gain = Gain $z
        Check ([Math]::Abs($r.channels[$ch].vpp - 2*$gain) -lt $tolerance) "$key CH$($ch+1) Vpp matches independent loaded-divider formula"
        if (-not $script:fixture) {
            # Record endpoint rounding may leave slightly less than an integer
            # cycle. Its visible-window mean includes that genuine AC remainder;
            # use the observed extrema midpoint for the known DC bias instead.
            $dcMidpoint = ($r.channels[$ch].minimum + $r.channels[$ch].maximum) / 2
            Check ([Math]::Abs($dcMidpoint - 10*$gain) -lt $tolerance) "$key CH$($ch+1) DC midpoint agrees with loaded-divider formula"
        }
    }
    return $r
}
try {
    $project = Join-Path $WorkRoot 'divider'
    New-Item -ItemType Directory -Force -Path (Join-Path $project 'diagrams') | Out-Null
    @{ schema = 'djehuti-electronics-project'; version = 1; name = 'Scope Input Impedance QA'; lastDiagram = 'Divider'; diagrams = @(@{ name = 'Divider'; file = 'diagrams/divider.diagram.json' }) } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $project 'project.json')
    $diagram = Join-Path $project 'diagrams/divider.diagram.json'
    $components = @(
        @{ id = 'VTEST'; symbol = 'ac_voltage_source'; x = 100; y = 100; value = '1'; frequency = '1000'; params = @{ waveform = 'Sine'; offset = '10' } },
        @{ id = 'R1'; symbol = 'resistor'; x = 300; y = 100; value = '1k' },
        @{ id = 'R2'; symbol = 'resistor'; x = 500; y = 200; value = '1k'; rotation = 90 },
        @{ id = 'R3'; symbol = 'resistor'; x = 300; y = 300; value = '1k' },
        @{ id = 'R4'; symbol = 'resistor'; x = 500; y = 400; value = '1k'; rotation = 90 },
        @{ id = 'GND1'; symbol = 'ground'; x = 100; y = 500; value = '0' },
        @{ id = 'SCOPE1'; symbol = 'oscilloscope_2ch'; x = 700; y = 100; value = '2ch' }
    )
    $wires = @(
        @{ a = 'VTEST.+'; b = 'R1.1' }, @{ a = 'R1.2'; b = 'R2.1' }, @{ a = 'R2.2'; b = 'GND1.0' },
        @{ a = 'VTEST.+'; b = 'R3.1' }, @{ a = 'R3.2'; b = 'R4.1' }, @{ a = 'R4.2'; b = 'GND1.0' },
        @{ a = 'VTEST.-'; b = 'GND1.0' }, @{ a = 'SCOPE1.CH1'; b = 'R2.1' },
        @{ a = 'SCOPE1.CH2'; b = 'R4.1' }, @{ a = 'SCOPE1.REF'; b = 'GND1.0' }
    )
    @{ schemaVersion = 2; kind = 'electronics_circuit'; components = $components; wires = $wires } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $diagram
    $started = Get-Date
    $app = Start-Process -FilePath (Join-Path $Repository 'bin/Debug/djehuti_electronics_lab.exe') -WorkingDirectory $Repository -WindowStyle Hidden -PassThru
    $apiPath = Join-Path $env:APPDATA 'DjehutiElectronicsLab/agent-api.json'
    $deadline = (Get-Date).AddSeconds(35)
    do {
        Start-Sleep -Milliseconds 400
        if ($app.HasExited) { throw 'Application exited during startup.' }
        $ready = (Test-Path -LiteralPath $apiPath) -and (Get-Item -LiteralPath $apiPath).LastWriteTime -ge $started.AddSeconds(-1)
    } until ($ready -or (Get-Date) -gt $deadline)
    if (-not $ready) { throw 'Agent API did not become ready.' }
    $api = Get-Content -LiteralPath $apiPath -Raw | ConvertFrom-Json
    $headers = @{ Authorization = 'Bearer ' + $api.token }
    Check (Call 'project_open' @{ project = $project }).ok 'Open isolated analytic divider project'
    $qaProjectOpened = $true
    $script:fixture = ''; $script:scopeId = 'SCOPE1'
    Check (Scope 'configure' @{ capture_duration = '10m'; capture_step = '10u'; time_per_div = '1m' }).ok 'Configure existing acquisition controls'
    Check (Scope 'open').ok 'Open production scope window'
    $nativeDefault = Capture 'nativeDefault10M' 1e7 1e7
    Check ($nativeDefault.channels[0].impedanceEnabled -and $nativeDefault.channels[1].finiteImpedanceOhms -eq 1e7) 'Legacy native defaults remain 10 megohm per channel'
    Check (Scope 'configure' @{ ch1_impedance_enabled = 'Off'; ch2_impedance_enabled = 'Off' }).ok 'Set native channels to infinite impedance'
    $nativeIdeal = Capture 'nativeIdeal' 0 0
    Check (Scope 'configure' @{ ch1_impedance = '50'; ch1_impedance_enabled = 'On'; ch2_impedance = '1k'; ch2_impedance_enabled = 'On' }).ok 'Set independent native 50 ohm and 1 kilohm inputs'
    $nativeCustom = Capture 'nativeCustom' 50 1000
    Check (Scope 'configure' @{ ch1_impedance_enabled = 'Off' }).ok 'Disable native CH1 load without changing CH2'
    $nativeDisabled = Capture 'nativeDisabled' 0 1000
    Check ($nativeDisabled.channels[0].finiteImpedanceOhms -eq 50 -and $null -eq $nativeDisabled.channels[0].inputImpedanceOhms) 'Native Off reports infinity and retains 50 ohm'
    Scope 'configure' @{ ch1_impedance_enabled = 'On' } | Out-Null
    $nativeRestored = Capture 'nativeRestored' 50 1000
    foreach ($invalid in @('0', '-50', 'abc', '50banana', '1kV', 'nan', 'inf', '1e999')) {
        $bad = Scope 'configure' @{ ch1_impedance = $invalid; ch2_impedance_enabled = 'Off' }
        Check (-not $bad.ok -and $bad.error -match 'greater than zero') "Reject invalid native resistance $invalid with feedback"
    }
    $afterBad = Scope 'read'
    Check ($afterBad.channels[0].finiteImpedanceOhms -eq 50 -and $afterBad.channels[1].impedanceEnabled) 'Rejected request preserves both channel settings'
    Scope 'configure' @{ ch1_enabled = 'Off'; ch1_coupling = 'AC'; cursors = 'On'; cursor_t1 = '1m'; cursor_t2 = '2m' } | Out-Null
    $displayOnly = Scope 'read'
    Check ($displayOnly.acquisitions -eq $afterBad.acquisitions -and $displayOnly.channels[0].impedanceEnabled -and -not $displayOnly.channels[0].enabled) 'Trace and coupling controls preserve electrical load and reuse samples'
    Check ([Math]::Abs($displayOnly.cursors.frequencyHz - 1000) -lt 0.001 -and [Math]::Abs($displayOnly.channels[0].frequencyHz - 1000) -lt 0.01) 'Existing cursor and frequency measurements remain functional'
    Scope 'configure' @{ ch1_enabled = 'On'; ch1_coupling = 'DC'; ch2_impedance_enabled = 'Off' } | Out-Null
    Check (Call 'diagram_save').ok 'Save native independent On/Off states and resistance values'
    Check (Call 'project_open' @{ project = $project }).ok 'Reopen saved native diagram from disk'
    $nativeReopen = Capture 'nativeReopen' 50 0
    Check ($nativeReopen.channels[0].finiteImpedanceOhms -eq 50 -and -not $nativeReopen.channels[1].impedanceEnabled -and $nativeReopen.channels[1].finiteImpedanceOhms -eq 1000) 'Native save/reopen preserves both independent settings'
    # Native instrument remains present, explicitly ideal, so it adds no hidden
    # legacy 10M load beneath fixture equipment on the same circuit.
    Scope 'configure' @{ ch1_impedance_enabled = 'Off'; ch2_impedance_enabled = 'Off' } | Out-Null
    Call 'diagram_save' | Out-Null
    $authoritativeHash = (Get-FileHash -LiteralPath $diagram).Hash
    $script:fixture = 'Input Impedance QA ' + [guid]::NewGuid().ToString('N').Substring(0, 8)
    Check (Call 'test_fixture_create' @{ name = $script:fixture }).ok 'Create independent fixture'
    $inputId = (Call 'test_fixture_place_equipment' @{ fixture = $script:fixture; type = 'audio_input'; x = 50; y = 80 }).equipment.id
    $outputId = (Call 'test_fixture_place_equipment' @{ fixture = $script:fixture; type = 'audio_output'; x = 800; y = 400 }).equipment.id
    $script:scopeId = (Call 'test_fixture_place_equipment' @{ fixture = $script:fixture; type = 'scope'; x = 800; y = 80 }).equipment.id
    foreach ($lead in @(
        @($inputId, 'out', 'VTEST.+'), @($inputId, 'gnd', 'VTEST.-'), @($outputId, 'in', 'R2.1'),
        @($script:scopeId, 'in', 'R2.1'), @($script:scopeId, 'ch2', 'R4.1'), @($script:scopeId, 'ref', 'VTEST.-')
    )) {
        Check (Call 'test_fixture_connect' @{ fixture = $script:fixture; equipmentId = $lead[0]; terminalId = $lead[1]; targetPin = $lead[2] }).ok "Connect fixture $($lead[1]) to $($lead[2])"
    }
    Check (Call 'test_fixture_configure_equipment' @{ fixture = $script:fixture; id = $inputId; stimulusType = 'sine'; sineFrequencyHz = 1000; peakVolts = 1; isOverride = $true; overrideRefdes = 'VTEST' }).ok 'Configure actual fixture sine stimulus through existing source'
    Scope 'configure' @{ time_per_div = '1m' } | Out-Null
    Check (Scope 'open').ok 'Open same production scope UI in fixture'
    $fixtureIdeal = Capture 'fixtureIdeal' 0 0
    Check (-not $fixtureIdeal.channels[0].impedanceEnabled -and $fixtureIdeal.channels[0].finiteImpedanceOhms -eq 1e7) 'Legacy fixture defaults remain ideal with 10M stored finite default'
    Scope 'configure' @{ ch1_impedance_enabled = 'On'; ch2_impedance_enabled = 'On' } | Out-Null
    Check (-not (Scope 'read').ok) 'Fixture load change cannot present stale unloaded samples as current'
    $fixture10M = Capture 'fixture10M' 1e7 1e7
    Scope 'configure' @{ ch1_impedance = '50'; ch2_impedance = '1k' } | Out-Null
    $fixtureCustom = Capture 'fixtureCustom' 50 1000
    Scope 'configure' @{ ch1_impedance_enabled = 'Off' } | Out-Null
    $fixtureDisabled = Capture 'fixtureDisabled' 0 1000
    Scope 'configure' @{ ch1_impedance_enabled = 'On' } | Out-Null
    $fixtureRestored = Capture 'fixtureRestored' 50 1000
    Check ($fixtureRestored.channels[0].finiteImpedanceOhms -eq 50) 'Fixture disabling and reenabling restores previous finite value'
    Scope 'configure' @{ ch2_impedance_enabled = 'Off' } | Out-Null
    $sidecar = Join-Path $WorkRoot 'impedance.fixture.json'
    Check (Call 'test_fixture_save' @{ name = $script:fixture; path = $sidecar }).ok 'Save fixture sidecar settings'
    Scope 'configure' @{ ch1_impedance = '1G'; ch1_impedance_enabled = 'Off'; ch2_impedance_enabled = 'On' } | Out-Null
    Check (Call 'test_fixture_open' @{ name = $script:fixture; path = $sidecar }).ok 'Reload saved fixture sidecar from disk'
    $fixtureReopen = Capture 'fixtureReopen' 50 0
    Check ($fixtureReopen.channels[0].impedanceEnabled -and $fixtureReopen.channels[0].finiteImpedanceOhms -eq 50 -and -not $fixtureReopen.channels[1].impedanceEnabled -and $fixtureReopen.channels[1].finiteImpedanceOhms -eq 1000) 'Fixture save/reopen restores both independent On/Off states and values'
    # Reference is a real measurement terminal, not an assumed ground.
    Call 'test_fixture_connect' @{ fixture = $script:fixture; equipmentId = $script:scopeId; terminalId = 'ref'; targetPin = 'VTEST.+' } | Out-Null
    Check (Scope 'run').ok 'Acquire fixture with a nonground REF terminal'
    $differential = Scope 'read'; $records.fixtureDifferential = $differential
    $upper = 1.0 / (1.0 / 1000.0 + 1.0 / 50.0)
    Check ([Math]::Abs($differential.channels[0].vpp - 2*$upper/(1000+$upper)) -lt 0.000003) 'Finite impedance loads between channel and actual REF, verified electrically'
    Check ((Get-FileHash -LiteralPath $diagram).Hash -eq $authoritativeHash) 'All fixture impedance operations preserve authoritative schematic bytes'
    Scope 'stop' | Out-Null
} finally {
    if ($null -ne $app) {
        # Only this generated QA project is eligible for a cleanup save. Saving
        # also avoids leaving an unsaved-change prompt after an early failure.
        if ($qaProjectOpened -and -not $app.HasExited) {
            try { Call 'diagram_save' | Out-Null } catch { Write-Warning 'Could not save temporary QA diagram before closing.' }
        }
        $app.Refresh()
        # Close owned floating windows and the main window normally. Each refresh
        # discovers the remaining main handle; no unrelated process is targeted.
        for ($attempt = 0; $attempt -lt 8 -and -not $app.HasExited; ++$attempt) {
            $cleanup.normalCloseAttempted = $true
            $app.CloseMainWindow() | Out-Null
            $app.WaitForExit(900) | Out-Null
            $app.Refresh()
        }
        if (-not $app.HasExited) {
            Write-Warning "Normal close did not terminate owned QA process $($app.Id); terminating only that instance."
            $cleanup.forced = $true
            Stop-Process -Id $app.Id
            $app.WaitForExit(5000) | Out-Null
        }
        $cleanup.terminated = $app.HasExited
        $cleanup.processId = $app.Id
    }
    @{ checks = $checks.Count; results = $checks; records = $records; cleanup = $cleanup; divider = @{ sourceDc = 10; sourcePeak = 1; upperOhms = 1000; lowerOhms = 1000 } } | ConvertTo-Json -Depth 25 | Set-Content -LiteralPath (Join-Path $WorkRoot 'impedance-results.json')
}
if (-not $cleanup.terminated) { throw 'Owned application did not terminate.' }
Write-Output "$($checks.Count) input-impedance checks passed. Owned QA application process $($app.Id) terminated (forced=$($cleanup.forced))."
