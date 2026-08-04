# 02 - First-Class Verifiable Components

In traditional programming, functions or objects are the primary abstraction. In the **LagDaemon Component Architecture**, the fundamental unit of computation is the **First-Class Component**.

A **Component** is an autonomous, encapsulated functional unit that:
1. Exposes **Verifiable Interfaces** with contractual input and output ports.
2. Is a **First-Class Value** (can be created dynamically, passed to higher-order components, transformed, and returned).
3. Connects to other components via **Type-Checked & Contract-Verified Wiring**, where connections are verified at compile-time or dynamic JIT link-time.

---

## Verifiable Interfaces (`interface`)

An **Interface** specifies the precise contract of communication:

```ldfn
interface AudioProcessor {
    in  signal:  Stream<f32>[range: -1.0 .. 1.0]
    in  control: ControlSignal[freq: <= 1000.0]
    out output:  Stream<f32>[range: -1.0 .. 1.0]

    where {
        latency <= 64,          // Latency guarantee in samples
        thread_safe == true     // Real-time audio thread safety proof
    }
}
```

---

## First-Class Component Definition (`component`)

Components implement one or more interfaces:

```ldfn
component GainFilter(factor: f32) : AudioProcessor {
    in signal
    in control
    out output

    output = signal * factor * (control / 1000.0)
}
```

---

## Composable Wiring (`~>`)

```ldfn
fn build_processing_chain(gain: f32, lowpass_cutoff: f32) -> Component<AudioProcessor> = {
    let stage1 = GainFilter(gain)
    let stage2 = LowPassFilter(lowpass_cutoff)

    // Compiler verifies that stage1.output satisfies stage2.signal contracts!
    let chain = stage1.output ~> stage2.signal
    return chain
}
```
