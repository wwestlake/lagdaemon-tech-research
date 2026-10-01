# Xyce Backend Vendor Notes

Xyce is the first reference SPICE-class simulation backend for the electronics design tool research prototype.

Project stance:

- Xyce is GPLv3 and acceptable for this research project.
- The prototype uses Xyce as an external process backend first.
- We do not link Xyce into the JUCE app yet.
- The app owns circuit JSON, circuit IR, instruments, agent workflows, datasets, and Frust integration.
- Xyce owns reference circuit solving.

## Expected Windows Install

The official Windows installer currently installs Xyce under a path like:

```text
C:\Program Files\Xyce 7.10 NORAD\bin\Xyce.exe
```

The setup script in `tools/xyce/setup_xyce.ps1` checks common install paths and writes a local backend config.

## Backend Boundary

The app should call Xyce through a backend interface:

```text
Circuit JSON
-> Circuit IR
-> Xyce netlist
-> Xyce external process
-> .prn / CSV / stdout parse
-> normalized dataset JSON
-> instruments / math console / agent
```

Do not build UI code directly around Xyce command-line quirks. Keep an adapter layer.

## First Smoke Test

The initial test is a resistor divider transient/DC-style check:

```text
V1 in 0 10
R1 in out 10000
R2 out 0 10000
```

Expected output node voltage is approximately 5 V.

Run:

```powershell
.\tools\xyce\run_xyce_smoke.ps1
```

## Productization Note

If this becomes a product later, review GPLv3 obligations and distribution model. Keeping Xyce behind an external process boundary is cleaner than embedding or linking.
