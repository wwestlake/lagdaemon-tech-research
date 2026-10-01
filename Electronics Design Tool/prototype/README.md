# Djehuti Electronics Lab Prototype

This is a standalone research shell for the electronics design tool.

It is intentionally outside the FrustLang repository:

```text
D:\000 Tech Research\Electronics Design Tool\prototype
```

The shell reuses the research dock manager from FrustLang, but this project owns its own app target and electronics-specific panels.

## Panels

- Component Library
- Schematic
- Simulation
- Instruments
- Frust Math Console
- BYOK Agent
- Properties
- Spec Ingestion

## Build

Configure with CMake, pointing at JUCE if needed:

```powershell
cmake -S "D:\000 Tech Research\Electronics Design Tool\prototype" -B "D:\000 Tech Research\Electronics Design Tool\prototype\build" -DJUCE_PATH=D:\JUCE
cmake --build "D:\000 Tech Research\Electronics Design Tool\prototype\build" --config Debug
```

The app output is:

```text
D:\000 Tech Research\Electronics Design Tool\prototype\bin\Debug\djehuti_electronics_lab.exe
```

## Notes

This first shell is not the electronics engine. It is the platform workspace where we can start adding:

- circuit JSON model
- component ingestion
- BYOK agent integration
- LiteSemRAG cards
- Frust math console binding
- SPICE/offline simulation
- compiled Frust realtime preview
