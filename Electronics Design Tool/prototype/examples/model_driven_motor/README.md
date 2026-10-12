# Model-Driven Motor Controller Example

This folder contains saved model assets used by `ModelDrivenSimulationTests`.

- `nominal/` is the editable baseline machine.
- `low_gain/` changes the saved FRust controller gain.
- `high_inertia/` changes the saved motor inertia.

Each variant is a normal saved model set:

- `saved_motor_controller.machine.json`
- `motor_drive.diagram.json`
- `SavedMotorController.frcomp.json`
- `SavedMotorController.frnode.json`

The verification test loads the machine JSON, compiles the referenced schematic and FRust/node controller through `AssemblyCompiler`, and advances the transient solver. No factory machine is substituted.
