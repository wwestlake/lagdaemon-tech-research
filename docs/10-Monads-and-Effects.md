# 10 - Monads & Composable Monadic Architecture

In **Frust**, **Monads** are first-class functional abstractions used to handle side-effects, error propagation, optionality, state transitions, and async operations—without category-theory jargon or performance overhead.

---

## 1. Built-in Monadic Types

* **`Option<T>`**: `Some(T)` \| `None` (replaces null pointer crashes).
* **`Result<T, E>`**: `Ok(T)` \| `Err(E)` (replaces exceptions with explicit error handling).
* **`State<S, A>`**: Pure functional state transitions (`fn(S) -> (A, S)`).

---

## 2. Monad Sequencing Syntax

### `do` Notation Syntax (`<-`)
Sequences monadic operations without callback nesting:

```frust
pub fn get_channel_gain(device_id: u32) -> Option<f32> = do {
    let dev  <- find_audio_device(device_id) // Unwraps Option
    let chan <- dev.get_channel(0)          // Unwraps Option
    let gain <- chan.read_gain()             // Unwraps Option

    return Some(gain * 2.0)
}
```

### The `?` Try Operator
Propagates errors or `None` values automatically:

```frust
pub fn load_config(path: &str) -> Result<Config, IoError> = {
    let file = File::open(path)?
    let text = file.read_to_string()?
    Ok(Config::parse(text)?)
}
```

---

## 3. Monad Interface Contract & Chaining

```frust
interface Monad<M, A> {
    fn pure(value: A) -> M<A>
    fn bind<B>(self: M<A>, f: fn(A) -> M<B>) -> M<B>
}

// Method Chaining
let result = find_user(42)
    .map(fn(u) => u.email)
    .and_then(send_email)
    .unwrap_or("Default")
```
