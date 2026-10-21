param(
    [string]$Repository = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory = $true)][string]$WorkRoot
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
$sourceProject = Join-Path $Repository 'examples\tube_amplifiers'
$sourceDiagram = Join-Path $sourceProject 'diagrams\12ax7_two_stage_preamp.diagram.json'
$originalHash = (Get-FileHash -LiteralPath $sourceDiagram).Hash
$projectCopy = Join-Path $WorkRoot 'preamp'
New-Item -ItemType Directory -Force -Path $projectCopy | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceProject 'project.json') -Destination $projectCopy -Force
Copy-Item -LiteralPath (Join-Path $sourceProject 'diagrams') -Destination $projectCopy -Recurse -Force
$copyDiagram = Join-Path $projectCopy 'diagrams\12ax7_two_stage_preamp.diagram.json'
$copyHash = (Get-FileHash -LiteralPath $copyDiagram).Hash

# A short real WAV keeps this integration check bounded. No bundled-file fallback.
$wav = Join-Path $WorkRoot 'stimulus.wav'
$writer = [System.IO.BinaryWriter]::new([System.IO.File]::Create($wav))
try {
    $count = 2400
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF')); $writer.Write([int](36 + 2 * $count))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $writer.Write([int]16)
    $writer.Write([short]1); $writer.Write([short]1); $writer.Write([int]48000); $writer.Write([int]96000)
    $writer.Write([short]2); $writer.Write([short]16); $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([int](2 * $count))
    for ($i = 0; $i -lt $count; ++$i) { $writer.Write([short](30000 * [Math]::Sin(2 * [Math]::PI * 1000 * $i / 48000))) }
} finally { $writer.Dispose() }

