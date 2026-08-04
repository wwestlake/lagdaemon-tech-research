# 03 - Rust-Like Module System

## Module Organization & File Mapping

```
src/
├── main.ldfn                 # Root entry module
├── math/
│   ├── mod.ldfn              # Submodule root for `math`
│   ├── dsp.ldfn              # Submodule `math::dsp`
│   └── matrix.ldfn           # Submodule `math::matrix`
└── audio/
    ├── synthesizer.ldfn      # Submodule `audio::synthesizer`
    └── filter.ldfn           # Submodule `audio::filter`
```

---

## Syntax & Visibility

```ldfn
mod math {
    pub mod dsp {
        pub fn gain(sample: f32, factor: f32) -> f32 =
            sample * factor
    }
}

use math::dsp::gain;

pub fn main() -> f32 =
    gain(0.75, 2.0)
```

---

## Dynamic Module Hot-Reloading in LLVM OrcJIT

Each submodule corresponds to a dynamic **`JITDylib`** inside LLVM OrcJIT:

```
[ JUCE / Host Application ]
           │
           ▼
[ LagDaemon OrcJIT Host Driver ]
     ├── JITDylib: "main"
     ├── JITDylib: "math::dsp"    ───(Re-JIT / Hot-Swapped on File Change)
     └── JITDylib: "audio"
```
