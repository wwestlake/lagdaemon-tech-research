# DEWB CAD simulation lifecycle completion

Date: October 19, 2026. Branch: `feat/dewb-cad-lifecycle`.

## Scope and findings

This change implements the lifecycle assignment from `DEWB_FEATURE_GAP_ASSESSMENT.md`, using the existing `DEWB_DCAD_MOTION_INTERFACE_V1.md` contract. No CAD source, coordinate/rate evaluation, electrical solver algorithms, VST, FRust, or PCB implementation was changed.

The original client acknowledged queued steps and non-quiescent pauses, synchronized the old final time before a fresh worker restarted at zero, returned Reset success before CAD's `StateApplied`, accepted acknowledgements by sequence alone, cleared request history on reconnect, and parsed step text with throwing `stoll`. Synchronous pipe writes could also stall lifecycle handling/shutdown.

## Implementation

- `Source/SystemSimulationSession.h/.cpp`: lifecycle calls serialize; Start waits for worker initialization; Pause waits for the worker to park; initial-state Step creates a paused worker and waits for the requested advancement; Stop joins the worker and retains its final snapshot. Reset publishes an initialized numerical state rather than a fabricated zero-valued signal map. A snapshot timeline revision identifies reinitialization without changing the simulation clock model or stepper algorithms. Worker initialization/advancement failures publish fault state. Slow-rate pacing now waits on the existing condition variable so Pause/Stop interrupt it immediately rather than waiting for the wall-clock pacing interval.
- `Source/DcadMotionClient.h/.cpp`: commands validate binding, capability/readiness, transition, duration, and timeline. Full synchronization is a pending transaction, acknowledged only by matching simulation/profile/timeline/sequence. Fresh Start prepares stopped t=0 state, waits for CAD, then starts and publishes running state. Reset waits for CAD before its final result. Acknowledgement timeout, failed CAD solve, revalidation, disconnect, and session replacement produce explicit terminal diagnostics. Late acknowledgements cannot complete canceled transactions. Completed/failed request records survive reconnect; reusing an ID for a different operation is rejected. Pipe reads/writes use bounded, cancelable overlapped operations on the same named-pipe transport.
- `Source/ElectronicsWorkbench.cpp`: registration of the lifecycle tool and existing connect/disconnect/status operations needed to establish lifecycle readiness, shared safe duration parser, returned request ID, and explicit `accepted` outcome handling.
- `knowledge/cards/electronics_tool_cards.jsonl`: lifecycle usage describes pending outcomes and stable retry IDs.
- `tools/cad_motion_tests/DcadMotionTests.cpp`: production-client pipe tests verify lifecycle transactions and state publication, rather than only directly calling session methods.
- `CMakeLists.txt`: `DcadMotionTests` now links only its actual default-motor/session/pipe boundary, excluding unused assembly/editor/compiler sources and libraries. `setupAssembly` (with its assembly-compiler call preserved and lifecycle revision initialization added) moved to `Source/SystemSimulationSessionAssembly.cpp`, retained in every existing assembly-capable target and omitted only from this lifecycle target. Debug function-level linking/unused-code elimination and FASTLINK keep symbol/link overhead bounded. The original target exhausted memory, including after the first link-setting adjustment. These tests do not validate assembly/FRust compilation; other existing targets retain that responsibility. Application link settings are unchanged.

## Supported transitions

| Request | Permitted state | Result |
|---|---|---|
| Start | Stopped | Running; a previously run simulation first resets to t=0 on a new synchronized timeline |
| Pause | Running | Paused after the current advancement finishes; state/time then remain fixed |
| Resume | Paused | Running on the same timeline |
| Stop | Running or paused | Stopped with final numerical state/time retained |
| Reset | Any, including faulted | Initial numerical state at t=0, stopped, new synchronized timeline; initialization failure is explicit |
| Step | Paused, or initial stopped | Exactly the positive requested picosecond interval, then paused on the same timeline |

Invalid transitions, malformed/overflowed/nonpositive step durations, unknown commands, stale bindings/timelines, and an unsynchronized/busy link are rejected with correlated results. Ordinary states advance sequence without changing timeline. Reinitialization uses `SynchronizeState` and a new timeline. Reconnect revalidates and fully synchronizes the actual current simulation state.

## Results and acknowledgements

`accepted` means a lifecycle transaction is waiting for CAD application; it is not completion. `StateApplied` confirms a synchronized CAD pose, while `LifecycleResult` reports the command outcome. Ordinary transitions publish a complete `StateFrame` before `applied`; Reset and fresh Start require the exact `StateApplied` before `applied`. Fresh Start remains stopped until that acknowledgement.

