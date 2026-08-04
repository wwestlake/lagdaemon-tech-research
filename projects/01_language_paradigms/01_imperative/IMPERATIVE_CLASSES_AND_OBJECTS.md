# Imperative Classes, Objects & Stateful Programming in Frust

## 1. Overview & Vision

While **Frust** provides functional expressions, components, and monadic abstractions, real-world software engineering (GUI desktop apps, OS kernels, device drivers, and game state engines) frequently requires **imperative programming, stateful classes, and object instantiation**.

Frust unifies object-oriented imperative programming with its smart-pointer and component system.

---

## 2. Imperative Control Flow & Mutable State (`mut`)

Imperative code blocks in Frust support explicit mutable bindings (`mut`), sequential statements, and standard control flow loops:

```frust
pub fn imperative_accumulator(limit: i32) -> i32 = {
    let mut sum: i32 = 0
    let mut i: i32 = 1

    while i <= limit {
        if i % 2 == 0 {
            sum = sum + i
        }
        i = i + 1
    }

    return sum
}
```

---

## 3. Class Definitions (`class`) & Methods

A **Class** in Frust encapsulates internal data fields and provides constructors, instance methods, and static methods:

```frust
pub class AudioSynthesizer implements AudioProcessor {
    // Encapsulated Fields (Private by default)
    frequency: f32,
    mut volume: f32,
    mut is_playing: bool,
    buffer: own AudioBuffer,

    // Constructor
    pub fn new(initial_freq: f32) -> AudioSynthesizer = {
        AudioSynthesizer {
            frequency: initial_freq,
            volume: 0.8,
            is_playing: false,
            buffer: own AudioBuffer::new(512)
        }
    }

    // Instance Method (Immutable Borrow)
    pub fn get_frequency(&self) -> f32 = {
        return self.frequency
    }

    // Instance Method (Mutable State Mutation)
    pub fn set_volume(&mut self, new_vol: f32) = {
        self.volume = new_vol.clamp(0.0, 1.0)
    }

    // Processing Method
    pub fn trigger(&mut self) = {
        self.is_playing = true
    }
}
```

---

## 4. Object Instantiation & Smart Pointer Allocation

Objects can be instantiated on the stack or allocated using Frust's built-in smart pointers (`own`, `shared`):

```frust
// 1. Stack Allocation
let mut synth = AudioSynthesizer::new(440.0)
synth.set_volume(0.9)

// 2. Unique Heap Allocation (Exclusive Owner)
let mut heap_synth: own AudioSynthesizer = own AudioSynthesizer::new(880.0)
heap_synth.trigger()

// 3. Shared Ref-Counted Allocation (Multi-Threaded / UI State)
let shared_synth: shared AudioSynthesizer = shared AudioSynthesizer::new(220.0)
```

---

## 5. Interface & Class Inheritance Architecture

Classes implement verifiable interfaces or inherit behavior from trait interfaces:

```frust
interface Renderable {
    fn draw(&self, canvas: &mut Canvas)
}

pub class Button implements Renderable {
    pub label: String,
    pub mut is_hovered: bool,

    pub fn draw(&self, canvas: &mut Canvas) = {
        canvas.draw_rect(self.label)
    }
}
```
