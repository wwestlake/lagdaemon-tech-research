# 04 - First-Class Metaprogramming & Type Construction

## Core Concept

Metaprogramming is simply describing to the compiler how I want the program constructed. It is not an arcane template sub-language.

1. **Types are First-Class Values** (`Type`).
2. **Metaprogramming IS Regular Code**: Normal functions that execute at compile time via OrcJIT.
3. **No Esoteric Angle Brackets**: No `<T, typename std::enable_if...>` complexity.

---

## Type Construction Example

```ldfn
// Plain function that constructs a Buffer layout for any element Type and capacity
fn Buffer(ElementType: Type, capacity: Int) -> Type = {
    return layout {
        items: Array(ElementType, capacity),
        count: Int,
        is_full: Bool
    }
}

// Instantiating types is just calling the function:
let AudioBuffer = Buffer(f32, 512)
let MidiBuffer  = Buffer(MidiMessage, 128)
```

---

## Metaprogramming Component Generators (`build_time`)

```ldfn
fn Equalizer(bands: Int) -> Component<AudioProcessor> = build_time {
    let eq = ComponentBuilder("Equalizer")

    for i in 0 .. bands {
        let freq = 100.0 * (2.0 ^ i)
        eq.append_stage(LowPassFilter(freq))
    }

    return eq.build()
}

let My8BandEQ = Equalizer(8)
```