if (Get-Process -Name djehuti_electronics_lab -ErrorAction SilentlyContinue) { throw 'Close the running app before this isolated integration check.' }
$started = Get-Date
$app = Start-Process -FilePath (Join-Path $Repository 'bin\Debug\djehuti_electronics_lab.exe') -WorkingDirectory $Repository -WindowStyle Hidden -PassThru
$apiPath = Join-Path $env:APPDATA 'DjehutiElectronicsLab\agent-api.json'
$deadline = (Get-Date).AddSeconds(35)
do {
    Start-Sleep -Milliseconds 500
    if ($app.HasExited) { throw 'Application exited during startup.' }
    $ready = (Test-Path -LiteralPath $apiPath) -and (Get-Item -LiteralPath $apiPath).LastWriteTime -ge $started.AddSeconds(-1)
} until ($ready -or (Get-Date) -gt $deadline)
if (-not $ready) { throw 'Agent API did not become ready.' }
$api = Get-Content -LiteralPath $apiPath -Raw | ConvertFrom-Json
$headers = @{ Authorization = 'Bearer ' + $api.token }
$script:checks = 0
$script:results = @()
function Check([bool]$condition, [string]$name) {
    ++$script:checks
    $script:results += [pscustomobject]@{ pass = $condition; name = $name }
    if (-not $condition) { throw "Check failed: $name" }
}
function Call([string]$name, [hashtable]$arguments = @{}) {
    $body = @{ name = $name; arguments = $arguments } | ConvertTo-Json -Depth 15
    try {
        $response = Invoke-RestMethod -Uri ($api.baseUrl + '/v1/tools/call') -Method Post -Headers $headers -Body $body -ContentType 'application/json' -TimeoutSec 120
    } catch {
        # Tool validation failures use non-2xx HTTP status and still carry a result.
        if (-not $_.ErrorDetails.Message) { throw }
        $response = $_.ErrorDetails.Message | ConvertFrom-Json
        if (-not $response.result) { throw }
    }
    $r = $response.result
    if ($r -is [string]) { $r = $r | ConvertFrom-Json }
    return $r
}
try {
    Check (Call 'project_open' @{ project = $projectCopy }).ok 'Open temporary copy of actual tube project'
    Check (Call 'diagram_open' @{ name = '12AX7 Two Stage Preamp' }).ok 'Open actual two-stage preamp schematic'
    $sources = Call 'test_fixture_sources'
    Check (($sources.sources | Where-Object refdes -eq 'VIN').eligible -and -not ($sources.sources | Where-Object refdes -eq 'VCC').eligible) 'VIN eligible and VCC identified as DC supply'
    $fixture = 'Integration Repairs QA'
    Check (Call 'test_fixture_create' @{ name = $fixture }).ok 'Create fixture tab in running application'
    $inputId = (Call 'test_fixture_place_equipment' @{ fixture = $fixture; type = 'audio_input'; x = 40; y = 60 }).equipment.id
    $sinkId = (Call 'test_fixture_place_equipment' @{ fixture = $fixture; type = 'audio_output'; x = 780; y = 450 }).equipment.id
    $scopeId = (Call 'test_fixture_place_equipment' @{ fixture = $fixture; type = 'scope'; x = 780; y = 60 }).equipment.id
    Check (Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $inputId; terminalId = 'gnd'; targetPin = 'VIN.-' }).ok 'Wire ground first'
    Check (Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $inputId; terminalId = 'out'; targetPin = 'A1.IN' }).ok 'Wire OUT to actual VIN signal net'
    Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $sinkId; terminalId = 'in'; targetPin = 'R_LOAD.1' } | Out-Null
    Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $scopeId; terminalId = 'in'; targetPin = 'A1.IN' } | Out-Null
    Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $scopeId; terminalId = 'ch2'; targetPin = 'R_LOAD.1' } | Out-Null
    Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $scopeId; terminalId = 'ref'; targetPin = 'VIN.-' } | Out-Null
    Check (Call 'test_fixture_configure_equipment' @{ fixture = $fixture; id = $inputId; audioFilePath = $wav; peakVolts = 0.05; isOverride = $true; overrideRefdes = 'VIN' }).ok 'Apply real WAV and VIN replacement'
    Check (-not (Call 'test_fixture_configure_equipment' @{ fixture = $fixture; id = $inputId; audioFilePath = (Join-Path $WorkRoot 'missing.wav') }).ok) 'Missing WAV rejected without fallback'
    Check (-not (Call 'test_fixture_configure_equipment' @{ fixture = $fixture; id = $inputId; overrideRefdes = 'VCC' }).ok) 'DC supply replacement rejected'
    Check (Call 'test_fixture_instrument' @{ fixture = $fixture; id = $scopeId; action = 'open' }).ok 'Open production scope window in running app'
    $run = Call 'test_setup_run' @{ name = $fixture; audition = $false }
    Check ($run.ok -and $run.inputDescription -match 'VIN' -and $run.samples -eq 2400 -and $run.outputPeakVolts -gt 0) 'Real WAV passes through tube simulation replacing VIN'
    $script:simulation = $run
    $heldWav = $wav + '.held'
    Move-Item -LiteralPath $wav -Destination $heldWav
    try {
        $missingRun = Call 'test_setup_run' @{ name = $fixture; audition = $false }
        Check (-not $missingRun.ok -and $missingRun.error -match 'file not found' -and $missingRun.error -match 'Browse') 'Deleted WAV fails at runtime without a bundled substitution'
    } finally { Move-Item -LiteralPath $heldWav -Destination $wav }
    Check (Call 'test_setup_run' @{ name = $fixture; audition = $false }).ok 'Restored WAV runs successfully'
    $scope = Call 'test_fixture_instrument' @{ fixture = $fixture; id = $scopeId; action = 'read' }
    Check ($scope.ok -and $scope.channels[0].connected -and $scope.channels[1].connected -and $scope.channels[0].vpp -gt 0.09 -and $scope.channels[1].vpp -gt 0) 'Scope reads actual input and output waveforms'
    Check (Call 'test_fixture_instrument' @{ fixture = $fixture; id = $scopeId; action = 'configure'; settings = @{ time_per_div = '1m'; ch2_volts_per_div = '5' } }).ok 'Operate existing scope scale controls'
    $sidecar = Join-Path $WorkRoot 'repair-qa.fixture.json'
    Check (Call 'test_fixture_save' @{ name = $fixture; path = $sidecar }).ok 'Save independent fixture sidecar'
    Check ((Get-Content -LiteralPath $sidecar -Raw) -match 'time_per_div') 'Scope settings persisted in fixture'
    Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $inputId; terminalId = 'out'; targetPin = 'VCC.+' } | Out-Null
    $badRun = Call 'test_setup_run' @{ name = $fixture; audition = $false }
    Check (-not $badRun.ok -and $badRun.error -match 'VCC' -and $badRun.error -match 'VIN' -and $badRun.error -match 'Reconnect') 'Miswired VCC run fails with source-specific correction'
    Check (-not (Call 'test_fixture_instrument' @{ fixture = $fixture; id = $scopeId; action = 'read' }).ok) 'Failed simulation cannot show previous waveform as current'
    $diagnostic = Call 'test_fixture_diagnostics' @{ fixture = $fixture; action = 'read' }
    Check ($diagnostic.message -eq $badRun.error -and $diagnostic.expanded) 'Full live simulation error visible in diagnostics drawer'
    Check (-not (Call 'test_fixture_diagnostics' @{ fixture = $fixture; action = 'hide' }).expanded) 'Hide diagnostics without losing details'
    Check ((Call 'test_fixture_diagnostics' @{ fixture = $fixture; action = 'read' }).message -eq $badRun.error) 'Hidden error remains discoverable'
    Check ((Call 'test_fixture_diagnostics' @{ fixture = $fixture; action = 'clear' }).message -eq '') 'Clear stale diagnostic'
    Call 'test_fixture_connect' @{ fixture = $fixture; equipmentId = $inputId; terminalId = 'out'; targetPin = 'A1.IN' } | Out-Null
    Check (Call 'test_setup_run' @{ name = $fixture; audition = $false }).ok 'Corrected wiring runs successfully again'
    Check ((Call 'test_fixture_diagnostics' @{ fixture = $fixture; action = 'read' }).message -eq '') 'Successful run clears old error'
    Check ((Get-FileHash -LiteralPath $copyDiagram).Hash -eq $copyHash) 'Temporary authoritative schematic file unchanged'
    Check ((Get-FileHash -LiteralPath $sourceDiagram).Hash -eq $originalHash) 'Original repository schematic file unchanged'
} finally {
    @{ checks = $script:checks; results = $script:results; simulation = $script:simulation; processId = $app.Id } | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $WorkRoot 'integration-results.json')
}
Write-Output "$($script:checks) live API checks passed. App PID $($app.Id) is open on the temporary test project."
