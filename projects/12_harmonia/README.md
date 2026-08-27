# Harmonia

*An open-world multiplayer music game, educator, and experimentation platform.*

Built with **JUCE 7** · **OpenGL 3.3** · **C++20** · **Windows**

---

## What Is Harmonia?

Harmonia is a shared musical universe. Players exist together in a continuous 3D space. Music is not played *in* the game — the 3D geometry *is* the music. Every note any player plays is heard by everyone nearby, rendered as light and geometry in real-time.

### Musical Worlds (Regions)

| World | Concept | Status |
|-------|---------|--------|
| 🌐 Living Grid | 3D cellular automaton — voxels that evolve into music | Phase 1 |
| 🔺 Interval Forge | Intervals as 3D geometry, consonance as light | Planned |
| 🏗️ Chord Architect | Build chords as luminous columns | Planned |
| 🥁 Rhythm Engine | Polyrhythm as 3D grid patterns | Planned |
| 🌿 Scale Garden | Scales as navigable landscapes | Planned |
| 🌊 Harmonic Ocean | Chord progressions as flowing currents | Planned |
| 👂 Ear Training Arena | Gamified ear training | Planned |
| 🧬 R-D Studio | Gray-Scott reaction-diffusion music | Planned |

---

## Building

### Prerequisites
- Visual Studio 2022
- CMake 3.22+
- JUCE 7 at `D:\JUCE` (or set `JUCE_ROOT` in CMake)

### Configure + Build

```powershell
cd projects\12_harmonia
cmake -S . -B Builds -G "Visual Studio 17 2022" -A x64
MSBuild Builds\Harmonia.sln /t:Harmonia /p:Configuration=Debug
MSBuild Builds\Harmonia.sln /t:HarmoniaServer /p:Configuration=Debug
```

### Run

```powershell
# Start the server (listens on port 4440)
.\Builds\Debug\HarmoniaServer.exe

# Start the client (in a separate window)
.\Builds\Debug\Harmonia.exe
```

---

## Networking

Harmonia uses **HARP** — our own binary TCP protocol.

- **Port**: `4440` (A440 — the concert pitch standard)
- **Transport**: TCP/IP — no third-party networking libraries
- **Server**: `HarmoniaServer.exe` — lightweight Windows console app you host yourself
- **Protocol**: Defined in `Source/Shared/Network/Protocol.h`

See [HOSTING.md](HOSTING.md) for port forwarding and internet play setup.

---

## Project Structure

```
Source/
├── Shared/          # Compiled into both client and server
│   ├── Network/     # HARP protocol + serializer
│   ├── Music/       # Music theory, note events, circle of fifths
│   └── World/       # VoxelGrid, WorldState
├── Server/          # HarmoniaServer.exe
└── Client/          # Harmonia.exe
    ├── App/         # Entry point, top-level component
    ├── Network/     # TCP client, HARP framer, message dispatch
    ├── World/       # Open world, regions, player controller
    ├── Engine/
    │   ├── Rendering/ # OpenGL: voxels, particles, avatars, bloom
    │   └── Audio/     # JUCE synth, MIDI out, spatial audio
    └── Shell/       # Splash, server browser, journal
```

---

## HARP Protocol Quick Reference

```
Port: 4440 TCP
Handshake: [4b "HARP"][1b version][2b MsgType][4b len][payload]
Messages:  [1b version][2b MsgType][4b len][payload]
Endian: little-endian
```

Key message types: `Hello`, `Welcome`, `NoteOn/Off`, `PlayerPosition`, `VoxelDelta`, `VoxelFullSync`, `CAParamChange`, `ChordStackChange`, `ChatMessage`

Full spec: `Source/Shared/Network/Protocol.h`

---

## License

Research project — LagDaemon
