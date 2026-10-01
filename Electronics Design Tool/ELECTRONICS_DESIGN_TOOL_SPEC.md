# Electronics Design Tool Research Specification

Date: 2026-10-01

## Purpose

This research project explores a standalone electronics design tool with an agentic engineering workflow. The tool is not assumed to be part of any existing suite. It should be designed as a serious electronics lab: schematic capture, component intelligence, simulation, instruments, math solving, and agent-assisted design all sharing one coherent circuit model.

The central idea is:

```text
Circuit model -> analysis/simulation -> datasets -> instruments/math/agent feedback -> revised circuit
```

The tool should support traditional electronics workflows, but should also go beyond conventional schematic capture by making circuits queryable, analyzable, scriptable, and compilable.

## Core Principles

- The circuit model is authoritative, not the screen drawing.
- Visual diagrams are editable views over structured data.
- Components carry provenance, extracted specs, confidence, and verification state.
- Simulation results are datasets that can be replayed, inspected, plotted, and processed.
- Realtime simulation is desirable, but not required for all circuits.
- Offline solver simulation and compiled realtime simulation are separate modes.
- The agent must understand the model through LiteSemRAG, process cards, and tool APIs.
- Embedded Frust should serve as both an extension language and a math/workbench language.

## Major Subsystems

### 1. Schematic System

The schematic system models:

- projects
- sheets
- components
- pins
- wires
- nets
- net labels
- buses
- power symbols
- probes
- instruments
- constraints
- annotations

The schematic editor should eventually support normal electronics CAD gestures: place component, wire pins, label nets, edit properties, group sheets, and run electrical rule checks.

The model should be JSON-serializable. The diagram layout is important, but it is secondary to the electrical model.

### 2. Component Library and Spec Ingestion

The component library should not require every part to be preloaded. If the user places a part that does not exist locally, the BYOK agent should be able to acquire it.

Example:

```text
User places: Fairchild 741 op amp
```

Expected workflow:

1. Search local component database.
2. If missing, query approved providers or manufacturer sources.
3. Fetch the datasheet.
4. Extract the relevant data.
5. Generate a provisional component record.
6. Select or create a schematic symbol.
7. Store extracted claims with provenance.
8. Ask for user approval when confidence is low or ambiguity exists.
9. Add the verified/provisional component to the database.

The component record should include:

- manufacturer
- manufacturer part number
- aliases/family names
- category
- symbol reference
- pin definitions
- package variants
- footprint candidates
- SPICE/model references when available
- datasheet sources
- extracted spec claims
- confidence and verification status

Extracted values should be stored as claims, not unquestioned truth. Every claim should carry:

- value
- unit
- context
- source document
- page/table/section
- raw evidence snippet when possible
- extraction confidence
- verification status

### 3. BYOK Agent and LiteSemRAG

The electronics tool should include its own BYOK agent. The agent uses LiteSemRAG to understand:

- electronics concepts
- project process cards
- component ingestion rules
- simulation setup rules
- extraction policies
- verified component records
- user corrections and lessons learned
- tool capabilities

The agent should be able to:

- place components
- inspect circuits
- explain circuit behavior
- run checks
- fetch and ingest specs
- create or modify components
- propose simulation setups
- run simulations with approval when appropriate
- analyze simulation datasets
- write reports
- generate Frust scripts for math/analysis tasks

The agent must know what mode it is in and what tools are available. If a capability exists only in a different mode or requires approval, it should say that plainly.

### 4. Simulation System

Simulation should be analysis-first. The tool does not need to simulate everything in realtime while the user edits. The normal model is:

```text
Circuit + stimulus + analysis request -> computed dataset -> instrument visualization/playback
```

Initial simulation types:

- DC operating point
- DC sweep
- transient analysis
- AC small-signal analysis
- digital timing simulation

Later simulation types:

- noise analysis
- Monte Carlo/tolerance analysis
- mixed-signal simulation
- thermal approximations
- electromagnetic/layout-aware analysis

Simulation outputs should be stored as datasets:

- time-domain traces
- frequency-domain traces
- operating-point values
- branch currents
- node voltages
- digital state traces
- measurement results
- metadata tying the run to a circuit revision

### 5. Instruments

Instruments are views over simulation datasets, not necessarily realtime physical widgets.

Expected instruments:

- oscilloscope
- spectrum analyzer
- logic analyzer
- multimeter
- signal generator
- power supply
- Bode plotter
- arbitrary waveform viewer
- measurement panel

An oscilloscope, for example, should be able to display a transient dataset after computation and play it back at realtime speed for visualization.

### 6. Offline Solver Backend

The solver backend is the accurate/reference path. The first reference backend for research is Xyce, used as an external process.

Xyce is GPLv3. That is acceptable for this research phase. The initial integration should keep Xyce behind an external-process boundary rather than linking it into the app. This keeps the app architecture clean and lets productization choices be revisited later.

Responsibilities:

- generate netlists from the circuit model
- run Xyce DC, AC, transient, and sweep analyses
- parse solver outputs
- normalize results into the tool dataset model
- report convergence and model errors

ngspice may be added later as a secondary backend if embedded-library convenience or compatibility requires it, but the first backend is Xyce.

This backend is authoritative for difficult analog circuits, nonlinear devices, transistor-level simulation, and verification.

### 7. Compiled Frust Simulation Backend

