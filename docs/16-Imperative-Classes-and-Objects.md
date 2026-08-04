# 16 - Imperative Classes, Objects & Stateful Programming

In **Frust**, object-oriented imperative programming is seamlessly integrated with functional expressions, verifiable components, and built-in smart pointers.

---

## 1. Imperative Statements & Mutable Variables (`mut`)

```frust
pub fn calculate_sum(limit: i32) -> i32 = {
    let mut sum: i32 = 0
    let mut i: i32 = 1

    while i <= limit {
        sum = sum + i
        i = i + 1
    }

    return sum
}
```

---

## 2. Class Definitions (`class`)

```frust
pub class AudioSynthesizer implements Renderable {
    // Private Fields
    frequency: f32,
    mut volume: f32,
    buffer: own AudioBuffer,

    // Constructor
    pub fn new(freq: f32) -> AudioSynthesizer = {
        AudioSynthesizer {
            frequency: freq,
            volume: 0.8,
            buffer: own AudioBuffer::new(512)
        }
    }

    // Instance Method (Mutable Mutation)
    pub fn set_volume(&mut self, new_vol: f32) = {
        self.volume = new_vol
    }
}
```

---

## 3. Object Allocation Modes

```frust
// Stack Allocation
let mut synth = AudioSynthesizer::new(440.0)
synth.set_volume(0.9)

// Unique Heap Object (`own`)
let mut unique_synth: own AudioSynthesizer = own AudioSynthesizer::new(880.0)

// Shared Ref-Counted Object (`shared`)
let shared_synth: shared AudioSynthesizer = shared AudioSynthesizer::new(220.0)
```
