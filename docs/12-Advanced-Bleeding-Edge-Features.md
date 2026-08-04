# 12 - Advanced Bleeding-Edge Features

**Frust** incorporates the four most powerful concepts from modern programming language research across Lisp, Haskell, OCaml, and Unison:

---

## 1. Lisp-Style AST Code Quotation (`quote` / `unquote`)

```frust
// Metaprogram: Synthesizes AST expressions directly
fn build_poly(a: f32, b: f32) -> ASTExpr = build_time {
    return quote { (unquote(a) * x) + unquote(b) }
}
```

---

## 2. Refinement Types & Value Predicates

```frust
type NormalizedSample = f32[-1.0 .. 1.0]
type NonZeroI32       = i32[!= 0]

pub fn safe_divide(n: i32, d: NonZeroI32) -> i32 = n / d
```

---

## 3. First-Class Algebraic Effects & Resumable Handlers

```frust
effect ReadSensor -> f32
effect Log(msg: String)

pub fn process() = {
    let val = perform ReadSensor()
    perform Log("Read: " + val.to_string())
}

// Resumable Handler
handle process() with {
    effect ReadSensor() => resume(42.0)
    effect Log(msg)    => { println(msg); resume() }
}
```

---

## 4. AST Content-Addressing for Zero-Latency OrcJIT Hot-Reloading

Frust hashes every component's AST using BLAKE3 hashes. Unchanged AST hashes bypass LLVM re-compilation instantly with **0ms latency**!
