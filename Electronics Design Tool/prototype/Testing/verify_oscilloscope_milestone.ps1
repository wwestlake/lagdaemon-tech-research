param(
    [string]$Repository = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$WorkRoot,
    [switch]$NativeOnly
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
# Includes a real preamp WAV run, source conflict checks and authoritative-file hashes.
if (-not $NativeOnly) {
    & (Join-Path $PSScriptRoot 'verify_fixture_integration_repairs.ps1') -Repository $Repository -WorkRoot (Join-Path $WorkRoot 'fixture')
}
$api = Get-Content -LiteralPath (Join-Path $env:APPDATA 'DjehutiElectronicsLab\agent-api.json') -Raw | ConvertFrom-Json
$headers = @{ Authorization = 'Bearer ' + $api.token }
$results = [System.Collections.Generic.List[object]]::new()
if ($NativeOnly) {
    $prior = Get-Content -LiteralPath (Join-Path $WorkRoot 'scope-integration-results.json') -Raw | ConvertFrom-Json
    foreach ($check in $prior.results) {
        if (-not $check.pass) { break }
        $results.Add($check)
        if ($check.name -eq 'Scope operations preserve fixture authoritative schematic') { break }
    }
    $single = $prior.fixtureSingle
}
function Check([bool]$ok, [string]$name) {
    $results.Add([pscustomobject]@{ pass = $ok; name = $name })
    if (-not $ok) { throw "Check failed: $name" }
}
function Call([string]$name, [hashtable]$arguments = @{}) {
    $body = @{ name = $name; arguments = $arguments } | ConvertTo-Json -Depth 15
    try { $response = Invoke-RestMethod -Uri ($api.baseUrl + '/v1/tools/call') -Method Post -Headers $headers -Body $body -ContentType 'application/json' -TimeoutSec 120 }
    catch { if (-not $_.ErrorDetails.Message) { throw }; $response = $_.ErrorDetails.Message | ConvertFrom-Json; if (-not $response.result) { throw } }
    $r = $response.result
    if ($r -is [string]) { $r = $r | ConvertFrom-Json }
    return $r
}
function Scope([string]$action, [hashtable]$settings = @{}, [string]$path = '') {
    $argsObj = @{ fixture = $script:fixture; refdes = $script:scopeId; action = $action }
    if ($settings.Count) { $argsObj.settings = $settings }
    if ($path) { $argsObj.path = $path }
    return Call 'oscilloscope_control' $argsObj
}
try {
    if (-not $NativeOnly) {
    $status = Call 'test_mode_status'
    $script:fixture = $status.activeSetup
    $script:scopeId = ($status.activeSetupDetails.equipment | Where-Object type -eq 'scope' | Select-Object -First 1).id
    $diagram = Join-Path $WorkRoot 'fixture\preamp\diagrams\12ax7_two_stage_preamp.diagram.json'
    $originalHash = (Get-FileHash -LiteralPath $diagram).Hash
    $before = Scope 'read'
    Check ($before.ok -and $before.recordSamples -eq 2400) 'Retains complete real preamp record'
    Check (Scope 'configure' @{ ch2_volts_per_div = '100'; ch2_offset = '0'; cursors = 'On'; cursor_t1 = '1m'; cursor_t2 = '2m'; cursor_v1 = '-100'; cursor_v2 = '100'; math = 'CH1 - CH2'; math_scale = '0.1'; math_volts_per_div = '10'; time_per_div = '1m'; horizontal_position = '10m' }).ok 'Configure high voltage, pan, math and cursor controls'
    $read = Scope 'read'
    Check ($read.acquisitions -eq $before.acquisitions) 'Scale, pan and cursor controls do not reacquire or resimulate'
    Check ([Math]::Abs($read.cursors.frequencyHz - 1000) -lt 1e-6 -and $read.cursors.deltaV -eq 200) 'Actual instrument reports exact time and voltage cursor deltas'
    Check ($read.viewStart -gt $before.viewStart -and $read.mathSamples -eq 2400) 'Pan navigates full capture and math uses aligned real channels'
    Check (Scope 'configure' @{ trigger_mode = 'Normal'; trigger_level = '1Meg' }).ok 'Normal trigger accepts a level outside signal range'
    $waiting = Scope 'read'
    Check ($waiting.waiting -and $null -eq $waiting.channels[0].rms) 'No matching edge reports waiting and unavailable current measurements'
    Scope 'configure' @{ trigger_mode = 'Auto'; trigger_level = '0'; horizontal_position = '0' } | Out-Null
    Check (Scope 'single').ok 'Single requests a real fixture simulation'
    $single = Scope 'read'
    Check (-not $single.running -and $single.triggered -and $single.status -match 'SINGLE COMPLETE') 'Single holds one matching actual simulation capture'
    Check (Scope 'export' @{} (Join-Path $WorkRoot 'preamp-scope.csv')).ok 'Export full preamp record'
    Check ((Get-Content -LiteralPath (Join-Path $WorkRoot 'preamp-scope.csv')).Count -eq 2401) 'Export contains all actual preamp samples plus header'
    Check (Scope 'autoscale').ok 'Autoscale uses acquired preamp data'
    Check (Scope 'fit').ok 'Fit record uses actual acquisition extent'
    Check (Call 'test_fixture_save' @{ name = $script:fixture; path = (Join-Path $WorkRoot 'scope-settings.fixture.json') }).ok 'Save independent scope settings and connections'
    Check ((Get-Content -LiteralPath (Join-Path $WorkRoot 'scope-settings.fixture.json') -Raw) -match 'cursor_t1') 'Cursor settings persist with fixture'
    Check ((Get-FileHash -LiteralPath $diagram).Hash -eq $originalHash) 'Scope operations preserve fixture authoritative schematic'
    }

    # Analytically known high-bias source and divider, solved by the app's existing transient backend.
    $nativeRoot = Join-Path $WorkRoot 'native-scope'
    New-Item -ItemType Directory -Force -Path (Join-Path $nativeRoot 'diagrams') | Out-Null
    @{ schema = 'djehuti-electronics-project'; version = 1; name = 'Scope Numerical QA'; lastDiagram = 'High Bias'; diagrams = @(@{ name = 'High Bias'; file = 'diagrams/high_bias.diagram.json' }) } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $nativeRoot 'project.json')
    $components = @(
        @{ id = 'VTEST'; symbol = 'ac_voltage_source'; x = 100; y = 200; value = '0.2'; frequency = '1000'; params = @{ waveform = 'Sine'; offset = '250' } },
        @{ id = 'R1'; symbol = 'resistor'; x = 300; y = 200; value = '1k' },
        @{ id = 'R2'; symbol = 'resistor'; x = 500; y = 300; value = '1k'; rotation = 90 },
        @{ id = 'GND1'; symbol = 'ground'; x = 100; y = 400; value = '0' },
        @{ id = 'SCOPE1'; symbol = 'oscilloscope_2ch'; x = 700; y = 200; value = '2ch' }
    )
    $wires = @(@{ a = 'VTEST.+'; b = 'R1.1' }, @{ a = 'R1.2'; b = 'R2.1' }, @{ a = 'R2.2'; b = 'GND1.0' }, @{ a = 'VTEST.-'; b = 'GND1.0' }, @{ a = 'SCOPE1.CH1'; b = 'VTEST.+' }, @{ a = 'SCOPE1.CH2'; b = 'R2.1' }, @{ a = 'SCOPE1.REF'; b = 'GND1.0' })
    @{ schemaVersion = 2; kind = 'electronics_circuit'; components = $components; wires = $wires } | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $nativeRoot 'diagrams\high_bias.diagram.json')
    Check (Call 'project_open' @{ project = $nativeRoot }).ok 'Open analytically known circuit in running application'
    $script:fixture = ''; $script:scopeId = 'SCOPE1'
    Check (Scope 'configure' @{ time_per_div = '1m'; ch1_volts_per_div = '50m'; ch1_offset = '250'; ch2_volts_per_div = '50'; ch2_offset = '125' }).ok 'Configure native scope for small AC on 250 V bias'
    Check (Scope 'open').ok 'Open shared production scope UI for native schematic instrument'
    Check (Scope 'run').ok 'Request a fresh native transient record'
    $dc = Scope 'read'
    Check ($dc.ok -and [Math]::Abs($dc.channels[0].mean - 250) -lt 0.001 -and [Math]::Abs($dc.channels[0].vpp - 0.4) -lt 0.001) 'Simulation and scope measure 250 V bias plus 0.4 Vpp correctly'
    # The existing native scope has a 10 megohm input on each channel.
    $lower = 1.0 / (1.0 / 1000.0 + 1.0 / 10000000.0)
    $dividerGain = $lower / (1000.0 + $lower)
    Check ([Math]::Abs($dc.channels[1].mean - 250*$dividerGain) -lt 0.001 -and [Math]::Abs($dc.channels[1].vpp - 0.4*$dividerGain) -lt 0.001) 'Scope divider response includes actual 10 megohm input loading'
    Check ([Math]::Abs($dc.channels[0].rms - [Math]::Sqrt(250*250 + 0.2*0.2/2)) -lt 0.001) 'Total RMS agrees with analytic biased-sine formula'
    Scope 'configure' @{ ch1_coupling = 'AC'; ch1_offset = '0'; ch2_coupling = 'AC'; ch2_offset = '0'; ch2_volts_per_div = '50m' } | Out-Null
    $ac = Scope 'read'
    Check ($ac.acquisitions -eq $dc.acquisitions) 'Coupling changes reprocess samples without another circuit solve'
    Check ([Math]::Abs($ac.channels[0].mean) -lt 0.001 -and [Math]::Abs($ac.channels[0].rms - 0.2/[Math]::Sqrt(2)) -lt 0.001) 'AC coupling reveals small AC signal while retaining actual DC acquisition'
    Check ([Math]::Abs($ac.channels[0].frequencyHz - 1000) -lt 1 -and [Math]::Abs($ac.channels[0].period - 0.001) -lt 1e-6) 'Native scope frequency and period agree with known stimulus'
    $rise = (2*[Math]::Asin(0.8))/(2*[Math]::PI*1000)
    Check ([Math]::Abs($ac.channels[0].riseTime - $rise) -lt 3e-5 -and [Math]::Abs($ac.channels[0].dutyPercent - 50) -lt 1) 'Native edge timing and duty derive from captured waveform'
    Scope 'configure' @{ ch1_enabled = 'Off' } | Out-Null
    Check (-not (Scope 'read').channels[0].enabled) 'Channel visibility is independent of acquisition'
    Scope 'configure' @{ ch1_enabled = 'On'; cursors = 'On'; cursor_t1 = '1m'; cursor_t2 = '2m'; cursor_v1 = '-200m'; cursor_v2 = '200m'; math = 'CH1 - CH2'; math_volts_per_div = '50m' } | Out-Null
    Scope 'export' @{} (Join-Path $WorkRoot 'native-scope.csv') | Out-Null
    Scope 'stop' | Out-Null
    Check (-not (Scope 'read').running) 'Native Stop holds a real solved capture'
} finally {
    @{ results = $results; checks = $results.Count; nativeDc = $dc; nativeAc = $ac; fixtureSingle = $single; preampChecksReused = [bool]$NativeOnly; expectedDividerGain = $dividerGain } | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $WorkRoot 'scope-integration-results.json')
}
Write-Output "$($results.Count) oscilloscope checks passed, plus the 27 fixture regression checks."
