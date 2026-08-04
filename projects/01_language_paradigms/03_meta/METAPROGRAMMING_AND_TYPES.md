# LagDaemon Metaprogramming & Type Construction Model

## 1. The Core Philosophy

> *"Metaprogramming is simply describing to the compiler how I want the program constructed. It should not be an arcane, esoteric template language."*

In traditional languages like C++, templates are an accidental, Turing-complete sub-language with bizarre syntax (`typename std::enable_if<...>::type`, SFINAE, 500-line error tracebacks). 

In LagDaemon:
1. **Types are First-Class Values** at compile time (`Type` is just a data type).
2. **Metaprogramming IS Regular Code**: You write normal functions that run at compile time (via LLVM OrcJIT) to construct data layouts and components.
3. **No Special Template Syntax**: No `<T, U>`, no angle brackets, no SFINAE tricks.

---

## 2. Type Construction as Plain Functions

Instead of complex template definitions, parameterized types are created by plain functions that take a `Type` parameter and return a constructed `Type`:

```ldfn
// A function that constructs a Buffer data layout for any element Type and capacity
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

## 3. Metaprogramming Component Generators

Metaprogramming for components uses plain `build_time` execution. You inspect interface requirements using normal `if`/`match` code and build the resulting component dynamically:

```ldfn
// Metaprogram: Constructs a custom equalizer component with N frequency bands
fn Equalizer(bands: Int) -> Component<AudioProcessor> = build_time {
    let eq = ComponentBuilder("Equalizer")

    eq.add_input("audio_in", Stream<f32>)
    eq.add_output("audio_out", Stream<f32>)

    // Loop at compile time to create N filter stages!
    for i in 0 .. bands {
        let freq = 100.0 * (2.0 ^ i)
        let filter_stage = LowPassFilter(freq)
        eq.append_stage(filter_stage)
    }

    return eq.build()
}

// Usage: Generates an 8-band Equalizer component instantly!
let My8BandEQ = Equalizer(8)
```

---

## 4. Clear, Plain-Text Compiler Diagnostics

Because metaprogramming runs as standard code inside OrcJIT during compilation:
* If a contract or type check fails, the compiler prints normal, readable execution tracebacks with exact line numbers.
* No 500-line C++ template instantiation errors!
* `assert(ElementType.has_feature(Printable), "Buffer element type must be printable")` gives clean, user-defined error messages.

---

## 5. Summary Comparison

| Feature | C++ Templates / Rust Generics | LagDaemon Metaprogramming |
| :--- | :--- | :--- |
| **Language** | Separate, arcane template sub-language | Standard language code |
| **Types** | Static compile-time entities | First-class `Type` values |
| **Syntax** | Complex `<T, typename std::enable_if...>` | Plain function parameters `(T: Type)` |
| **Execution** | Template expansion phase | In-memory LLVM OrcJIT execution |
| **Error Messages** | Massive 500-line tracebacks | Clean line-numbered error assertions |
