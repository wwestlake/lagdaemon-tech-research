# 11 - Generated Streams, Lazy Sequences & Set-Builder Notation

In **Frust**, mathematical sequence generation and collection construction are first-class language primitives.

---

## 1. Set-Builder Notation

Frust provides direct mathematical set-builder syntax:

$$\{ x^2 \mid x \in [0..10], x \text{ is even} \}$$

```frust
// Set Builder
let even_squares: Set<i32> = { x * x | x <- 0..10, x % 2 == 0 }

// Array Builder
let scaled_audio: Array<f32, 512> = [ sample * 1.5 | sample <- raw_buffer, sample > 0.01 ]

// Map Builder
let user_map: Map<u64, String> = { u.id: u.name | u <- users, u.is_active }
```

---

## 2. Lazy Stream Generators (`Stream<T>` & `yield`)

Generators represent infinite or demand-driven sequences with zero memory overhead:

```frust
// Infinite Fibonacci Generator
pub fn fibonacci_stream() -> Stream<u64> = generator {
    let mut a: u64 = 0
    let mut b: u64 = 1
    loop {
        yield a
        let next = a + b
        a = b
        b = next
    }
}

// Pull first 10 items lazily
let first_10: Array<u64, 10> = fibonacci_stream().take(10)
```

---

## 3. Lazy Stream Set-Builders

Set-builder comprehensions over streams evaluate lazily on demand:

```frust
// Filtered audio peak stream
let peaks = stream { sample * 2.0 | sample <- sine_generator(440.0, 44100.0), abs(sample) > 0.8 }

// Pull 64 samples into audio output buffer
let buffer: Array<f32, 64> = peaks.take(64)
```