Request IDs are retained across transport reconnects for the attached simulation session. Retrying an identical ID returns its stored result, including a Reset result from a newer timeline, without repeating the operation. A disconnect during deferred Start cancels that Start; reconnect never implicitly resumes it. Failure messages distinguish a prepared/reset DEWB state from missing CAD confirmation. An acknowledgement timeout requires reconnect/rebinding before normal streaming resumes.

Worker completion waits are bounded at three seconds; synchronization waits at five seconds; individual pipe operations at two seconds. A numerical worker that fails to complete within its wait is reported explicitly as unconfirmed/faulted, never as successfully completed; callers must inspect current state before issuing another operation. Transport disconnect itself does not stop the simulation.

## Validation

- Debug `DcadMotionTests` build: passed after isolating the test link dependencies. Log: `build/cad_lifecycle_build.log`.
- Executed `bin/Debug/dcad_motion_tests.exe`: **241/241 checks passed in 6439.64 ms**, exit 0. Log: `build/cad_lifecycle_tests.log`. Its final `NonExistentExpectedResponse` timeout is a deliberately asserted 250 ms negative harness check, not a failing lifecycle test.
- Debug `DjehutiElectronicsLab` build: passed, exit 0. Log: `build/cad_lifecycle_app_build.log`. MSBuild reported the existing shared-intermediate-directory warning MSB8028; no clean/restructure was performed. The build process used temporary `_LINK_=/DEBUG:FASTLINK /OPT:REF /INCREMENTAL:NO` to fit available memory; no application CMake configuration or solver/compiler optimization setting was changed.
- All builds use `/m:1`. No broad regression suite was run. Initial full-dependency link attempts were interrupted because of memory pressure; narrowing the test boundary resolved that problem. One intermediate narrowed-target compile lacked graphics header types and one link lacked the assembly adapter definition; both were corrected before the successful build/run.

The pipe checks cover Start → Pause → Step → Resume → Stop → Reset; matching state-before-result sequence/time; stable paused/stopped numerical state; exact initial-state stepping; fresh-start synchronization; invalid transitions/durations; stale binding/timeline; wrong acknowledgements; retry deduplication; reconnect/full synchronization; acknowledgment timeout/late acknowledgement; and disconnect during deferred Start followed by retry.

### Actual two-application smoke: passed

Both Debug executables launched and answered through their production local APIs. DEWB loaded and compiled `examples/model_driven_motor/nominal/saved_motor_controller.machine.json`; CAD opened a temporary test copy of the existing `gear_pair_motor_speed_initial.djc` fixture. The saved machine advertises `drive.speed`, while the original fixture expects `motor.speed`: the first connection correctly remained unbound and CAD reported that exact unknown-signal problem. Only the temporary copy under `build/cad_lifecycle_gear_pair.djc` was adjusted to `drive.speed`; original fixtures, application projects, and CAD source were not edited.

CAD then issued the following commands through `cad_motion_lifecycle`. Its own `lifecycleControl.lastResult` confirmed each result; DEWB and CAD state/time agreed, and CAD reported `mechanism.valid=true` throughout.

| Command | Observed confirmed time | Timeline | Evidence |
|---|---:|---:|---|
| Start | 0.030 s | 1 | CAD recorded Start applied at 0.000 s; running state had advanced by the observation |
| Pause | 0.060 s | 1 | Time remained fixed during an additional paused observation |
| Step, 7 ms | 0.067 s | 1 | Exact 7,000,000,000 ps advancement, paused |
| Resume | 0.067 s | 1 | Running on same timeline |
| Stop | 0.117 s | 1 | Final time remained fixed |
| Reset | 0.000 s | 2 | New timeline, synchronized stopped initial state |
| Disconnect/reconnect | 0.000 s | 2 | Rebound/full synchronization retained time and timeline |

An additional real-pair Pause from stopped returned `Pause rejected (invalidTransition): Cannot pause while stopped.` A subsequent run stopped at 0.055 s on timeline 2; fresh Start completed on timeline 3, followed by Stop and Reset to timeline 4. Thus fresh-start synchronization was exercised with actual CAD acknowledgements as well as the simulated peer. The temporary fixture's file hash was unchanged by execution. Both owned test instances were disconnected and closed normally after the checks.

Logs retained under `build/`: `cad_lifecycle_pair_smoke.log`, `cad_lifecycle_pair_extra.log`, and structured `cad_lifecycle_pair_smoke.json`. The first incompatible-profile diagnostic is retained in `cad_lifecycle_pair_blocker.json`. These are functional API/protocol checks in the actual applications, not a visual rendering acceptance claim. No required CAD lifecycle behavior was missing and no CAD source change was necessary.

The initial CAD executable lock and memory contention were resolved without changing or terminating the other CAD build. Solver-timeout stress tests, broad numerical regressions, and coordinate/rate accuracy work were not performed. The separately assessed rate-coordinate limitations remain outside this assignment.