The tool should research compiling suitable circuits into Frust for realtime or faster-than-realtime execution.

Pipeline:

```text
Schematic / circuit model
-> circuit IR
-> partition into compileable regions
-> lower to Frust
-> compile native
-> run simulation kernel
-> stream traces to instruments
```

This is not required for every circuit. It is a capability for circuits or subcircuits that can be represented as:

- explicit update systems
- digital/event systems
- DSP/control systems
- state-space systems
- simplified behavioral models
- specialized per-timestep solvers

Guiding phrase:

```text
SPICE validates. Frust accelerates.
```

Example targets for compiled simulation:

- RC filters
- op-amp circuits using behavioral models
- logic circuits
- control loops
- audio-rate circuits
- signal-processing blocks
- educational visual simulations

The compiled backend may eventually support hybrid workflows where an offline solver creates or validates a simplified model, then Frust runs that model interactively.

### 8. Math Console and Solver Workbench

The tool should include an embedded Frust console/notebook similar in spirit to MATLAB, SciLab, or engineering notebooks.

The console should be connected to:

- circuits
- simulation runs
- datasets
- plots
- component values
- optimization tasks
- reports

Example uses:

```frust
let run = simulate_transient("MainAmp", 0.0, 0.05, 0.00001);
let out = trace(run, "V(out)");
let spectrum = fft(out);
plot(spectrum);
```

Possible capabilities:

- vectors and matrices
- complex numbers
- FFT
- filtering
- curve fitting
- statistics
- numerical solving
- optimization
- units
- plotting
- importing measured data
- comparing measured and simulated data
- generating stimuli
- computing component values

The math layer should make the electronics tool feel like a lab, not just a schematic editor.

### 9. Frust Plugins

Frust plugins may provide:

- PDF/spec extraction helpers
- component import providers
- simulation adapters
- SPICE netlist exporters
- dataset processors
- math functions
- custom instruments
- ERC rules
- symbol/footprint generators
- compiled simulation kernels

The host should expose services to plugins:

- logging
- component database access
- circuit model access
- dataset access
- plotting/instrument registration
- LiteSemRAG card registration
- user approval workflows

Plugins should be able to announce themselves and provide cards describing how the agent should use them.

## Initial Data Model Sketch

```json
{
  "project": {
    "id": "project_001",
    "name": "Example Circuit",
    "sheets": [],
    "libraries": [],
    "simulationRuns": [],
    "metadata": {}
  }
}
```

Component:

```json
{
  "id": "comp_u1",
  "refdes": "U1",
  "manufacturer": "Fairchild",
  "mpn": "uA741",
  "category": "op_amp",
  "symbol": "op_amp_single",
  "pins": [
    { "number": "2", "name": "IN-", "type": "analog_input" },
    { "number": "3", "name": "IN+", "type": "analog_input" },
    { "number": "6", "name": "OUT", "type": "analog_output" },
    { "number": "7", "name": "V+", "type": "power_input" },
    { "number": "4", "name": "V-", "type": "power_input" }
  ],
  "packages": [],
  "specClaims": [],
  "sources": [],
  "verification": {
    "status": "provisional",
    "verifiedBy": "",
    "verifiedAt": ""
  }
}
```

Simulation run:

```json
{
  "id": "run_001",
  "type": "transient",
  "circuitRevision": "hash",
  "startTime": 0.0,
  "stopTime": 0.05,
  "timeStep": 0.00001,
  "probes": ["V(in)", "V(out)"],
  "datasets": [
    {
      "name": "V(out)",
      "domain": "time",
      "unit": "V",
      "samples": []
    }
  ]
}
```

## Research Questions

1. What circuit IR can lower both to SPICE netlists and Frust simulation kernels?
2. How much component data can be reliably extracted from datasheets without human correction?
3. What confidence model should determine when the agent must ask for approval?
4. What subset of analog circuits can be compiled to realtime Frust kernels?
5. How should mixed analog/digital simulations be partitioned?
6. What math pods does Frust need to feel like a real engineering console?
7. How should instruments subscribe to offline datasets and realtime streams through one interface?
8. How should LiteSemRAG cards represent components, specs, simulation methods, and user corrections?

## First Prototype Slice

The first useful prototype should be modest but real:

1. Create a standalone research data model for circuits/components/nets.
2. Define a component JSON schema with provenance and extracted claims.
3. Implement a small library of generic components:
   - resistor
   - capacitor
   - voltage source
   - ground
   - 741-style op amp
4. Generate a simple SPICE-like netlist from the model.
5. Store a fake or imported transient dataset.
6. Display it in an oscilloscope-style viewer.
7. Add a Frust math console concept document or prototype.
8. Define the first component-ingestion agent workflow.

The first prototype does not need full realtime simulation. It should prove the model shape, ingestion workflow, and dataset/instrument boundary.

## Long-Term Vision

The mature version is an electronics engineering lab where the user can design, simulate, analyze, optimize, document, and extend circuits with the help of an agent.

It should support the normal expectations of electronics design, while adding:

- automatic component acquisition from datasheets
- agent-readable circuit knowledge
- solver-backed analysis
- compiled Frust realtime preview
- integrated math/notebook workflows
- extensible plugins
- provenance-aware engineering records

The goal is not merely to draw circuits. The goal is to help an engineer reason about them.
